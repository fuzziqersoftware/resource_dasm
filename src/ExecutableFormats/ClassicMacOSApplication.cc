#include "ClassicMacOSApplication.hh"

#include "../LowMemoryGlobals.hh"
#include "ELFFile.hh"

namespace ResourceDASM {

std::string elf_for_classic_mac_os_application(ResourceFile& rf) {
  auto code0_res = rf.get_resource(RESOURCE_TYPE_CODE, static_cast<int16_t>(0));
  auto code0_dec = rf.decode_CODE_0(code0_res);
  if (code0_res->data.size() + 0x10 > code0_dec.above_a5_size) {
    throw std::runtime_error("CODE 0 does not fit in space above A5");
  }

  // General strategy: run the application until it performs any syscall except the few implemented below. This
  // allows us to handle arbitrary methods of initializing the A5 world, at the cost of it possibly executing too
  // much or too little code and producing an incorrectly-initialized application.

  auto mem = std::make_shared<MemoryContext>();
  static constexpr uint32_t a5 = 0x80000000;
  static constexpr uint32_t code_region_alignment = 0x0000FFFF;
  uint32_t stack_size = 0x4000 + (4 - ((code0_dec.below_a5_size + code0_dec.above_a5_size) & 3));
  uint32_t stack_end = a5 - code0_dec.below_a5_size - stack_size;

  // Allocate low-memory globals (but not anything in the 00000000-000000FF range; we want null pointer dereferences
  // to cause errors)
  mem->allocate_at(0x00000100, sizeof(LowMemoryGlobals) - 0x100);
  mem->write_u32b(0x00000904, a5); // Set CurrentA5
  mem->write_u32b(0x00000908, a5 - code0_dec.below_a5_size); // Set CurStackBase

  // Allocate the A5 world and put CODE 0 into place
  mem->allocate_at(stack_end, stack_size + code0_dec.below_a5_size + 0x10 + code0_dec.above_a5_size);
  mem->memcpy(a5 + 0x10, code0_res->data.data(), code0_res->data.size());

  auto resource_key = [](uint32_t res_type, int16_t res_id) -> uint64_t {
    return (static_cast<uint64_t>(res_type) << 16) | (static_cast<uint64_t>(res_id) & 0xFFFF);
  };
  struct CodeResource {
    std::shared_ptr<const ResourceFile::Resource> res;
    ResourceFile::DecodedCodeResource decoded;
    uint32_t base_addr;
    uint32_t header_size;
  };
  std::map<int16_t, CodeResource> codes;
  std::unordered_map<uint64_t, uint32_t> loaded_resources;
  uint32_t next_code_addr = 0xA0000000;
  for (int16_t res_id : rf.all_resources_of_type(RESOURCE_TYPE_CODE)) {
    if (res_id != 0) {
      auto res = rf.get_resource(RESOURCE_TYPE_CODE, res_id);
      auto decoded = rf.decode_CODE(res, true);
      uint32_t header_size = (decoded.first_jump_table_entry_index >= 0)
          ? sizeof(CodeResourceHeader)
          : sizeof(CodeResourceFarHeader);
      const auto& code = codes.emplace(res_id, CodeResource{res, std::move(decoded), next_code_addr, header_size}).first->second;
      next_code_addr = (next_code_addr + res->data.size() + code_region_alignment) & (~code_region_alignment);
      loaded_resources.emplace(resource_key(RESOURCE_TYPE_CODE, res_id), code.base_addr);
      mem->allocate_at(code.base_addr, res->data.size());
      mem->memcpy(code.base_addr, res->data.data(), res->data.size());
    }
  }

  // Update the jump table since all segments are loaded (keeping the LoadSeg traps would just be confusing later)
  auto update_jump_table = [&]() -> void {
    for (uint32_t addr = a5 + code0_dec.jump_table_a5_offset; addr < a5 + code0_dec.above_a5_size; addr += 8) {
      // Expect the jump table entry to be like XXXX 3F3C YYYY A9F0, where X = offset into code and Y = CODE res id
      uint64_t entry = mem->read_u64b(addr);
      if ((entry & 0x0000FFFF00000000ULL) == 0x00004EF900000000) { // Already loaded
        continue;
      } else if ((entry & 0x0000FFFF0000FFFFULL) != 0x00003F3C0000A9F0ULL) {
        phosg::fwrite_fmt(stderr, "warning: jump table entry at {:08X} ({:016X}) is not valid\n", addr, entry);
        continue;
      }
      int16_t code_res_id = (entry >> 16) & 0xFFFF;
      uint16_t offset_after_header = (entry >> 48) & 0xFFFF;

      auto code_it = codes.find(code_res_id);
      if (code_it == codes.end()) {
        phosg::fwrite_fmt(stderr, "warning: jump table entry at {:08X} ({:016X}) refers to missing CODE resource {}\n",
            addr, entry, code_res_id);
        continue;
      }
      const auto& code = code_it->second;
      uint32_t target = code.base_addr + code.header_size + offset_after_header;
      mem->write_u64b(addr, 0x00004EF900000000ULL | (static_cast<uint64_t>(code_res_id) << 48) | target);
    }
  };
  update_jump_table();

  // Start executing at the first jump table entry
  M68KEmulator emu(mem);
  auto& regs = emu.registers();
  regs.a[5] = a5;
  regs.a[7] = a5 - code0_dec.below_a5_size;
  regs.pc = a5 + code0_dec.jump_table_a5_offset + 2;

  // Uncomment for debugging
  // auto debugger = std::make_shared<EmulatorDebugger<M68KEmulator>>();
  // debugger->bind(emu);
  // debugger->state.mode = DebuggerMode::TRACE;

  std::unordered_map<uint32_t, uint64_t> handle_to_resource_key;
  emu.set_syscall_handler([&](M68KEmulator& emu, uint16_t opcode) -> void {
    auto& regs = emu.registers();
    switch (opcode) {
      case 0xA029: // HLock
      case 0xA02A: // HUnlock
      case 0xA040: // ReserveMem
        // Nothing to do here; virtual memory makes ReserveMem obsolete
        break;

      case 0xA9A0: { // GetResource(ResType theType @ [A7 + 2], short theID @ [A7]) -> Handle @ [A7]
        uint32_t res_type = mem->read_u32b(regs.a[7] + 2);
        int16_t res_id = mem->read_s16b(regs.a[7]);
        regs.a[7] += 6;

        uint32_t res_addr;
        uint64_t res_key = resource_key(res_type, res_id);
        if (auto it = loaded_resources.find(res_key); it != loaded_resources.end()) {
          res_addr = it->second;
        } else {
          auto res = rf.get_resource(res_type, res_id);
          // It seems some implementations read beyond the end of the resource! We put a few zero bytes there to ensure
          // reasonable behavior.
          res_addr = mem->allocate(res->data.size() + 0x10);
          mem->memcpy(res_addr, res->data.data(), res->data.size());
          mem->memset(res_addr + res->data.size(), 0, 0x10);
          loaded_resources.emplace(res_key, res_addr);
        }

        uint32_t handle_value = mem->allocate(4);
        mem->write_u32b(handle_value, res_addr);
        mem->write_u32b(regs.a[7], handle_value);
        handle_to_resource_key.emplace(handle_value, res_key);
        break;
      }

      case 0xA992: // DetachResource(Handle theResource @ [A7]) -> void
      case 0xA9A2: // LoadResource(Handle theResource @ [A7]) -> void
      case 0xA9A3: // ReleaseResource(Handle theResource @ [A7]) -> void
        // All resources are already loaded, and we never release resources since we're just emulating the beginning of
        // application initialization, so we ignore these syscalls
        regs.a[7] += 4;
        break;

      case 0xA9A5: { // GetResourceSizeOnDisk(Handle theResource @ [A7]) -> long @ [A7]
        uint64_t res_key = handle_to_resource_key.at(mem->read_u32b(emu.registers().a[7]));
        auto res = rf.get_resource((res_key >> 16) & 0xFFFFFFFF, static_cast<int16_t>(res_key & 0xFFFF));
        regs.a[7] += 4;
        mem->write_u32b(emu.registers().a[7], res->data.size());
        break;
      }

      case 0xA025: { // GetHandleSize(Handle h @ A0) -> Size @ D0
        uint64_t res_key = handle_to_resource_key.at(emu.registers().a[0]);
        auto res = rf.get_resource((res_key >> 16) & 0xFFFFFFFF, static_cast<int16_t>(res_key & 0xFFFF));
        emu.registers().d[0].u = res->data.size();
        break;
      }

      case 0xA9A6: // GetResAttrs(Handle theResource @ [A7]) -> short @ [A7]
        // No attributes are relevant in this environment; just return 0
        emu.registers().a[7] += 4;
        mem->write_u16b(emu.registers().a[7], 0);
        break;

      default:
        emu.exit_all();
    }
  });

  emu.execute();

  // Some frameworks compress the jump table too, so there may be newly-valid entries that need postprocessing
  update_jump_table();

  // Export the memory contents as an ELF file
  ELFFile::SerializeInput inp;
  inp.is_be = true;
  inp.is_64 = false;
  inp.type = 2; // Executable
  inp.architecture = 0x04; // 68k
  inp.entrypoint_addr = a5 + code0_dec.jump_table_a5_offset + 2; // First jump table entry
  inp.segment_file_alignment_bits = 5; // 0x20

  auto& below_a5_seg = inp.segments.emplace_back(); // Everything until the start of the jump table (A5 + 0x10)
  below_a5_seg.section_flags = 0x00000003; // Writable, allocated
  below_a5_seg.program_flags = 0x00000006; // Read/write, not executable
  below_a5_seg.virtual_addr = 0x80000000UL - code0_dec.below_a5_size;
  below_a5_seg.physical_addr = below_a5_seg.virtual_addr;
  below_a5_seg.loaded_size = code0_dec.below_a5_size + 0x10;
  below_a5_seg.name = ".globals";
  below_a5_seg.data = mem->read(a5 - code0_dec.below_a5_size, code0_dec.below_a5_size + 0x10);

  auto& above_a5_seg = inp.segments.emplace_back(); // Jump table
  above_a5_seg.section_flags = 0x00000002; // Allocated, not writable
  above_a5_seg.program_flags = 0x00000005; // Read/execute, no write
  above_a5_seg.virtual_addr = 0x80000010;
  above_a5_seg.physical_addr = above_a5_seg.virtual_addr;
  above_a5_seg.loaded_size = code0_dec.above_a5_size;
  above_a5_seg.name = ".jumptable";
  above_a5_seg.data = mem->read(a5 + 0x10, code0_dec.above_a5_size);

  // Add all the code
  for (const auto& [res_id, code] : codes) {
    auto& seg = inp.segments.emplace_back();
    seg.section_flags = 0x00000002; // Allocated, not writable
    seg.program_flags = 0x00000005; // Read/execute, no write
    seg.virtual_addr = code.base_addr;
    seg.physical_addr = code.base_addr;
    seg.loaded_size = code.res->data.size();
    seg.name = std::format(".code{}", res_id);
    seg.data = mem->read(code.base_addr, code.res->data.size());
  }

  return ELFFile::serialize(inp);
}

} // namespace ResourceDASM
