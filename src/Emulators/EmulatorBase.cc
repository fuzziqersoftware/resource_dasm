#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <vector>

#include "EmulatorBase.hh"

using namespace std;



EmulatorBase::EmulatorBase(shared_ptr<MemoryContext> mem)
  : mem(mem), instructions_executed(0), log_memory_access(false) { }

vector<EmulatorBase::MemoryAccess> EmulatorBase::get_and_clear_memory_access_log() {
  vector<EmulatorBase::MemoryAccess> ret;
  ret.swap(this->memory_access_log);
  return ret;
}

void EmulatorBase::report_mem_access(uint32_t addr, uint8_t size, bool is_write) {
  if (this->log_memory_access) {
    this->memory_access_log.push_back({addr, size, is_write});
  }
}
