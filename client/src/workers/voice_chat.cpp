#include "voice_chat.h"
#include "crossSockets.h"
#include "logger.h"
#include "packets.h"
#include <QThread>
#include <atomic>
#include <cstring>
#include <opus.h>
#include <soundio/soundio.h>
#include <string>
#include <thread>
#include <unistd.h>

// Audio configuration (must match server)
#define SAMPLE_RATE 48000
#define CHUNK_SIZE 120 // 2.5ms at 48kHz | Minimum Opus frame size

// Ring buffer settings
#define RING_MS 150
#define BYTES_PER_FRAME (int)sizeof(float)

#define SW_LATENCY 0.005 // in s

// Opus
#define MAX_OPUS_BYTES 1276

static struct SoundIo *soundio = nullptr;
static struct SoundIoDevice *in_device = nullptr;
static struct SoundIoInStream *instream = nullptr;
static struct SoundIoDevice *out_device = nullptr;
static struct SoundIoOutStream *outstream = nullptr;

// Ring buffers
SoundIoRingBuffer *ring_buffer_input = nullptr;
SoundIoRingBuffer *ring_buffer_output = nullptr;

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

static inline int frames_fill_count(SoundIoRingBuffer *rb) {
    return soundio_ring_buffer_fill_count(rb) / BYTES_PER_FRAME;
}

// Produce samples into the SoundIo ring buffer from Input callback
static void produce_samples(const float *samples, int nframes) {
    const int bytes_to_write = nframes * BYTES_PER_FRAME;

    int free_bytes = soundio_ring_buffer_free_count(ring_buffer_input);

    // If not enough space, drop old samples (advance read pointer)
    if (free_bytes < bytes_to_write) {
        int overflow_bytes = bytes_to_write - free_bytes;
        overflow_bytes = (overflow_bytes / BYTES_PER_FRAME) * BYTES_PER_FRAME;

        soundio_ring_buffer_advance_read_ptr(ring_buffer_input, overflow_bytes);
    }

    char *write_ptr = soundio_ring_buffer_write_ptr(ring_buffer_input);
    float *out = reinterpret_cast<float *>(write_ptr);

    if (samples) {
        // Copy samples directly
        memcpy(out, samples, bytes_to_write);
    } else {
        // Fill with silence
        for (int i = 0; i < nframes; i++) {
            out[i] = 0.0f;
        }
    }

    soundio_ring_buffer_advance_write_ptr(ring_buffer_input, bytes_to_write);
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
            int device_channels = instream->layout.channel_count;
            while (processed < frame_count) {
                int tocopy = std::min(CHUNK_SIZE, frame_count - processed);
                float temp[CHUNK_SIZE];

                if (device_channels == 1) {
                    // straightforward copy from areas[0]
                    for (int i = 0; i < tocopy; ++i) {
                        temp[i] = *((float *)(areas[0].ptr + (processed + i) * areas[0].step));
                    }
                } else {
                    // mix all channels to mono (average)
                    for (int i = 0; i < tocopy; ++i) {
                        float sum = 0.0f;
                        for (int c = 0; c < device_channels; ++c) {
                            float *src = (float *)(areas[c].ptr + (processed + i) * areas[c].step);
                            sum += *src;
                        }
                        temp[i] = sum / device_channels;
                    }
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

        float *buffer = (float *)soundio_ring_buffer_read_ptr(ring_buffer_output);
        int fill_count = frames_fill_count(ring_buffer_output);
        int copy_frames = std::min(fill_count, frame_count);
        int silence_frames = frame_count - copy_frames;

        for (int frame = 0; frame < copy_frames; frame++) {
            float sample = buffer[frame];

            // Write mono sample to all channels
            for (int ch = 0; ch < outstream->layout.channel_count; ch++) {
                float *ptr = (float *)(areas[ch].ptr + areas[ch].step * frame);
                *ptr = sample;
            }
        }

        for (int frame = copy_frames; frame < frame_count; frame++) {
            for (int ch = 0; ch < outstream->layout.channel_count; ch++) {
                float *ptr = (float *)(areas[ch].ptr + areas[ch].step * frame);
                *ptr = 0.0f;
            }
        }

        soundio_ring_buffer_advance_read_ptr(ring_buffer_output, copy_frames * BYTES_PER_FRAME);
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
        int available = frames_fill_count(ring_buffer_input);
        while (available < CHUNK_SIZE && running.load()) {
            // std::this_thread::sleep_for(std::chrono::milliseconds(1)); Windows doesnt play nice with this. Maybe change this to a event wait
            available = frames_fill_count(ring_buffer_input);
        }

        // Check again after the wait loop
        if (!running.load()) {
            break;
        }

        float *read_buf = (float *)soundio_ring_buffer_read_ptr(ring_buffer_input);
        memcpy(sendbuf.data(), read_buf, CHUNK_SIZE * BYTES_PER_FRAME);
        soundio_ring_buffer_advance_read_ptr(ring_buffer_input, CHUNK_SIZE * BYTES_PER_FRAME);

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
        int free = soundio_ring_buffer_free_count(ring_buffer_output);
        int bytes_to_write = frame_count * BYTES_PER_FRAME;
        if (free < bytes_to_write) {
            LOG_DEBUG("Packet Dropped");
            continue;
        }

        // Write chunk to ring buffer
        float *buffer = (float *)soundio_ring_buffer_write_ptr(ring_buffer_output);
        memcpy(buffer, chunk.data(), bytes_to_write);
        soundio_ring_buffer_advance_write_ptr(ring_buffer_output, bytes_to_write);

        // std::this_thread::sleep_for(std::chrono::milliseconds(2));
        std::this_thread::yield();
    }

    LOG_DEBUG("Network recv exited");
}

void start_input_stream() {
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

    // allocate ring buffer
    int capacity = (int)(((double)RING_MS / 1000.0) * SAMPLE_RATE) * BYTES_PER_FRAME;
    ring_buffer_input = soundio_ring_buffer_create(soundio, capacity);
    if (!ring_buffer_input) {
        LOG_ERROR("Unable to allocate ring buffer");
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

    // allocate ring buffer
    int capacity = (int)(((double)RING_MS / 1000.0) * SAMPLE_RATE) * BYTES_PER_FRAME;
    ring_buffer_output = soundio_ring_buffer_create(soundio, capacity);
    if (!ring_buffer_output) {
        LOG_ERROR("Unable to allocate ring buffer");
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
    soundio_ring_buffer_destroy(ring_buffer_input);
    soundio_ring_buffer_destroy(ring_buffer_output);

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
    vc_socket = -1;
    underflow_count = 0;

    return;
}
