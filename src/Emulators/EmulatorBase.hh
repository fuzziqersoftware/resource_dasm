#pragma once

#include <stdio.h>
#include <stdint.h>

#include <vector>
#include <stdexcept>

#include "MemoryContext.hh"



template <typename T>
const T bits_for_type = sizeof(T) << 3;

template <typename T>
const T msb_for_type = (1 << (bits_for_type<T> - 1));



class EmulatorBase {
public:
  explicit EmulatorBase(std::shared_ptr<MemoryContext> mem);
  virtual ~EmulatorBase() = default;

  virtual void import_state(FILE* stream) = 0;
  virtual void export_state(FILE* stream) const = 0;

  inline std::shared_ptr<MemoryContext> memory() {
    return this->mem;
  }

  inline uint64_t cycles() const {
    return this->instructions_executed;
  }

  virtual void print_state_header(FILE* stream) = 0;
  virtual void print_state(FILE* stream) = 0;

  // The syscall handler or debug hook can throw this to terminate emulation
  // cleanly (and cause .execute() to return). Throwing any other type of
  // exception will cause emulation to terminate uncleanly and the exception
  // will propagate out of .execute().
  class terminate_emulation : public std::runtime_error {
  public:
    terminate_emulation() : runtime_error("terminate emulation") { }
    ~terminate_emulation() = default;
  };

  inline void set_log_memory_access(bool log_memory_access) {
    this->log_memory_access = log_memory_access;
    if (!this->log_memory_access) {
      this->memory_access_log.clear();
    }
  }

  struct MemoryAccess {
    uint32_t addr;
    uint8_t size;
    bool is_write;
  };

  std::vector<MemoryAccess> get_and_clear_memory_access_log();

  virtual void execute() = 0;

protected:
  std::shared_ptr<MemoryContext> mem;
  uint64_t instructions_executed;

  bool log_memory_access;
  std::vector<MemoryAccess> memory_access_log;

  void report_mem_access(uint32_t addr, uint8_t size, bool is_write);
};
