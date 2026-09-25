#include "MemoryContext.hh"

#include <phosg/Platform.hh>

#include <inttypes.h>
#include <stdint.h>
#include <unistd.h>
#ifndef PHOSG_WINDOWS
#include <sys/mman.h>
#else
#include <windows.h>
#endif

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <set>

namespace ResourceDASM {

#ifdef ENABLE_MEMORY_CONTEXT_DEBUG
constexpr bool verify_operations = true;
#else
constexpr bool verify_operations = false;
#endif

static void* reserve_mem(size_t size) {
#ifdef PHOSG_WINDOWS
  void* ret = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
  if (ret == nullptr) {
    throw std::runtime_error("Cannot reserve context address space");
  }
#else
  void* ret = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ret == MAP_FAILED) {
    throw std::runtime_error("Cannot reserve context address space");
  }
#endif
  return ret;
}

static void unreserve_mem(void* base, size_t size) {
#ifdef PHOSG_WINDOWS
  if (!VirtualFree(base, 0, MEM_RELEASE)) {
    throw std::runtime_error("Cannot unreserve context address space");
  }
#else
  if (munmap(base, size)) {
    throw std::runtime_error("Cannot unreserve context address space");
  }
#endif
}

static void alloc_reserved_mem(void* base, size_t size) {
#ifdef PHOSG_WINDOWS
  if (!VirtualAlloc(base, size, MEM_COMMIT, PAGE_READWRITE)) {
    throw std::runtime_error("Cannot commit reserved pages");
  }
#else
  if (mprotect(base, size, PROT_READ | PROT_WRITE)) {
    throw std::runtime_error("Cannot commit reserved pages");
  }
#endif
}

MemoryContext::MemoryContext(bool strict) : strict(strict), base(reserve_mem(this->TOTAL_SIZE)) {
  this->allocators.emplace(0x00000000, Allocator{0x00000000, this->TOTAL_SIZE});
  if constexpr (verify_operations) {
    this->verify();
  }
}

MemoryContext::MemoryContext(MemoryContext&& other)
    : strict(other.strict),
      base(other.base),
      pages_valid(std::move(other.pages_valid)),
      allocators(std::move(other.allocators)),
      symbol_addrs(std::move(other.symbol_addrs)),
      addr_symbols(std::move(other.addr_symbols)) {
  other.base = nullptr;
  if constexpr (verify_operations) {
    this->verify();
  }
}

MemoryContext& MemoryContext::operator=(MemoryContext&& other) {
  this->strict = other.strict;
  if (this->base) {
    unreserve_mem(this->base, this->TOTAL_SIZE);
  }
  this->base = other.base;
  other.base = nullptr;
  this->pages_valid = std::move(other.pages_valid);
  this->allocators = std::move(other.allocators);
  this->symbol_addrs = std::move(other.symbol_addrs);
  this->addr_symbols = std::move(other.addr_symbols);
  if constexpr (verify_operations) {
    this->verify();
  }
  return *this;
}

MemoryContext MemoryContext::duplicate() const {
  MemoryContext ret(this->strict);
  for (const auto& [_, allocator] : this->allocators) {
    auto [addr_low, addr_high] = allocator.range();
    if (addr_high != this->TOTAL_SIZE) {
      ret.split_allocators(addr_high);
    }
    auto& ret_alloc = ret.get_allocator(addr_low);
    for (const auto& [addr, block] : allocator.all_blocks()) {
      ret_alloc.allocate_at(addr, block.requested_size);
      ret.memcpy(addr, this->at(addr, block.requested_size), block.requested_size);
    }
  }
  ret.symbol_addrs = this->symbol_addrs;
  ret.addr_symbols = this->addr_symbols;
  if (verify_operations) {
    this->verify();
  }
  return ret;
}

MemoryContext::Allocator::Allocator(uint32_t addr_low, uint64_t addr_high) : addr_low{addr_low}, addr_high{addr_high} {
  this->reconstruct_free_maps();
  if constexpr (verify_operations) {
    this->verify();
  }
}

