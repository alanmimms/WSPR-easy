/* file: src/sys/Logger.hpp */
#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <mutex>
#include <chrono>
#include <format> // C++20

class RingBufferLogger {
public:
  // Singleton access
  static RingBufferLogger& instance();

  void init(size_t sizeBytes) {
    std::lock_guard<std::mutex> lock(mtx);
    buffer.resize(sizeBytes);
    head = 0;
    full = false;
    log("Logger initialized with " + std::to_string(sizeBytes) + " bytes.");
  }

  void log(std::string_view message) {
    // Get UTC Time
    auto now = std::chrono::system_clock::now();
    // std::format is C++20 (Note: Zephyr support varies, fallback to snprintf if needed)
    // string entry = std::format("[{:%Y-%m-%dT%H:%M:%SZ}] {}\n", now, message);
        
    // Manual formatting for embedded safety:
    char timeBuf[32];
    time_t t = std::chrono::system_clock::to_time_t(now);
    strftime(timeBuf, sizeof(timeBuf), "[%Y-%m-%dT%H:%M:%SZ] ", gmtime(&t));
        
    std::string entry = std::string(timeBuf) + std::string(message) + "\n";
        
    // Print to Console
    printk("%s", entry.c_str());

    // Store in Ring Buffer
    std::lock_guard<std::mutex> lock(mtx);
    for (char c : entry) {
      buffer[head] = c;
      head = (head + 1) % buffer.size();
      if (head == 0) full = true;
    }
  }

  // Dump buffer to a callback (e.g., for Shell or Web)
  template<typename Func>
  void dump(Func consumer) {
    std::lock_guard<std::mutex> lock(mtx);
    size_t start = full ? head : 0;
    size_t count = full ? buffer.size() : head;

    for (size_t i = 0; i < count; ++i) {
      consumer(buffer[(start + i) % buffer.size()]);
    }
  }

private:
  std::vector<char> buffer;
  size_t head = 0;
  bool full = false;
  std::mutex mtx;
};
