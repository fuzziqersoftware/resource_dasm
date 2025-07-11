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

void EmulatorBase::set_time_base(uint64_t) {
  throw logic_error("this CPU engine does not implement a time base");
}
void EmulatorBase::set_time_base(const vector<uint64_t>&) {
  throw logic_error("this CPU engine does not implement a time base");
}

} // namespace ResourceDASM