MemoryContext::Allocator MemoryContext::Allocator::split(uint32_t split_addr) {
  auto it = this->allocated_blocks.lower_bound(split_addr);
  if (it != this->allocated_blocks.end() && it != this->allocated_blocks.begin()) {
    auto prev_block_it = it;
    prev_block_it--;
    auto& block = prev_block_it->second;
    if ((block.addr < split_addr) && ((block.addr + block.actual_size) > split_addr)) {
      throw std::runtime_error("An allocated block spans the allocator split boundary");
    }
  }

  this->addr_high = split_addr;
  Allocator ret{split_addr, this->addr_high};

  // Move all allocated blocks after the split address into ret
  while (it != this->allocated_blocks.end()) {
    Block& block = it->second;
    this->allocated_bytes -= block.actual_size;
    ret.allocated_bytes += block.actual_size;
    ret.allocated_blocks.insert(this->allocated_blocks.extract(it++));
  }

  // TODO: We could be smarter here, but it's probably not worth it - allocators are generally only split shortly after
  // a MemoryContext is created, and before any significant number of allocations.
  this->reconstruct_free_maps();
  ret.reconstruct_free_maps();

  if constexpr (verify_operations) {
    this->verify();
  }
  if (verify_operations) {
    ret.verify();
  }

  return ret;
}

uint32_t MemoryContext::Allocator::allocate(size_t requested_size) {
  // Round size up to a multiple of 4. I didn't do my homework on this, but blocks almost certainly need to be 2-byte
  // aligned for 68K apps and 4-byte aligned for PPC apps on actual Mac hardware. Our emulators don't have that
  // limitation, but for debugging purposes, it's nice not to have blocks start at odd addresses.
  size_t actual_size = (requested_size + 3) & (~3);
  if (actual_size == 0) {
    return 0;
  }

  // Find the smallest free block with enough space, and allocate the first part of it to fulfill the request
  auto it = this->free_blocks_by_size.lower_bound(actual_size);
  if (it == this->free_blocks_by_size.end()) {
    return 0; // Not enough space available
  }
  Block free_block = *it->second;
  Block allocated_block{.addr = free_block.addr, .requested_size = requested_size, .actual_size = actual_size};
  this->allocated_blocks.emplace(allocated_block.addr, allocated_block);
  this->delete_free_block(it);
  if (free_block.actual_size > actual_size) {
    this->add_free_block(
        free_block.addr + allocated_block.actual_size, free_block.actual_size - allocated_block.actual_size);
  }
  this->allocated_bytes += actual_size;

  if constexpr (verify_operations) {
    this->verify();
  }
  return allocated_block.addr;
}

bool MemoryContext::Allocator::allocate_at(uint32_t addr, size_t requested_size) {
  size_t actual_size = (requested_size + 3) & (~3); // See comment in allocate()
  if (actual_size == 0) {
    return false;
  }
  uint64_t req_end = static_cast<uint64_t>(addr) + actual_size;

  // Find the free block that spans this request
  auto it = this->free_blocks_by_addr.upper_bound(addr);
  if (it == this->free_blocks_by_addr.begin()) {
    return false;
  }
  it--;
  Block free_block = it->second;
  uint64_t free_block_end = free_block.addr + free_block.actual_size;
  if (free_block.addr > addr) {
    throw std::logic_error("Free block index is inconsistent");
  }
  if (free_block_end < req_end) {
    return false; // Part of the requested range is already allocated
  }

  this->delete_free_block(it);
  if (free_block.addr < addr) {
    this->add_free_block(free_block.addr, addr - free_block.addr);
  }
  if (free_block_end > req_end) {
    this->add_free_block(req_end, free_block_end - req_end);
  }
  this->allocated_blocks.emplace(
      addr, Block{.addr = addr, .requested_size = requested_size, .actual_size = actual_size});
  this->allocated_bytes += actual_size;

  if constexpr (verify_operations) {
    this->verify();
  }
  return true;
}

