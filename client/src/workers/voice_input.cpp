#include "voice_input.h"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <qthread.h>
#include <soundio/soundio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

// Audio configuration (must match server)
#define SAMPLE_RATE 48000
#define CHUNK_SIZE 240 // 5ms at 48kHz
#define PORT 8888

// Ring buffer settings (SPSC)
#define RING_MS 100
#define RING_SIZE ((SAMPLE_RATE * RING_MS) / 1000)

static struct SoundIo *soundio = nullptr;
static struct SoundIoDevice *in_device = nullptr;
static struct SoundIoInStream *instream = nullptr;

// SPSC ring buffer
static float *ring_buffer = nullptr;
static std::atomic<int> write_index{0}; // next write pos (mod ring_buffer_size)
static std::atomic<int> read_index{0};  // next read pos (mod ring_buffer_size)
static std::atomic<bool> running{true};
static std::atomic<bool> connected{false};

void run(std::string server_ip);

void VoiceInput::init(std::string ip) {
    server_ip = ip;

    soundio = nullptr;
    in_device = nullptr;
    instream = nullptr;
    ring_buffer = nullptr;

    write_index.store(0); // next write pos (mod ring_buffer_size)
    read_index.store(0);  // next read pos (mod ring_buffer_size)
    running.store(true);
    connected.store(false);

    main = std::thread(run, server_ip);
}

void VoiceInput::stop() {
    running.store(false);
    if (soundio) {
        soundio_wakeup(soundio);
    }
    main.join();
    QThread::currentThread()->quit();
}

// Helper to compute fill count (samples available)
static inline int ring_fill_count() {
    int w = write_index.load(std::memory_order_acquire);
    int r = read_index.load(std::memory_order_acquire);
    int fill = w - r;
    if (fill < 0)
        fill += RING_SIZE;
    return fill;
}

// Produce samples into the ring buffer from audio callback
static void produce_samples(const float *samples, int nframes) {
    int w = write_index.load(std::memory_order_relaxed);
    for (int i = 0; i < nframes; ++i) {
        if (!samples) {
            ring_buffer[w] = 0.0f;
        } else {
            ring_buffer[w] = samples[i];
        }
        w = (w + 1) % RING_SIZE;
        // If buffer would overflow, advance read index (drop oldest sample)
        int next_w = w;
        int r = read_index.load(std::memory_order_relaxed);
        if (next_w == r) {
            // overflow: drop oldest
            r = (r + 1) % RING_SIZE;
            read_index.store(r, std::memory_order_relaxed);
        }
    }
    write_index.store(w, std::memory_order_release);
}

// Audio input callback
static void read_callback(struct SoundIoInStream *instream, int frame_count_min, int frame_count_max) {
    struct SoundIoChannelArea *areas;
    int err;
    int frames_left = frame_count_min;

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

// Network thread: reads CHUNK_SIZE samples from ring buffer and sends them
void network_thread(const std::string server_ip) {
    while (running.load()) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("socket");
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        int flag = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        // enlarge send buffer
        int sndbuf = CHUNK_SIZE * sizeof(float) * 8;
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

        struct sockaddr_in srv;
        srv.sin_family = AF_INET;
        srv.sin_port = htons(PORT);
        inet_pton(AF_INET, server_ip.c_str(), &srv.sin_addr);

        if (::connect(sock, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
            perror("connect");
            close(sock);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        printf("Connected to server, streaming audio...\n");
        connected.store(true);

        std::vector<float> sendbuf(CHUNK_SIZE);

        while (running.load() && connected.load()) {
            // If not enough samples, sleep a tiny bit
            int available = ring_fill_count();
            while (available < CHUNK_SIZE) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                available = ring_fill_count();
            }

            int r = read_index.load(std::memory_order_relaxed);
            for (int i = 0; i < CHUNK_SIZE; ++i) {
                sendbuf[i] = ring_buffer[r];
                r = (r + 1) % RING_SIZE;
            }
            read_index.store(r, std::memory_order_release);

            // sendbuf ready: send all bytes
            size_t bytes_to_send = CHUNK_SIZE * sizeof(float);
            char *p = (char *)sendbuf.data();
            while (bytes_to_send > 0) {
                ssize_t n = send(sock, p, bytes_to_send, MSG_NOSIGNAL);
                if (n <= 0) {
                    perror("send");
                    connected.store(false);
                    break;
                }
                p += n;
                bytes_to_send -= n;
            }

            // yield thread
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
        }

        connected.store(false);
        close(sock);
        printf("Disconnected from server\n");
        if (running.load())
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void run(std::string server_ip) {
    // allocate ring buffer
    ring_buffer = new float[RING_SIZE];
    if (!ring_buffer) {
        fprintf(stderr, "Unable to allocate ring buffer\n");
        return;
    }
    memset(ring_buffer, 0, RING_SIZE * sizeof(float));
    write_index.store(0);
    read_index.store(0);

    int err;
    soundio = soundio_create();
    if (!soundio) {
        fprintf(stderr, "Out of memory\n");
        return;
    }
    err = soundio_connect(soundio);
    if (err) {
        fprintf(stderr, "Error connecting: %s\n", soundio_strerror(err));
        return;
    }
    soundio_flush_events(soundio);

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

    err = soundio_instream_open(instream);
    if (err) {
        fprintf(stderr, "Unable to open input stream: %s\n", soundio_strerror(err));
        return;
    }

    err = soundio_instream_start(instream);
    if (err) {
        fprintf(stderr, "Unable to start input stream: %s\n", soundio_strerror(err));
        return;
    }

    std::thread net_t(network_thread, server_ip);

    printf("AUDIO CLIENT STARTED: streaming to %s:%d\n", server_ip.c_str(), PORT);

    // main loop: pump soundio events
    while (running.load()) {
        soundio_wait_events(soundio);
    }

    running.store(false);
    connected.store(false);

    net_t.join();

    soundio_instream_destroy(instream);
    soundio_device_unref(in_device);
    soundio_destroy(soundio);
    delete[] ring_buffer;

    return;
}
