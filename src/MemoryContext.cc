#include "MemoryContext.hh"

#include <inttypes.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>

using namespace std;


MemoryContext::MemoryContext()
  : page_size(sysconf(_SC_PAGESIZE)),
    allocated_bytes(0),
    free_bytes(0) {

  if (this->page_size == 0) {
    throw invalid_argument("system page size is zero");
  }
  if (this->page_size & (this->page_size - 1)) {
    throw invalid_argument("system page size is not a power of 2");
  }

  {
    this->page_bits = 0;
    size_t s = this->page_size;
    while (s >>= 1) {
      this->page_bits++;
    }
    if (!this->page_bits) {
      throw invalid_argument("system page bits is zero");
    }
  }

  this->total_pages = (0x100000000 >> this->page_bits) - 1;
  this->arena_for_page_number.clear();
  this->arena_for_page_number.resize(total_pages, nullptr);
}

uint32_t MemoryContext::allocate(size_t requested_size) {
  return this->allocate_within(0, 0xFFFFFFFF, requested_size);
}

uint32_t MemoryContext::allocate_within(
    uint32_t addr_low, uint32_t addr_high, size_t requested_size) {
  // Round requested_size up to a multiple of 0x10, just for convenience. (I
  // didn't do my homework on this, but blocks almost certainly need to be
  // 2-byte aligned for 68K apps and 4-byte aligned for PPC apps on actual
  // Mac hardware. Our emulators don't have that limitation, but for debugging
  // purposes, it's nice to have every block start on a 16-byte boundary.)
  requested_size = (requested_size + 0xF) & (~0xF);

  // Find the arena with the smallest amount of free space that can accept this
  // block. Only look in arenas that are completely within the requested range.
  // TODO: make this not linear time in the arena count somehow
  uint32_t block_addr = 0;
  shared_ptr<Arena> arena = nullptr;
  {
    size_t smallest_block = 0xFFFFFFFF;
    for (auto arena_it = this->arenas_by_addr.lower_bound(addr_low);
         (arena_it != this->arenas_by_addr.end()) &&
           (arena_it->first + arena_it->second->size < addr_high);
         arena_it++) {
      auto& a = arena_it->second;
      auto block_it = a->free_blocks_by_size.lower_bound(requested_size);
      if ((block_it != a->free_blocks_by_size.end()) &&
          (block_it->first < smallest_block)) {
        arena = a;
        block_addr = block_it->second;
      }
    }
  }

  // If no such block was found, create a new arena with enough space.
  if (block_addr == 0) {
    arena = this->create_arena(
        this->find_arena_space(addr_low, addr_high, requested_size), requested_size);
    block_addr = arena->addr;
  }

  // Split or replace the arena's free block appropriately
  arena->split_free_block(block_addr, block_addr, requested_size);

  // Update stats
  this->free_bytes -= requested_size;
  this->allocated_bytes += requested_size;

  return block_addr;
}

void MemoryContext::allocate_at(uint32_t addr, size_t requested_size) {
  // Round requested_size up to a multiple of 0x10, as in allocate(). Here, we
  // also need to ensure that addr is aligned properly.
  if (addr & 0xF) {
    throw invalid_argument("blocks can only be allocated on 16-byte boundaries");
  }
  requested_size = (requested_size + 0xF) & (~0xF);

  // Find the arena that this block would fit into. All spanned pages must be
  // part of the same arena. (There is no technical reason why this must be the
  // case, but the bookkeeping would be quite a bit harder if we allowed this,
  // and allocate_at should generally only be called on a new MemoryContext
  // before any dynamic blocks are allocated.)
  uint32_t start_page_number = this->page_number_for_addr(addr);
  uint32_t end_page_num = this->page_number_for_addr(addr + requested_size - 1);
  shared_ptr<Arena> arena = this->arena_for_page_number.at(start_page_number);
  for (uint64_t page_num = start_page_number + 1; page_num <= end_page_num; page_num++) {
    if (this->arena_for_page_number.at(page_num) != arena) {
      throw runtime_error("fixed-address allocation request spans multiple arenas");
    }
  }

  // If no arena exists already, make a new one with enough space. If an arena
  // does already exist, we need to ensure that the requested allocation fits
  // entirely within an existing free block.
  uint32_t free_block_addr = 0;
  if (!arena.get()) {
    uint32_t arena_addr = this->page_base_for_addr(addr);
    arena = this->create_arena(arena_addr, requested_size + (addr - arena_addr));
    free_block_addr = arena->addr;
  } else {
    auto it = arena->free_blocks_by_addr.upper_bound(addr);
    if (it == arena->free_blocks_by_addr.begin()) {
      throw runtime_error("arena contains no free blocks");
    }
    it--;
    if (it->first > addr) {
      throw logic_error("preceding free block is not before the requested address");
    }
    if (it->first + it->second < addr + requested_size) {
      throw runtime_error("not enough space in preceding free block");
    }
    free_block_addr = it->first;
  }

  // Split or replace the arena's free block appropriately
  arena->split_free_block(free_block_addr, addr, requested_size);

  // Update stats
  this->free_bytes -= requested_size;
  this->allocated_bytes += requested_size;
}

