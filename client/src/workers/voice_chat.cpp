#include "voice_chat.h"
#include "crossSockets.h"
#include "logger.h"
#include "packets.h"
#include <QThread>
#include <atomic>
#include <chrono>
#include <opus.h>
#include <soundio/soundio.h>
#include <string>
#include <thread>
#include <unistd.h>

// Audio configuration (must match server)
#define SAMPLE_RATE 48000
#define CHUNK_SIZE 120 // 5ms at 48kHz | Min of 120 for opus

// Ring buffer settings (SPSC)
#define RING_MS 200

#define SW_LATENCY 0.005 // in s

// Opus
#define MAX_OPUS_BYTES 1276

static struct SoundIo *soundio = nullptr;
static struct SoundIoDevice *in_device = nullptr;
static struct SoundIoInStream *instream = nullptr;
static struct SoundIoDevice *out_device = nullptr;
static struct SoundIoOutStream *outstream = nullptr;

// SPSC ring buffer
static int ring_buffer_size = (SAMPLE_RATE * RING_MS) / 1000;

static float *ring_buffer_input = nullptr;
static std::atomic<int> write_index_input{0}; // next write pos (mod ring_buffer_size)
static std::atomic<int> read_index_input{0};  // next read pos (mod ring_buffer_size)

static float *ring_buffer_output = nullptr;
static std::atomic<int> write_index_output{0};
static std::atomic<int> read_index_output{0};

static int underflow_count = 0;

static std::atomic<bool> running{true};
static int vc_socket = -1;
static uint32_t channel;

// Opus
static OpusEncoder *opus_encoder = nullptr;
static OpusDecoder *opus_decoder = nullptr;

void run();

void init_opus() {
    int err;
    // Encoder
    opus_encoder = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK) {
        LOG_ERROR("opus_encoder_create failed: " + std::string(opus_strerror(err)));
        running.store(false);
        return;
    }

    // Set a target bitrate https://wiki.xiph.org/Opus_Recommended_Settings
    opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(38000));
    opus_encoder_ctl(opus_encoder, OPUS_SET_COMPLEXITY(7));

    // Decoder
    opus_decoder = opus_decoder_create(SAMPLE_RATE, 1, &err);
    if (err != OPUS_OK) {
        LOG_ERROR("opus_decoder_create failed: " + std::string(opus_strerror(err)));
        running.store(false);
        return;
    }
}