bool MemoryContext::Allocator::free(uint32_t addr) {
  auto block_it = this->allocated_blocks.find(addr);
  if (block_it == this->allocated_blocks.end()) {
    return false;
  }
  Block block = block_it->second;
  uint32_t free_block_addr = addr;
  size_t free_block_size = block.actual_size;
  uint64_t block_end_addr = static_cast<uint64_t>(block.addr) + block.actual_size;
  this->allocated_blocks.erase(block_it);

  // Delete the free blocks immediately before and after the deallocated block, so they can be merged into the new
  // free block
  auto it = this->free_blocks_by_addr.find(block.addr + block.actual_size);
  if (it != this->free_blocks_by_addr.end()) {
    if (it->second.addr < block_end_addr) {
      throw std::logic_error("Later free block overlaps allocated block");
    } else if (it->second.addr == block_end_addr) {
      free_block_size += it->second.actual_size;
      it = this->delete_free_block(it);
    }
  }
  if (it != this->free_blocks_by_addr.begin()) {
    it--;
    if (it->second.addr + it->second.actual_size > block.addr) {
      throw std::logic_error("Earlier free block overlaps allocated block");
    } else if (it->second.addr + it->second.actual_size == block.addr) {
      free_block_addr = it->second.addr;
      free_block_size += it->second.actual_size;
      it = this->delete_free_block(it);
    }
  }

  this->add_free_block(free_block_addr, free_block_size);

  if constexpr (verify_operations) {
    this->verify();
  }
  return true;
}

bool MemoryContext::Allocator::resize(uint32_t addr, size_t new_requested_size) {
  size_t new_actual_size = (new_requested_size + 3) & (~3); // See comment in allocate()
  if (new_actual_size == 0) {
    return false;
  }

  auto block_it = this->allocated_blocks.find(addr);
  if (block_it == this->allocated_blocks.end()) {
    return false;
  }
  auto& block = block_it->second;
  if (block.actual_size == new_actual_size) {
    return true;
  }
  uint64_t block_end_addr = static_cast<uint64_t>(block.addr) + block.actual_size;

  auto after_it = this->free_blocks_by_addr.find(block_end_addr);

  if (new_actual_size < block.actual_size) { // Block is shrinking (cannot fail)
    if (after_it == this->free_blocks_by_addr.end()) { // No free block after this one
      this->add_free_block(block.addr + new_actual_size, block.actual_size - new_actual_size);
    } else { // Extend the existing free block backward
      this->add_free_block(
          block.addr + new_actual_size, (block.actual_size - new_actual_size) + after_it->second.actual_size);
      this->delete_free_block(after_it);
    }
  } else if (new_actual_size > block.actual_size) { // Block is growing (can fail)
    if (after_it == this->free_blocks_by_addr.end()) {
      return false; // No space after existing allocated block
    } else if (after_it->second.actual_size < (new_actual_size - block.actual_size)) {
      return false; // Free block after existing block is not large enough
    } else { // Shorten existing free block
      if (after_it->second.actual_size > (new_actual_size - block.actual_size)) {
        this->add_free_block(
            block.addr + new_actual_size, after_it->second.actual_size - (new_actual_size - block.actual_size));
      }
      this->delete_free_block(after_it);
    }
  }
  block.requested_size = new_requested_size;
  block.actual_size = new_actual_size;

  if constexpr (verify_operations) {
    this->verify();
  }
  return true;
}

std::pair<bool, uint32_t> MemoryContext::Allocator::resize_reverse(uint32_t addr, size_t new_requested_size) {
  size_t new_actual_size = (new_requested_size + 3) & (~3); // See comment in allocate()
  if (new_actual_size == 0) {
    return {false, addr}; // Block size would be zero
  }

  auto block_it = this->allocated_blocks.find(addr);
  if (block_it == this->allocated_blocks.end()) {
    return {false, addr}; // Block is not allocated
  }
  auto block = block_it->second;

  // The difference must be a multiple of 4, since the block's end alignment must not change
  if ((new_requested_size % 3) != (block.requested_size % 3)) {
    return {false, addr}; // Block start would be misaligned
  }
  if (block.actual_size == new_actual_size) {
    return {true, addr}; // Block size would not change; nothing to do
  }

  uint64_t block_end_addr = static_cast<uint64_t>(block.addr) + block.actual_size;

  auto before_it = this->free_blocks_by_addr.upper_bound(block_end_addr);
  if (before_it == this->free_blocks_by_addr.begin()) {
    return {false, addr}; // No free block before allocated block
  }
  before_it--;
  if (before_it->second.addr + before_it->second.actual_size != block.addr) {
    before_it = this->free_blocks_by_addr.end();
  }
  auto free_block = before_it->second;

  if (new_actual_size < block.actual_size) {
    if (before_it == this->free_blocks_by_addr.end()) { // Create new free block
      this->add_free_block(block.addr, (block.actual_size - new_actual_size));
    } else { // Extend existing free block
      this->delete_free_block(before_it);
      this->add_free_block(free_block.addr, free_block.actual_size + (block.actual_size - new_actual_size));
    }
  } else if (new_actual_size > block.actual_size) {
    if (before_it == this->free_blocks_by_addr.end()) {
      return {false, addr}; // Not enough space before block
    } else if (before_it->second.actual_size < (new_actual_size - block.actual_size)) {
      return {false, addr}; // Free block before block is not large enough
    } else { // Shorten existing free block
      this->delete_free_block(before_it);
      this->add_free_block(free_block.addr, free_block.actual_size - (new_actual_size - block.actual_size));
    }
  }

  // Expand allocated block
  block.addr -= (new_actual_size - block.actual_size);
  block.requested_size = new_requested_size;
  block.actual_size = new_actual_size;
  this->allocated_blocks.emplace(block.addr, block);
  this->allocated_blocks.erase(block_it);

  if constexpr (verify_operations) {
    this->verify();
  }
  return {true, block.addr};
}

