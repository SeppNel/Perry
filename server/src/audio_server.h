
#include <atomic>

namespace AudioServer {
extern std::atomic<bool> running;
void run();
} // namespace AudioServer
