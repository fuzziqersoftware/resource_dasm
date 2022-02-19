#pragma once

#include <stdint.h>

#include <memory>
#include <unordered_map>



struct TrapInfo {
  const char* name;
  std::unordered_map<uint8_t, std::shared_ptr<TrapInfo>> flag_overrides;
  std::unordered_map<uint32_t, std::shared_ptr<TrapInfo>> subtrap_info;
  uint32_t proc_selector_mask;

  TrapInfo(const char* name);
  TrapInfo(const char* name,
      std::unordered_map<uint8_t, TrapInfo>&& flag_overrides,
      std::unordered_map<uint32_t, TrapInfo>&& subtrap_info,
      uint32_t proc_selector_mask = 0xFFFFFFFF);
};

const TrapInfo* info_for_68k_trap(uint16_t trap_num, uint8_t flags = 0);