size_t MemoryContext::Allocator::get_block_size(uint32_t addr) const {
  auto it = this->allocated_blocks.find(addr);
  return (it == this->allocated_blocks.end()) ? 0 : it->second.requested_size;
}

bool MemoryContext::Allocator::exists(uint32_t addr, size_t size) const {
  auto it = this->allocated_blocks.upper_bound(addr);
  if (it == this->allocated_blocks.begin()) {
    return false;
  }
  it--;
  const auto& block = it->second;
  return ((block.addr <= addr) && (block.addr + block.actual_size >= addr + size));
}

MemoryContext::Allocator MemoryContext::Allocator::import_state(FILE* stream) {
  uint32_t addr_low = phosg::freadx<phosg::le_uint32_t>(stream);
  uint64_t addr_high = phosg::freadx<phosg::le_uint64_t>(stream);
  Allocator ret(addr_low, addr_high);
  uint32_t num_blocks = phosg::freadx<phosg::le_uint32_t>(stream);
  for (size_t z = 0; z < num_blocks; z++) {
    uint32_t addr = phosg::freadx<phosg::le_uint32_t>(stream);
    uint64_t size = phosg::freadx<phosg::le_uint64_t>(stream);
    ret.allocate_at(addr, size);
  }
  if (verify_operations) {
    ret.verify();
  }
  return ret;
}

void MemoryContext::Allocator::export_state(FILE* stream) const {
  phosg::fwritex<phosg::le_uint32_t>(stream, this->addr_low);
  phosg::fwritex<phosg::le_uint64_t>(stream, this->addr_high);
  phosg::fwritex<phosg::le_uint32_t>(stream, this->allocated_blocks.size());
  for (const auto& [_, block] : this->allocated_blocks) {
    phosg::fwritex<phosg::le_uint32_t>(stream, block.addr);
    phosg::fwritex<phosg::le_uint64_t>(stream, block.requested_size);
  }
}

