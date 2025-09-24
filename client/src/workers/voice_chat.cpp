#include "voice_chat.h"
#include "packets.h"
#include <QThread>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <soundio/soundio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// Audio configuration (must match server)
#define SAMPLE_RATE 48000
#define CHUNK_SIZE 240 // 5ms at 48kHz
#define VC_PORT 8888

// Ring buffer settings (SPSC)
#define RING_MS 100

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

static std::atomic<bool> running{true};
static std::atomic<bool> connected{false};
static int vc_socket = -1;
static uint32_t channel;

void run();

void VoiceChat::init(std::string ip, uint32_t ch) {
    channel = ch;

    // Voice socket
    vc_socket = socket(AF_INET, SOCK_STREAM, 0);
    int flag = 1;
    setsockopt(vc_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // enlarge send buffer
    int sndbuf = CHUNK_SIZE * sizeof(float) * 8;
    setsockopt(vc_socket, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    struct sockaddr_in srv;
    srv.sin_family = AF_INET;
    srv.sin_port = htons(VC_PORT);
    inet_pton(AF_INET, ip.c_str(), &srv.sin_addr);

    if (::connect(vc_socket, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("connect");
        close(vc_socket);
        return;
    }

    running.store(true);
    connected.store(true);

    main = std::thread(run);
}

void VoiceChat::stop() {
    running.store(false);
    if (soundio) {
        soundio_wakeup(soundio);
    }
    main.join();
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
    struct SoundIoChannelArea *areas;
    int err;
    int frames_left = frame_count_min;

    // std::cout << "frames_left = " << frames_left << std::endl;
    while (frames_left > 0) {
        int frame_count = frames_left;
        err = soundio_instream_begin_read(instream, &areas, &frame_count);
        if (err) {
            fprintf(stderr, "Begin read error: %s\n", soundio_strerror(err));
            exit(1);
        }
        if (frame_count == 0)
            break;

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
            fprintf(stderr, "End read error: %s\n", soundio_strerror(err));
            exit(1);
        }
        frames_left -= frame_count;
    }
}

// Audio output callback
static void write_callback(struct SoundIoOutStream *outstream, int frame_count_min, int frame_count_max) {
    struct SoundIoChannelArea *areas;
    int err;

    int frames_left = frame_count_min;

    while (frames_left > 0) {
        int frame_count = frames_left;
        err = soundio_outstream_begin_write(outstream, &areas, &frame_count);
        if (err) {
            fprintf(stderr, "Begin write error: %s\n", soundio_strerror(err));
            exit(1);
        }

        if (!frame_count)
            break;

        int current_read = read_index_output.load(std::memory_order_relaxed);
        int fill_count = ring_fill_count(write_index_output, read_index_output);

        for (int frame = 0; frame < frame_count; frame++) {
            float sample = 0.0f;

            // Read from network buffer
            if (fill_count > 0) {
                sample = ring_buffer_output[current_read];
                current_read = (current_read + 1) % ring_buffer_size;
                fill_count--;
            } else {
                // If underflow: output silence
                sample = 0.0f;
            }

            // Write mono sample to all channels
            for (int channel = 0; channel < outstream->layout.channel_count; channel++) {
                float *ptr = (float *)(areas[channel].ptr + areas[channel].step * frame);
                *ptr = sample;
            }
        }

        read_index_output.store(current_read, std::memory_order_release);

        if ((err = soundio_outstream_end_write(outstream))) {
            fprintf(stderr, "End write error: %s\n", soundio_strerror(err));
            exit(1);
        }

        frames_left -= frame_count;
    }
}

static void underflow_callback(struct SoundIoOutStream *outstream) {
    static int count = 0;
    fprintf(stderr, "Underflow %d (network latency too high)\n", ++count);
}

void network_send_thread() {
    send_packet(vc_socket, PacketType::UINT, channel);

    printf("Connected to server, streaming audio...\n");

    std::vector<float> sendbuf(CHUNK_SIZE);
    while (running.load() && connected.load()) {
        // If not enough samples, sleep a tiny bit
        int available = ring_fill_count(write_index_input, read_index_input);
        while (available < CHUNK_SIZE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            available = ring_fill_count(write_index_input, read_index_input);
        }

        int r = read_index_input.load(std::memory_order_relaxed);
        for (int i = 0; i < CHUNK_SIZE; ++i) {
            sendbuf[i] = ring_buffer_input[r];
            r = (r + 1) % ring_buffer_size;
        }
        read_index_input.store(r, std::memory_order_release);

        // sendbuf ready: send all bytes
        if (!send_all(vc_socket, sendbuf.data(), CHUNK_SIZE * sizeof(float))) {
            std::cerr << "Error sending voice" << std::endl;
            connected.store(false);
        }

        // yield thread
        std::this_thread::sleep_for(std::chrono::milliseconds(0));
    }

    connected.store(false);
    printf("Disconnected from server\n");

    std::cout << "[Both] Network send exited" << std::endl;
}

void network_recv_thread() {
    std::vector<float> chunk(CHUNK_SIZE);

    printf("VC connected, receiving audio...\n");

    int current_write = write_index_output.load(std::memory_order_relaxed);
    while (running.load()) {
        bool got = recv_all(vc_socket, chunk.data(), CHUNK_SIZE * sizeof(float));
        if (!got) {
            printf("VC disconnected or read error\n");
            break;
        }

        // Check if buffer would overflow
        int fill_count = ring_fill_count(write_index_output, read_index_output);
        if (fill_count + CHUNK_SIZE >= ring_buffer_size - 1) {
            // Drop packet
            continue;
        }

        // Write chunk to ring buffer
        for (int i = 0; i < CHUNK_SIZE; i++) {
            ring_buffer_output[current_write] = chunk[i];
            current_write = (current_write + 1) % ring_buffer_size;
        }

        write_index_output.store(current_write, std::memory_order_release);
    }

    std::cout << "[Both] Network recv exited" << std::endl;
}

void start_input_stream() {
    // allocate ring buffer
    ring_buffer_input = new float[ring_buffer_size];
    if (!ring_buffer_input) {
        fprintf(stderr, "Unable to allocate ring buffer\n");
        return;
    }
    memset(ring_buffer_input, 0, ring_buffer_size * sizeof(float));

    int default_input_index = soundio_default_input_device_index(soundio);
    if (default_input_index < 0) {
        fprintf(stderr, "No input device found\n");
        return;
    }
    in_device = soundio_get_input_device(soundio, default_input_index);
    if (!in_device) {
        fprintf(stderr, "Could not get input device\n");
        return;
    }
    printf("Input device: %s\n", in_device->name);

    instream = soundio_instream_create(in_device);
    instream->format = SoundIoFormatFloat32NE;
    instream->layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdMono);
    instream->sample_rate = SAMPLE_RATE;
    instream->software_latency = 0.005; // 5 ms capture latency
    instream->read_callback = read_callback;
    instream->overflow_callback = nullptr;

    int err = soundio_instream_open(instream);
    if (err) {
        fprintf(stderr, "Unable to open input stream: %s\n", soundio_strerror(err));
        return;
    }

    err = soundio_instream_start(instream);
    if (err) {
        fprintf(stderr, "Unable to start input stream: %s\n", soundio_strerror(err));
        return;
    }
}

void start_output_stream() {
    // Create ring buffer for network audio
    ring_buffer_output = new float[ring_buffer_size];
    if (!ring_buffer_output) {
        std::cerr << "Unable to allocate ring buffer\n";
        return;
    }
    memset(ring_buffer_output, 0, ring_buffer_size * sizeof(float));
    write_index_output.store(0);
    read_index_output.store(0);

    // Get default output device
    int default_output_index = soundio_default_output_device_index(soundio);
    if (default_output_index < 0) {
        std::cerr << "No output device found\n";
        return;
    }
    out_device = soundio_get_output_device(soundio, default_output_index);
    if (!out_device) {
        std::cerr << "Could not get output device\n";
        return;
    }

    std::cout << "Output device: " << out_device->name << "\n";

    // Create output stream
    outstream = soundio_outstream_create(out_device);
    outstream->format = SoundIoFormatFloat32NE;
    outstream->layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdStereo);
    outstream->sample_rate = SAMPLE_RATE;
    outstream->software_latency = 0.005; // 5ms
    outstream->write_callback = write_callback;
    outstream->underflow_callback = underflow_callback;

    int err = soundio_outstream_open(outstream);
    if (err) {
        std::cerr << "Unable to open output stream: " << soundio_strerror(err) << "\n";
        return;
    }

    err = soundio_outstream_start(outstream);
    if (err) {
        std::cerr << "Unable to start output stream: " << soundio_strerror(err) << "\n";
        return;
    }
}

void run() {
    // Initialize soundio
    soundio = soundio_create();
    if (!soundio) {
        std::cerr << "Out of memory\n";
        return;
    }
    int err = soundio_connect(soundio);
    if (err) {
        std::cerr << "Error connecting: " << soundio_strerror(err) << "\n";
        return;
    }
    soundio_flush_events(soundio);

    // Initialize soundio streams
    start_input_stream();
    start_output_stream();

    // Start network threads
    std::thread net_send(network_send_thread);
    std::thread net_recv(network_recv_thread);

    std::cout << "VOICE CHAT STARTED: streaming\n";

    // main loop: pump soundio events
    while (running.load()) {
        soundio_wait_events(soundio);
    }

    // Cleanup
    running.store(false);
    connected.store(false);

    net_send.join();
    net_recv.join();

    close(vc_socket);
    soundio_instream_destroy(instream);
    soundio_outstream_destroy(outstream);
    soundio_device_unref(in_device);
    soundio_device_unref(out_device);
    soundio_destroy(soundio);
    delete[] ring_buffer_input;
    delete[] ring_buffer_output;

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

    std::cout << "[Both] Main exited" << std::endl;
    return;
}
