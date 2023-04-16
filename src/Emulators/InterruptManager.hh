#pragma once

#include <stdint.h>

#include <functional>
#include <memory>
#include <string>

class InterruptManager {
public:
  InterruptManager();
  ~InterruptManager() = default;

  struct PendingCall {
    std::shared_ptr<PendingCall> next;
    uint64_t at_cycle_count;
    bool canceled;
    bool completed;
    std::function<void()> fn;

    inline void cancel() {
      this->canceled = true;
    }
  };

  std::shared_ptr<PendingCall> add(uint64_t cycle_count, std::function<bool()> fn);

  void on_cycle_start();

  uint64_t cycles() const;

protected:
  uint64_t cycle_count;
  std::shared_ptr<PendingCall> head;
};
