#include "voice_output.h"
#include "packets.h"
#include <arpa/inet.h>
#include <atomic>
#include <iostream>
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

// Audio configuration
#define SAMPLE_RATE 48000
#define CHUNK_SIZE 240 // 5ms at 48kHz
#define PORT 8888

// Network ring buffer
#define RING_MS 100

static struct SoundIo *soundio = nullptr;
static struct SoundIoDevice *out_device = nullptr;
static struct SoundIoOutStream *outstream = nullptr;

static int ring_buffer_size = (SAMPLE_RATE * RING_MS) / 1000;
static float *ring_buffer = nullptr;
static std::atomic<int> write_index{0};
static std::atomic<int> read_index{0};
static std::atomic<bool> running{true};
static std::atomic<bool> connected{false};

static int vc_sock = -1;

void run();

void VoiceOutput::init(int sock) {
    vc_sock = sock;

    soundio = nullptr;
    out_device = nullptr;
    outstream = nullptr;
    ring_buffer = nullptr;

    write_index.store(0); // next write pos (mod ring_buffer_size)
    read_index.store(0);  // next read pos (mod ring_buffer_size)
    running.store(true);
    connected.store(false);

    main = std::thread(run);
}

void VoiceOutput::stop() {
    running.store(false);
    if (soundio) {
        soundio_wakeup(soundio);
    }
    main.join();
    QThread::currentThread()->quit();
}

// Helper to compute fill count (samples available)
static inline int ring_buffer_fill_count() {
    int w = write_index.load(std::memory_order_acquire);
    int r = read_index.load(std::memory_order_acquire);
    int fill_count = w - r;
    if (fill_count < 0)
        fill_count += ring_buffer_size;
    return fill_count;
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

        int current_read = read_index.load(std::memory_order_relaxed);
        int fill_count = ring_buffer_fill_count();

        for (int frame = 0; frame < frame_count; frame++) {
            float sample = 0.0f;

            // Read from network buffer
            if (fill_count > 0) {
                sample = ring_buffer[current_read];
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

        read_index.store(current_read, std::memory_order_release);

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

// Network thread: reads CHUNK_SIZE samples from ring buffer and sends them
void network_thread() {
    std::vector<float> chunk(CHUNK_SIZE);

    printf("Client connected, receiving audio...\n");

    int current_write = write_index.load(std::memory_order_relaxed);
    while (running.load()) {
        ssize_t got = recv_all(vc_sock, chunk.data(), CHUNK_SIZE * sizeof(float));
        if (got <= 0) {
            printf("Client disconnected or read error\n");
            break;
        }
        if (got != CHUNK_SIZE * sizeof(float)) {
            continue;
        }

        // Check if buffer would overflow
        int fill_count = ring_buffer_fill_count();
        if (fill_count + CHUNK_SIZE >= ring_buffer_size - 1) {
            // Buffer overflow scenario: drop packet
            continue;
        }

        // Write chunk to ring buffer
        for (int i = 0; i < CHUNK_SIZE; i++) {
            ring_buffer[current_write] = chunk[i];
            current_write = (current_write + 1) % ring_buffer_size;
        }

        write_index.store(current_write, std::memory_order_release);
    }

    close(vc_sock);
}

void run() {
    int err;

    // Initialize soundio
    soundio = soundio_create();
    if (!soundio) {
        std::cerr << "Out of memory\n";
        return;
    }
    err = soundio_connect(soundio);
    if (err) {
        std::cerr << "Error connecting: " << soundio_strerror(err) << "\n";
        return;
    }
    soundio_flush_events(soundio);

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

    // Create ring buffer for network audio
    ring_buffer = new float[ring_buffer_size];
    if (!ring_buffer) {
        std::cerr << "Unable to allocate ring buffer\n";
        return;
    }
    memset(ring_buffer, 0, ring_buffer_size * sizeof(float));
    write_index.store(0);
    read_index.store(0);

    std::cout << "Network buffer: " << ring_buffer_size << " samples (" << (ring_buffer_size * 1000.0f) / SAMPLE_RATE << " ms)\n";

    // Create output stream
    outstream = soundio_outstream_create(out_device);
    outstream->format = SoundIoFormatFloat32NE;
    outstream->layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdStereo);
    outstream->sample_rate = SAMPLE_RATE;
    outstream->software_latency = 0.005; // 5ms
    outstream->write_callback = write_callback;
    outstream->underflow_callback = underflow_callback;

    err = soundio_outstream_open(outstream);
    if (err) {
        std::cerr << "Unable to open output stream: " << soundio_strerror(err) << "\n";
        return;
    }

    err = soundio_outstream_start(outstream);
    if (err) {
        std::cerr << "Unable to start output stream: " << soundio_strerror(err) << "\n";
        return;
    }

    std::thread net_t(network_thread);
    net_t.join();

    // Cleanup
    running = false;

    soundio_outstream_destroy(outstream);
    soundio_device_unref(out_device);
    soundio_destroy(soundio);
    delete[] ring_buffer;

    return;
}