void VoiceChat::init(std::string ip, uint port, uint32_t ch) {
    channel = ch;

    // Voice socket
    vc_socket = socket(AF_INET, SOCK_STREAM, 0);
    int flag = 1;
    crossSockets::setSocketOptions(vc_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // enlarge send buffer
    int sndbuf = CHUNK_SIZE * sizeof(float) * 8;
    crossSockets::setSocketOptions(vc_socket, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    struct sockaddr_in srv;
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &srv.sin_addr);

    if (::connect(vc_socket, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("VC connect");
        close(vc_socket);
        vc_socket = -1;
        return;
    }

    running.store(true);
    main = std::thread(run);
}

void VoiceChat::stop() {
    running.store(false);
    shutdown(vc_socket, SHUT_RDWR);

    // Wake up soundio to break out of wait_events
    if (soundio) {
        soundio_wakeup(soundio);
    }

    // Wait for main thread to finish
    if (main.joinable()) {
        main.join();
    }

    LOG_DEBUG("VC stopped fully");

    emit closed();
    QThread::currentThread()->quit();
}

// Helper to compute fill count (samples available)
static inline int ring_fill_count(const std::atomic<int> &write_index, const std::atomic<int> &read_index) {
    int w = write_index.load(std::memory_order_acquire);
    int r = read_index.load(std::memory_order_acquire);
    int fill = w - r;
    if (fill < 0)
        fill += ring_buffer_size;
    return fill;
}

// Produce samples into the ring buffer from audio callback
static void produce_samples(const float *samples, int nframes) {
    int w = write_index_input.load(std::memory_order_relaxed);
    for (int i = 0; i < nframes; ++i) {
        if (!samples) {
            ring_buffer_input[w] = 0.0f;
        } else {
            ring_buffer_input[w] = samples[i];
        }
        w = (w + 1) % ring_buffer_size;
        // If buffer would overflow, advance read index (drop oldest sample)
        int next_w = w;
        int r = read_index_input.load(std::memory_order_relaxed);
        if (next_w == r) {
            // overflow: drop oldest
            r = (r + 1) % ring_buffer_size;
            read_index_input.store(r, std::memory_order_relaxed);
        }
    }
    write_index_input.store(w, std::memory_order_release);
}

// Audio input callback
static void read_callback(struct SoundIoInStream *instream, int frame_count_min, int frame_count_max) {
    // Check if we're shutting down
    if (!running.load()) {
        return;
    }

    struct SoundIoChannelArea *areas;
    int err;
    int frames_left = std::max(CHUNK_SIZE, frame_count_min);
    frames_left = std::min(frames_left, frame_count_max);
    while (frames_left > 0) {
        int frame_count = frames_left;
        err = soundio_instream_begin_read(instream, &areas, &frame_count);
        if (err) {
            LOG_ERROR("Begin read error: " + std::string(soundio_strerror(err)));
            running.store(false);
            return;
        }

        // If no frames, we're done
        if (frame_count == 0) {
            break;
        }

        int processed = 0;
        // If device gives us areas == NULL, fill with silence
        if (!areas) {
            while (processed < frame_count) {
                int tocopy = std::min(CHUNK_SIZE, frame_count - processed);
                produce_samples(nullptr, tocopy);
                processed += tocopy;
            }
        } else {
            // Copy frames into a temporary stack buffer in small blocks to call
            while (processed < frame_count) {
                int tocopy = std::min(CHUNK_SIZE, frame_count - processed);
                float temp[CHUNK_SIZE];
                for (int i = 0; i < tocopy; ++i) {
                    temp[i] = *((float *)(areas[0].ptr + (processed + i) * areas[0].step));
                }
                produce_samples(temp, tocopy);
                processed += tocopy;
            }
        }

        err = soundio_instream_end_read(instream);
        if (err) {
            LOG_ERROR("End read error: " + std::string(soundio_strerror(err)));
            running.store(false);
            return;
        }
        frames_left -= frame_count;
    }
}

// Audio output callback
static void write_callback(struct SoundIoOutStream *outstream, int frame_count_min, int frame_count_max) {
    //  Check if we're shutting down
    if (!running.load()) {
        return;
    }

    static struct SoundIoChannelArea *areas;
    static int err;

    int frames_left = frame_count_max;
    if (frame_count_min > 0) {
        frames_left = frame_count_min;
    }
    while (frames_left > 0) {
        int frame_count = frames_left;
        err = soundio_outstream_begin_write(outstream, &areas, &frame_count);
        if (err) {
            LOG_ERROR("Begin write error: " + std::string(soundio_strerror(err)));
            running.store(false);
            return;
        }

        int current_read = read_index_output.load(std::memory_order_relaxed);
        int fill_count = ring_fill_count(write_index_output, read_index_output);
        int copy_frames = std::min(fill_count, frame_count);
        int silence_frames = frame_count - copy_frames;

        for (int frame = 0; frame < copy_frames; frame++) {
            float sample = ring_buffer_output[current_read];
            current_read = (current_read + 1) % ring_buffer_size;

            // Write mono sample to all channels
            for (int channel = 0; channel < outstream->layout.channel_count; channel++) {
                float *ptr = (float *)(areas[channel].ptr + areas[channel].step * frame);
                *ptr = sample;
            }
        }

        for (int frame = 0; frame < silence_frames; frame++) {
            for (int channel = 0; channel < outstream->layout.channel_count; channel++) {
                float *ptr = (float *)(areas[channel].ptr + areas[channel].step * frame);
                *ptr = 0.0f;
            }
        }

        read_index_output.store(current_read, std::memory_order_release);
        frames_left -= frame_count;

        err = soundio_outstream_end_write(outstream);
        if (err) {
            LOG_ERROR("End write error: " + std::string(soundio_strerror(err)));
            running.store(false);
            return;
        }
    }
}

static void underflow_callback(struct SoundIoOutStream *outstream) {
    underflow_count++;
    LOG_ERROR("Underflow " + std::to_string(underflow_count) + " (network latency too high)");
}

void network_send_thread() {
    send_packet(vc_socket, PacketType::UINT, channel);

    LOG_DEBUG("Connected to server, streaming audio...");

    std::vector<float> sendbuf(CHUNK_SIZE);
    std::vector<unsigned char> opus_buf(MAX_OPUS_BYTES);

    while (running.load()) {
        // If not enough samples, sleep a tiny bit
        int available = ring_fill_count(write_index_input, read_index_input);
        while (available < CHUNK_SIZE && running.load()) {
            // std::this_thread::sleep_for(std::chrono::milliseconds(1)); Windows doesnt play nice with this. Maybe change this to a event wait
            available = ring_fill_count(write_index_input, read_index_input);
        }

        // Check again after the wait loop
        if (!running.load()) {
            break;
        }

        int r = read_index_input.load(std::memory_order_relaxed);
        for (int i = 0; i < CHUNK_SIZE; ++i) {
            sendbuf[i] = ring_buffer_input[r];
            r = (r + 1) % ring_buffer_size;
        }
        read_index_input.store(r, std::memory_order_release);

        int nb_bytes = opus_encode_float(opus_encoder,
                                         sendbuf.data(),
                                         CHUNK_SIZE,
                                         opus_buf.data(),
                                         MAX_OPUS_BYTES);

        if (nb_bytes < 0) {
            LOG_ERROR("Opus encode failed: " + std::string(opus_strerror(nb_bytes)));
            running.store(false);
            break;
        }

        // Send length prefix (uint16 network-order) then packet
        uint16_t len16 = static_cast<uint16_t>(nb_bytes);
        uint16_t len_net = htons(len16);
        if (!send_all(vc_socket, &len_net, sizeof(len_net))) {
            LOG_ERROR("Error sending opus length");
            running.store(false);
            break;
        }
        if (!send_all(vc_socket, opus_buf.data(), nb_bytes)) {
            LOG_ERROR("Error sending voice");
            running.store(false);
        }

        std::this_thread::yield();
    }

    LOG_DEBUG("Network send exited");
}

void network_recv_thread() {
    std::vector<unsigned char> packet_buf(MAX_OPUS_BYTES);
    std::vector<float> chunk(CHUNK_SIZE);

    LOG_DEBUG("Connected, receiving audio...");

    int current_write = write_index_output.load(std::memory_order_relaxed);
    while (running.load()) {
        uint16_t len_net;
        if (!recv_all(vc_socket, &len_net, sizeof(len_net))) {
            LOG_ERROR("Disconnected or read error (len)");
            running.store(false);
            break;
        }
        uint16_t nb_bytes = ntohs(len_net);
        if (nb_bytes == 0 || nb_bytes > (int)packet_buf.size()) {
            LOG_ERROR("Invalid packet length");
            running.store(false);
            break;
        }

        if (!recv_all(vc_socket, packet_buf.data(), nb_bytes)) {
            LOG_ERROR("Disconnected or read error (payload)");
            running.store(false);
            break;
        }

        int frame_count = opus_decode_float(opus_decoder,
                                            packet_buf.data(),
                                            nb_bytes,
                                            chunk.data(),
                                            CHUNK_SIZE,
                                            0);
        if (frame_count < 0) {
            LOG_ERROR("Opus decode failed: " + std::string(opus_strerror(frame_count)));
            continue;
        }

        // Check if buffer would overflow
        int fill_count = ring_fill_count(write_index_output, read_index_output);
        if (fill_count + frame_count >= ring_buffer_size - 1) {
            LOG_DEBUG("Packet Dropped");
            continue;
        }

        // Write chunk to ring buffer
        for (int i = 0; i < frame_count; i++) {
            ring_buffer_output[current_write] = chunk[i];
            current_write = (current_write + 1) % ring_buffer_size;
        }

        write_index_output.store(current_write, std::memory_order_release);
        // std::this_thread::sleep_for(std::chrono::milliseconds(2));
        std::this_thread::yield();
    }

    LOG_DEBUG("Network recv exited");
}

void start_input_stream() {
    // allocate ring buffer
    ring_buffer_input = new float[ring_buffer_size];
    if (!ring_buffer_input) {
        LOG_ERROR("Unable to allocate ring buffer");
        return;
    }
    memset(ring_buffer_input, 0, ring_buffer_size * sizeof(float));

    int default_input_index = soundio_default_input_device_index(soundio);
    if (default_input_index < 0) {
        LOG_ERROR("No input device found");
        running.store(false);
        return;
    }

    in_device = soundio_get_input_device(soundio, default_input_index);
    if (!in_device) {
        LOG_ERROR("Could not get input device");
        running.store(false);
        return;
    }

    LOG_INFO("Input device: " + std::string(in_device->name));
    LOG_INFO("Sample rates supported:");
    for (int i = 0; i < in_device->sample_rate_count; i++) {
        const SoundIoSampleRateRange &range = in_device->sample_rates[i];
        LOG_INFO("  " + std::to_string(range.min) + " - " + std::to_string(range.max));
    }

    LOG_INFO("Formats supported:");
    for (int i = 0; i < in_device->format_count; i++) {
        LOG_INFO("  " + std::string(soundio_format_string(in_device->formats[i])));
    }

    LOG_INFO("Channel layouts:");
    for (int i = 0; i < in_device->layout_count; i++) {
        LOG_INFO("  " + std::string(in_device->layouts[i].name));
    }

    instream = soundio_instream_create(in_device);
    instream->format = SoundIoFormatFloat32NE;
    // If mono not supported, use device’s default layout
    if (soundio_device_supports_layout(in_device, soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdMono))) {
        instream->layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdMono);
    } else {
        instream->layout = in_device->current_layout;
    }
    instream->sample_rate = SAMPLE_RATE;
    instream->software_latency = SW_LATENCY; // 5 ms capture latency
    instream->read_callback = read_callback;
    instream->overflow_callback = nullptr;

    int err = soundio_instream_open(instream);
    if (err) {
        LOG_ERROR("Unable to open input stream: " + std::string(soundio_strerror(err)));
        running.store(false);
        return;
    }

    err = soundio_instream_start(instream);
    if (err) {
        LOG_ERROR("Unable to start input stream: " + std::string(soundio_strerror(err)));
        running.store(false);
        return;
    }
}