MemoryContext::Arena::Arena(uint32_t addr, size_t size)
  : addr(addr),
    host_addr(mmap(
      nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)),
    size(size),
    allocated_bytes(0),
    free_bytes(size) {
  if (this->host_addr == MAP_FAILED) {
    this->host_addr = nullptr;
    throw runtime_error("cannot mmap arena");
  }
  this->free_blocks_by_addr.emplace(addr, size);
  this->free_blocks_by_size.emplace(size, addr);
}

MemoryContext::Arena::~Arena() {
  munmap(this->host_addr, this->size);
}

void MemoryContext::Arena::delete_free_block(uint32_t addr, uint32_t size) {
  this->free_blocks_by_addr.erase(addr);
  for (auto its = this->free_blocks_by_size.equal_range(size);
       its.first != its.second;) {
    if (its.first->second == addr) {
      its.first = this->free_blocks_by_size.erase(its.first);
    } else {
      its.first++;
    }
  }
}

void MemoryContext::Arena::split_free_block(
    uint32_t free_block_addr,
    uint32_t allocate_block_addr,
    uint32_t allocate_size) {

  size_t free_block_size = this->free_blocks_by_addr.at(free_block_addr);
  size_t new_free_bytes_before = allocate_block_addr - free_block_addr;
  size_t new_free_bytes_after = (free_block_addr + free_block_size)
      - (allocate_block_addr + allocate_size);

  // If any of the sizes overflowed, then the allocated block doesn't fit in the
  // free block
  if (new_free_bytes_before > free_block_size) {
    throw runtime_error("cannot split free block: allocated address too low");
  }
  if (new_free_bytes_after > free_block_size) {
    throw runtime_error("cannot split free block: allocated address or size too high");
  }
  if (new_free_bytes_before + allocate_size + new_free_bytes_after != free_block_size) {
    throw logic_error("sizes do not add up correctly after splitting free block");
  }

  // Delete the existing free block
  this->delete_free_block(free_block_addr, free_block_size);

  // Create an allocated block (and free blocks, if there's extra space) in the
  // now-unrepresented space.
  this->allocated_blocks.emplace(allocate_block_addr, allocate_size);

  if (new_free_bytes_before > 0) {
    this->free_blocks_by_addr.emplace(free_block_addr, new_free_bytes_before);
    this->free_blocks_by_size.emplace(new_free_bytes_before, free_block_addr);
  }
  if (new_free_bytes_after > 0) {
    uint32_t new_free_block_addr = allocate_block_addr + allocate_size;
    this->free_blocks_by_addr.emplace(new_free_block_addr, new_free_bytes_after);
    this->free_blocks_by_size.emplace(new_free_bytes_after, new_free_block_addr);
  }

  // Update stats
  this->free_bytes -= allocate_size;
  this->allocated_bytes += allocate_size;
}

uint32_t MemoryContext::find_arena_space(
    uint32_t addr_low, uint32_t addr_high, uint32_t size) const {
  size_t page_count = this->page_count_for_size(size);

  // TODO: Make this not be linear-time by adding some kind of index
  size_t start_page_num = this->page_number_for_addr(addr_low);
  size_t end_page_num = this->page_number_for_addr(addr_high - 1);
  for (size_t z = start_page_num; z < end_page_num; z++) {
    if (this->arena_for_page_number[z].get()) {
      start_page_num = z + 1;
    } else if (z - start_page_num >= page_count - 1) {
      break;
    }
  }
  return (start_page_num << this->page_bits);
}

shared_ptr<MemoryContext::Arena> MemoryContext::create_arena(
    uint32_t addr, size_t size) {
  // Round size up to a host page boundary
  size = this->page_size_for_size(size);

  // Make sure the relevant space in the arenas list is all blank
  size_t end_page_num = this->page_number_for_addr(addr + size - 1);
  for (size_t z = this->page_number_for_addr(addr); z <= end_page_num; z++) {
    if (this->arena_for_page_number[z].get()) {
      throw runtime_error("fixed-address arena overlaps existing arena");
    }
  }

  // Create the arena and add it to the arenas list
  shared_ptr<Arena> arena(new Arena(addr, size));
  this->arenas_by_addr.emplace(addr, arena);
  for (uint32_t z = this->page_number_for_addr(arena->addr); z <= end_page_num; z++) {
    this->arena_for_page_number[z] = arena;
  }

  // Update stats
  this->free_bytes += arena->free_bytes;
  this->allocated_bytes += arena->allocated_bytes;
  this->size += arena->size;

  return arena;
}