void MemoryContext::Allocator::verify() const {
  if (this->allocated_bytes > (this->addr_high - this->addr_low)) {
    throw std::logic_error("Too many allocated bytes");
  }

  // Check index sizes. There can be at most one more free block than there are allocated blocks, since we should have
  // merged them as soon as possible
  if (this->free_blocks_by_addr.size() > this->allocated_blocks.size() + 1) {
    throw std::logic_error("There are too many free blocks");
  }

  // Check index keys
  for (const auto& [k, block] : this->allocated_blocks) {
    if (block.addr != k) {
      throw std::logic_error("Allocated block key is incorrect");
    }
  }
  for (const auto& [k, block] : this->free_blocks_by_addr) {
    if (block.addr != k) {
      throw std::logic_error("Free block address key is incorrect");
    }
  }
  for (const auto& [k, block] : this->free_blocks_by_size) {
    if (block->actual_size != k) {
      throw std::logic_error("Free block size key is incorrect");
    }
  }

  // Check that no blocks overlap and the entire address space is accounted for
  bool last_block_free = false;
  auto block_it = this->allocated_blocks.begin();
  auto free_it = this->free_blocks_by_addr.begin();
  size_t computed_allocated_bytes = 0;
  for (uint64_t addr = this->addr_low; addr < this->addr_high;) {
    bool is_block = false, is_free = false;
    if (block_it != this->allocated_blocks.end()) {
      if (block_it->second.addr < addr) {
        throw std::logic_error("Allocated block overlaps with previous block");
      }
      is_block = (block_it->second.addr == addr);
    }
    if (free_it != this->free_blocks_by_addr.end()) {
      if (free_it->second.addr < addr) {
        throw std::logic_error("Free block overlaps with previous block");
      }
      is_free = (free_it->second.addr == addr);
    }
    if (is_block && is_free) {
      throw std::logic_error("Memory block is both allocated and free");
    } else if (is_block) {
      last_block_free = false;
      addr += block_it->second.actual_size;
      computed_allocated_bytes += block_it->second.actual_size;
      block_it++;
    } else if (is_free) {
      if (last_block_free) {
        throw std::logic_error("Adjacent free blocks were not merged");
      }
      last_block_free = true;
      addr += free_it->second.actual_size;
      free_it++;
    } else {
      throw std::logic_error("Memory block is neither allocated nor free");
    }
  }
  if (block_it != this->allocated_blocks.end()) {
    throw std::logic_error("Not all allocated blocks are within the allocator\'s range");
  }
  if (free_it != this->free_blocks_by_addr.end()) {
    throw std::logic_error("Not all free blocks are within the allocator\'s range");
  }
  if (this->allocated_bytes != computed_allocated_bytes) {
    throw std::logic_error("Allocated byte count is incorrect");
  }

  // Check the size-keyed index
  if (this->free_blocks_by_addr.size() != this->free_blocks_by_size.size()) {
    throw std::logic_error("Free block indexes do not match");
  }
  std::set<uint32_t> remaining_addrs;
  for (const auto& [addr, _] : this->free_blocks_by_addr) {
    remaining_addrs.emplace(addr);
  }
  for (const auto& [size, block] : this->free_blocks_by_size) {
    auto it = this->free_blocks_by_addr.find(block->addr);
    if (it == this->free_blocks_by_addr.end()) {
      throw std::logic_error("Free block size index contains entry for nonexistent block");
    }
    if (block != &it->second) {
      throw std::logic_error("Free block size index contains entry for incorrect block");
    }
    remaining_addrs.erase(it->second.addr);
  }
  if (!remaining_addrs.empty()) {
    throw std::logic_error("Free block size index contains duplicate entry");
  }
}

void MemoryContext::Allocator::add_free_block(uint32_t addr, size_t size) {
  auto& block = this->free_blocks_by_addr.emplace(addr, Block{.addr = addr, .requested_size = size, .actual_size = size}).first->second;
  this->free_blocks_by_size.emplace(size, &block);
}

std::map<uint32_t, MemoryContext::Allocator::Block>::iterator MemoryContext::Allocator::delete_free_block(
    std::map<uint32_t, Block>::iterator block_it) {
  size_t count = 0;
  for (auto [it, end_it] = this->free_blocks_by_size.equal_range(block_it->second.actual_size); it != end_it;) {
    if (it->second == &block_it->second) {
      it = this->free_blocks_by_size.erase(it);
      count++;
    } else {
      it++;
    }
  }
  if (count != 1) {
    throw std::logic_error("Free block size index is inconsistent");
  }
  return this->free_blocks_by_addr.erase(block_it);
}

std::multimap<size_t, MemoryContext::Allocator::Block*>::iterator MemoryContext::Allocator::delete_free_block(
    std::multimap<size_t, Block*>::iterator it) {
  this->free_blocks_by_addr.erase(it->second->addr);
  return this->free_blocks_by_size.erase(it);
}

void MemoryContext::Allocator::reconstruct_free_maps() {
  this->free_blocks_by_addr.clear();
  this->free_blocks_by_size.clear();
  auto block_it = this->allocated_blocks.begin();
  for (uint64_t addr = this->addr_low; addr < this->addr_high;) {
    uint32_t free_block_addr = addr;
    size_t free_block_size;
    if (block_it == this->allocated_blocks.end()) {
      free_block_size = this->addr_high - addr;
      addr = this->addr_high;
    } else if (block_it->second.addr < addr) {
      throw std::logic_error("Allocated block order is incorrect");
    } else {
      free_block_size = block_it->second.addr - addr;
      addr = block_it->second.addr + block_it->second.actual_size;
    }
    if (free_block_size > 0) {
      this->add_free_block(free_block_addr, free_block_size);
    }
  }
}

