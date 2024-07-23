#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "EmulatorBase.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {

EmulatorBase::EmulatorBase(shared_ptr<MemoryContext> mem)
    : mem(mem),
      instructions_executed(0),
      log_memory_access(false) {}

void EmulatorBase::set_behavior_by_name(const string&) {
  throw logic_error("this CPU engine does not implement multiple behaviors");
}

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

void EmulatorBase::set_time_base(uint64_t) {
  throw logic_error("this CPU engine does not implement a time base");
}
void EmulatorBase::set_time_base(const vector<uint64_t>&) {
  throw logic_error("this CPU engine does not implement a time base");
}

EmulatorDebuggerState::EmulatorDebuggerState()
    : max_cycles(0),
      mode(DebuggerMode::NONE),
      trace_period(0x100),
      print_state_headers(true),
      print_memory_accesses(true) {}

} // namespace ResourceDASM