void MemoryContext::delete_arena(shared_ptr<Arena> arena) {
  // Remove the arena from the arenas set
  if (!this->arenas_by_addr.erase(arena->addr)) {
    throw logic_error("attempting to delete unregistered arena");
  }

  // Clear the arena from the page pointers list
  size_t end_page_num = this->page_number_for_addr(arena->addr + arena->size - 1);
  for (size_t z = this->page_number_for_addr(arena->addr); z <= end_page_num; z++) {
    if (this->arena_for_page_number[z] != arena) {
      throw logic_error("arena did not have all valid page pointers at deletion time");
    }
    this->arena_for_page_number[z].reset();
  }

  // Update stats. Note that allocated_bytes may not be zero since free() has a
  // shortcut where it doesn't update structs/stats if the arena is about to be
  // deleted anyway.
  this->size -= arena->size;
  this->allocated_bytes -= arena->allocated_bytes;
  this->free_bytes -= arena->free_bytes;
}

void MemoryContext::free(uint32_t addr) {
  // Find the arena that this region is within
  auto arena = this->arena_for_page_number.at(this->page_number_for_addr(addr));
  if (!arena.get()) {
    throw invalid_argument("freed region is not part of any arena");
  }

  // Find the allocated block
  auto allocated_block_it = arena->allocated_blocks.find(addr);
  if (allocated_block_it == arena->allocated_blocks.end()) {
    throw invalid_argument("pointer being freed is not allocated");
  }

  // Delete the allocated block. If there are no allocated blocks remaining in
  // the arena, don't bother cleaning up the free maps and instead delete the
  // entire arena.
  size_t size = allocated_block_it->second;
  arena->allocated_blocks.erase(allocated_block_it);
  if (arena->allocated_blocks.empty()) {
    // Note: delete_arena will correctly update the stats for us; no need to do
    // it manually here.
    this->delete_arena(arena);

  } else {
    // Find the free block after the allocated block. Note that this may be
    // end() if there is another allocated block immediately following, or if
    // the allocated block ends exactly at the arena's boundary.
    auto after_free_block_it = arena->free_blocks_by_addr.find(addr + size);

    // Find the free block before the allocated block. If the after iterator
    // points to the first free block, then there is no existing free block
    // before the allocated block; we'll represent this with end().
    auto before_free_block_it = after_free_block_it;
    if (before_free_block_it != arena->free_blocks_by_addr.begin()) {
      before_free_block_it--;
    } else {
      before_free_block_it = arena->free_blocks_by_addr.end();
    }
    if (before_free_block_it->first >= addr) {
      throw logic_error("before free block is not actually before allocated address");
    }
    if (before_free_block_it->first + before_free_block_it->second != addr) {
      throw logic_error("unrepresented space before allocated block");
    }

    // Figure out the address and size for the new free block (we have to do this
    // before the iterators become invalid below)
    uint32_t new_free_block_addr =
        (before_free_block_it == arena->free_blocks_by_addr.end())
        ? addr
        : before_free_block_it->first;
    uint32_t new_free_block_end_addr =
        (after_free_block_it == arena->free_blocks_by_addr.end())
        ? (addr + size)
        : (after_free_block_it->first + after_free_block_it->second);
    size_t new_free_block_size = new_free_block_end_addr - new_free_block_addr;

    // Delete both free blocks
    if (before_free_block_it != arena->free_blocks_by_addr.end()) {
      arena->delete_free_block(before_free_block_it->first, before_free_block_it->second);
    }
    if (after_free_block_it != arena->free_blocks_by_addr.end()) {
      arena->delete_free_block(after_free_block_it->first, after_free_block_it->second);
    }

    // Create a new free block spanning all the just-deleted free blocks and
    // allocated block
    arena->free_blocks_by_addr.emplace(new_free_block_addr, new_free_block_size);
    arena->free_blocks_by_size.emplace(new_free_block_size, new_free_block_addr);

    // Update stats
    arena->free_bytes += size;
    arena->allocated_bytes -= size;
    this->free_bytes += size;
    this->allocated_bytes -= size;
  }
}