MemoryContext::Allocator& MemoryContext::get_or_split_allocator(uint32_t addr_low, uint32_t addr_high) {
  auto it = this->allocators.upper_bound(addr_low);
  if (it == this->allocators.begin()) {
    throw std::logic_error("Low memory is not covered by any allocator");
  }
  it--;

  auto [existing_addr_low, existing_addr_high] = it->second.range();
  if (existing_addr_low > addr_low) {
    throw std::logic_error("Allocator index is inconsistent");
  }
  if (existing_addr_high < addr_high) {
    throw std::runtime_error("Allocator is already split within requested range");
  }

  if (existing_addr_low < addr_low) {
    this->split_allocators(addr_low);
  }
  if (existing_addr_high > addr_high) {
    this->split_allocators(addr_high);
  }
  return this->get_allocator(addr_low);
}

MemoryContext::Allocator& MemoryContext::get_allocator(uint32_t addr) {
  auto it = this->allocators.upper_bound(addr);
  if (it == this->allocators.begin()) {
    throw std::logic_error("Low memory is not covered by any allocator");
  }
  it--;
  return it->second;
}

void MemoryContext::split_allocators(uint32_t addr) {
  this->allocators.emplace(addr, this->get_allocator(addr).split(addr));
  if constexpr (verify_operations) {
    this->verify();
  }
}

bool MemoryContext::exists(uint32_t addr, size_t size, bool skip_strict) const {
  if (!this->check_pages_valid(addr, size)) {
    return false;
  }
  return (!this->strict || skip_strict || this->get_allocator(addr).exists(addr, size));
}

void MemoryContext::set_symbol_addr(const std::string& name, uint32_t addr) {
  if (!this->symbol_addrs.emplace(name, addr).second) {
    throw std::runtime_error("cannot redefine symbol");
  }
  // Multiple symbols can share the same address (C++ aliases, vtables, TVectors); keep the first name for the reverse
  // index only
  this->addr_symbols.emplace(addr, name);
  if constexpr (verify_operations) {
    this->verify();
  }
}

void MemoryContext::delete_symbol(const std::string& name) {
  auto it = this->symbol_addrs.find(name);
  if (it != this->symbol_addrs.end()) {
    this->addr_symbols.erase(it->second);
    this->symbol_addrs.erase(it);
  }
  if constexpr (verify_operations) {
    this->verify();
  }
}

void MemoryContext::delete_symbol(uint32_t addr) {
  auto it = this->addr_symbols.find(addr);
  if (it != this->addr_symbols.end()) {
    this->symbol_addrs.erase(it->second);
    this->addr_symbols.erase(it);
  }
  if constexpr (verify_operations) {
    this->verify();
  }
}

uint32_t MemoryContext::get_symbol_addr(const std::string& name) const {
  return this->symbol_addrs.at(name);
}

const std::string& MemoryContext::get_symbol_at_addr(uint32_t addr) const {
  return this->addr_symbols.at(addr);
}

const std::unordered_map<std::string, uint32_t> MemoryContext::all_symbols() const {
  return this->symbol_addrs;
}

MemoryContext MemoryContext::import_state(FILE* stream) {
  uint8_t version = phosg::freadx<uint8_t>(stream);
  if (version != 2) {
    throw std::runtime_error("unknown format version");
  }

  MemoryContext ret;
  ret.strict = phosg::freadx<uint8_t>(stream);

  std::array<uint8_t, (MemoryContext::PAGE_COUNT >> 3)> pages_valid;
  phosg::freadx(stream, pages_valid.data(), pages_valid.size());
  for (size_t z = 0; z < MemoryContext::PAGE_COUNT; z++) {
    if (pages_valid[z >> 3] & (0x80 >> (z & 7))) {
      uint32_t page_addr = z << MemoryContext::PAGE_BITS;
      ret.make_pages_valid(page_addr, MemoryContext::PAGE_SIZE);
      phosg::freadx(stream, ret.at<void>(page_addr, MemoryContext::PAGE_SIZE), MemoryContext::PAGE_SIZE);
    }
  }

  uint32_t num_allocators = phosg::freadx<phosg::le_uint32_t>(stream);
  while (ret.allocators.size() < num_allocators) {
    auto allocator = Allocator::import_state(stream);
    uint32_t key = allocator.range().first;
    ret.allocators.emplace(key, std::move(allocator));
  }

  size_t symbol_count = phosg::freadx<phosg::le_uint32_t>(stream);
  for (size_t z = 0; z < symbol_count; z++) {
    uint32_t addr = phosg::freadx<phosg::le_uint32_t>(stream);
    size_t name_length = phosg::freadx<phosg::le_uint32_t>(stream);
    std::string name = phosg::freadx(stream, name_length);
    ret.symbol_addrs.emplace(name, addr);
    ret.addr_symbols.emplace(addr, std::move(name));
  }

  if (verify_operations) {
    ret.verify();
  }
  return ret;
}

