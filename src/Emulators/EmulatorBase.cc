#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "EmulatorBase.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {

void EmulatorBase::DisassembleResult::import_labels(
    const std::multimap<uint32_t, std::string>& labels, const std::map<uint32_t, LabelRefs>* branch_refs) {
  for (const auto& [addr, name] : labels) {
    this->labels.emplace(addr, DisassembleResult::Label{.address = addr, .name = name, .refs = {}});
  }
  if (branch_refs) {
    for (const auto& [addr, refs] : *branch_refs) {
      auto [label_it, label_end_it] = this->labels.equal_range(addr);
      if (label_it == label_end_it) {
        std::string name = std::format("{}{:08X}", refs.call_addrs.empty() ? "label" : "fn", addr);
        auto& label = this->labels.emplace(addr, DisassembleResult::Label{})->second;
        label.address = addr;
        label.name = std::move(name);
        label.refs = refs;
      } else {
        for (; label_it != label_end_it; label_it++) {
          label_it->second.refs = refs;
        }
      }
    }
  }
}

EmulatorBase::EmulatorBase(shared_ptr<MemoryContext> mem)
    : mem(mem), instructions_executed(0), log_memory_access(false) {}

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

std::string EmulatorBase::format_label(uint32_t pc, uint32_t label_addr, const LabelRefs& refs) {
  std::string comment_str;
  if (label_addr != pc) {
    comment_str = "Misaligned";
  }
  if (!refs.branch_addrs.empty() || !refs.call_addrs.empty()) {
    if (comment_str.empty()) {
      comment_str = "Referenced by ";
    } else {
      comment_str = "; referenced by ";
    }
    size_t comment_start_size = comment_str.size();
    for (uint32_t src_addr : refs.call_addrs) {
      if (comment_str.size() > comment_start_size) {
        comment_str += ", ";
      }
      comment_str += std::format("call at {:08X}", src_addr);
    }
    for (uint32_t src_addr : refs.branch_addrs) {
      if (comment_str.size() > comment_start_size) {
        comment_str += ", ";
      }
      comment_str += std::format("branch at {:08X}", src_addr);
    }
  }

  const char* label_type = refs.call_addrs.empty() ? "label" : "fn";
  if (comment_str.empty()) {
    return std::format("{}{:08X}:\n", label_type, label_addr);
  } else {
    return std::format("{}{:08X}: // {}\n", label_type, label_addr, comment_str);
  }
}

} // namespace ResourceDASM