void start_output_stream() {
    // Create ring buffer for network audio
    ring_buffer_output = new float[ring_buffer_size];
    if (!ring_buffer_output) {
        LOG_ERROR("Unable to allocate ring buffer");
        running.store(false);
        return;
    }
    memset(ring_buffer_output, 0, ring_buffer_size * sizeof(float));
    write_index_output.store(0);
    read_index_output.store(0);

    // Get default output device
    int default_output_index = soundio_default_output_device_index(soundio);
    if (default_output_index < 0) {
        LOG_ERROR("No output device found");
        running.store(false);
        return;
    }
    out_device = soundio_get_output_device(soundio, default_output_index);
    if (!out_device) {
        LOG_ERROR("Could not get output device");
        running.store(false);
        return;
    }

    LOG_INFO("Output device: " + std::string(out_device->name));

    // Create output stream
    outstream = soundio_outstream_create(out_device);
    outstream->format = SoundIoFormatFloat32NE;
    outstream->layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdStereo);
    outstream->sample_rate = SAMPLE_RATE;
    outstream->software_latency = SW_LATENCY; // 5ms
    outstream->write_callback = write_callback;
    outstream->underflow_callback = underflow_callback;

    int err = soundio_outstream_open(outstream);
    if (err) {
        LOG_ERROR("Unable to open output stream: " + std::string(soundio_strerror(err)));
        running.store(false);
        return;
    }

    err = soundio_outstream_start(outstream);
    if (err) {
        LOG_ERROR("Unable to start output stream: " + std::string(soundio_strerror(err)));
        running.store(false);
        return;
    }
}