void MemoryContext::export_state(FILE* stream) const {
  phosg::fwritex<uint8_t>(stream, 2); // version
  phosg::fwritex<uint8_t>(stream, this->strict);
  phosg::fwritex(stream, this->pages_valid.data(), this->pages_valid.size());
  for (size_t z = 0; z < MemoryContext::PAGE_COUNT; z++) {
    if (this->page_is_valid(z)) {
      uint32_t page_addr = z << MemoryContext::PAGE_BITS;
      phosg::fwritex(stream, this->at<void>(page_addr, MemoryContext::PAGE_SIZE), MemoryContext::PAGE_SIZE);
    }
  }

  phosg::fwritex<phosg::le_uint32_t>(stream, this->allocators.size());
  for (const auto& [_, allocator] : this->allocators) {
    allocator.export_state(stream);
  }

  phosg::fwritex<phosg::le_uint32_t>(stream, this->symbol_addrs.size());
  for (const auto& [name, addr] : this->symbol_addrs) {
    phosg::fwritex<phosg::le_uint32_t>(stream, addr);
    phosg::fwritex<phosg::le_uint32_t>(stream, name.size());
    phosg::fwritex(stream, name);
  }
}

void MemoryContext::verify() const {
  // Check allocators' internal consistency, and check that all allocated regions are in valid pages
  uint32_t addr = 0x00000000;
  for (const auto& [k, allocator] : this->allocators) {
    auto [addr_low, addr_high] = allocator.range();
    if (addr_low != addr) {
      throw std::logic_error("Part of the context is not covered by any allocator");
    }
    if (addr_low >= addr_high) {
      throw std::logic_error("Allocator range is empty or inverted");
    }
    if (k != addr_low) {
      throw std::logic_error("Allocator key is incorrect");
    }
    allocator.verify();
    for (const auto& [_, block] : allocator.all_blocks()) {
      if (!this->check_pages_valid(block.addr, block.actual_size)) {
        throw std::logic_error("Allocated region spans invalid page");
      }
    }
  }

  // Symbols do not have to be in allocated memory, but the indexes must be inverses of each other. It suffices to
  // check the sizes and only one direction of the mapping since both name and address must be separately unique
  if (this->addr_symbols.size() != this->symbol_addrs.size()) {
    throw std::logic_error("Symbol indexes are not the same size");
  }
  for (const auto& [name, addr] : this->symbol_addrs) {
    auto it = this->addr_symbols.find(addr);
    if (it == this->addr_symbols.end()) {
      throw std::logic_error("Symbol is missing from reverse index");
    }
    if (it->second != name) {
      throw std::logic_error("Symbol has incorrect name in reverse index");
    }
  }
}

void MemoryContext::make_pages_valid(uint32_t addr, uint32_t size) {
  uint32_t start_page_num = this->page_number_for_addr(addr);
  uint32_t end_page_num = this->page_number_for_addr(addr + size - 1);
  alloc_reserved_mem(
      reinterpret_cast<uint8_t*>(this->base) + (start_page_num << this->PAGE_BITS),
      (end_page_num + 1 - start_page_num) << this->PAGE_BITS);
  for (uint64_t page_num = start_page_num; page_num <= end_page_num; page_num++) {
    this->pages_valid[page_num >> 3] |= (0x80 >> (page_num & 7));
  }
}

bool MemoryContext::check_pages_valid(uint32_t addr, uint32_t size) const {
  uint32_t start_page_num = this->page_number_for_addr(addr);
  uint32_t end_page_num = this->page_number_for_addr(addr + size - 1);
  for (uint64_t page_num = start_page_num; page_num <= end_page_num; page_num++) {
    if (!this->page_is_valid(page_num)) {
      return false;
    }
  }
  return true;
}

} // namespace ResourceDASM