void MemoryContext::set_symbol_addr(const char* name, uint32_t addr) {
  if (!this->symbol_addrs.emplace(name, addr).second) {
    throw runtime_error("cannot redefine symbol");
  }
}

uint32_t MemoryContext::get_symbol_addr(const char* name) const {
  return this->symbol_addrs.at(name);
}

const unordered_map<string, uint32_t> MemoryContext::all_symbols() const {
  return this->symbol_addrs;
}

size_t MemoryContext::get_block_size(uint32_t addr) const {
  auto arena = this->arena_for_page_number.at(this->page_number_for_addr(addr));
  if (!arena.get()) {
    return 0;
  }
  try {
    return arena->allocated_blocks.at(addr);
  } catch (const out_of_range&) {
    return 0;
  }
}

size_t MemoryContext::get_page_size() const {
  return this->page_size;
}

void MemoryContext::print_state(FILE* stream) const {
  fprintf(stream, "[MemoryContext page_bits=%hhu page_size=0x%zX total_pages=0x%zX size=0x%zX allocated_bytes=0x%zX free_bytes=0x%zX arenas_by_addr=[",
      this->page_bits,
      this->page_size,
      this->total_pages,
      this->size,
      this->allocated_bytes,
      this->free_bytes);

  for (const auto& it : this->arenas_by_addr) {
    const auto& arena = it.second;
    fprintf(stream, "0x%08" PRIX32 "=[Arena addr=0x%08" PRIX32 " host_addr=%p size=0x%zX allocated_bytes=0x%zX free_bytes=0x%zX allocated_blocks=[",
        it.first,
        arena->addr,
        arena->host_addr,
        arena->size,
        arena->allocated_bytes,
        arena->free_bytes);
    for (const auto& it : arena->allocated_blocks) {
      fprintf(stream, "(0x%08" PRIX32 ",0x%" PRIX32 "),", it.first, it.second);
    }
    fprintf(stream, "] free_blocks_by_addr=[");
    for (const auto& it : arena->free_blocks_by_addr) {
      fprintf(stream, "(0x%08" PRIX32 ",0x%" PRIX32 "),", it.first, it.second);
    }
    fprintf(stream, "] free_blocks_by_size=[");
    for (const auto& it : arena->free_blocks_by_size) {
      fprintf(stream, "(0x%" PRIX32 ",0x%08" PRIX32 "),", it.first, it.second);
    }
    fprintf(stream, "]], ");
  }
  fprintf(stream, "arena_for_page_number=[");
  for (size_t z = 0; z < this->total_pages; z++) {
    const auto& arena = this->arena_for_page_number[z];
    if (arena.get()) {
      fprintf(stream, "[%zX]=%08" PRIX32 ", ", z, arena->addr);
    }
  }
  fprintf(stream, "]\n");
}

void MemoryContext::print_contents(FILE* stream) const {
  for (const auto& arena_it : this->arenas_by_addr) {
    for (const auto& block_it : arena_it.second->allocated_blocks) {
      print_data(stream, this->at(block_it.first, block_it.second),
          block_it.second, block_it.first);
    }
  }
}

void MemoryContext::import_state(FILE* stream) {
  // Delete everything before importing new state
  while (!this->arenas_by_addr.empty()) {
    this->delete_arena(this->arenas_by_addr.begin()->second);
  }

  uint8_t version;
  freadx(stream, &version, sizeof(version));
  if (version != 0) {
    throw runtime_error("unknown format version");
  }

  uint64_t region_count;
  freadx(stream, &region_count, sizeof(region_count));
  for (size_t x = 0; x < region_count; x++) {
    uint32_t addr, size;
    freadx(stream, &addr, sizeof(addr));
    freadx(stream, &size, sizeof(size));
    this->allocate_at(addr, size);
    freadx(stream, this->at(addr, size), size);
  }
}

void MemoryContext::export_state(FILE* stream) const {
  uint8_t version = 0;
  fwritex(stream, &version, sizeof(version));

  map<uint32_t, uint32_t> regions_to_export;
  for (const auto& arena_it : this->arenas_by_addr) {
    for (const auto& block_it : arena_it.second->allocated_blocks) {
      regions_to_export.emplace(block_it.first, block_it.second);
    }
  }

  uint64_t region_count = regions_to_export.size();
  fwritex(stream, &region_count, sizeof(region_count));
  for (const auto& region_it : regions_to_export) {
    uint32_t addr = region_it.first;
    uint32_t size = region_it.second;
    fwritex(stream, &addr, sizeof(addr));
    fwritex(stream, &size, sizeof(size));
    fwritex(stream, this->at(addr, size), size);
  }
}