void run() {
    // Initialize soundio
    soundio = soundio_create();
    if (!soundio) {
        LOG_ERROR("Out of memory");
        return;
    }
    int err = soundio_connect(soundio);
    if (err) {
        LOG_ERROR("Error connecting: " + std::string(soundio_strerror(err)));
        soundio_destroy(soundio);
        soundio = nullptr;
        return;
    }
    soundio_flush_events(soundio);

    LOG_INFO("Using SoundIo backend: " + std::string(soundio_backend_name(soundio->current_backend)));

    // Initialize soundio streams
    start_input_stream();
    start_output_stream();

    // Check if initialization failed
    if (!running.load()) {
        LOG_ERROR("VC | Initialization failed");
        return;
    }

    // Start Opus En/Decoders
    init_opus();

    // Start network threads
    std::thread net_send(network_send_thread);
    std::thread net_recv(network_recv_thread);

    LOG_INFO("VOICE CHAT STARTED: streaming");

    // main loop: pump soundio events
    while (running.load()) {
        soundio_wait_events(soundio);
    }

    // Cleanup
    // Wait for network threads to finish
    net_send.join();
    net_recv.join();

    // Destroy soundio stuff
    soundio_instream_destroy(instream);
    soundio_outstream_destroy(outstream);
    soundio_device_unref(in_device);
    soundio_device_unref(out_device);
    soundio_destroy(soundio);

    // Clean up ring buffers
    delete[] ring_buffer_input;
    delete[] ring_buffer_output;

    // Destroy opus stuff
    opus_encoder_destroy(opus_encoder);
    opus_decoder_destroy(opus_decoder);

    // Reset everything
    soundio = nullptr;
    in_device = nullptr;
    instream = nullptr;
    out_device = nullptr;
    outstream = nullptr;
    ring_buffer_input = nullptr;
    ring_buffer_output = nullptr;
    write_index_input.store(0);
    read_index_input.store(0);
    write_index_output.store(0);
    read_index_output.store(0);
    vc_socket = -1;
    underflow_count = 0;

    return;
}
