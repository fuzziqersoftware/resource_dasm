#include "MemoryContext.hh"

#include <stdint.h>
#include <sys/mman.h>

#include <new>

using namespace std;


MemoryContext::MemoryContext(size_t size) : size_bytes(size) {
  this->base_ptr = mmap(nullptr, this->size_bytes, PROT_READ | PROT_WRITE,
      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (this->base_ptr == MAP_FAILED) {
    this->base_ptr = nullptr;
    throw std::bad_alloc();
  }

  // reserve the first 16KB for low memory
  this->allocated_regions_by_addr.emplace(0, 0x4000);
  this->free_regions_by_size.emplace(this->size_bytes - 0x4000, 0x4000);
  this->free_regions_by_addr.emplace(0x4000, this->size_bytes - 0x4000);
}

MemoryContext::~MemoryContext() {
  if (this->base_ptr) {
    munmap(this->base_ptr, this->size_bytes);
  }
}

void* MemoryContext::base() {
  return this->base_ptr;
}

size_t MemoryContext::size() {
  return this->size_bytes;
}

void* MemoryContext::alloc(size_t requested_size, bool align_to_end) {
  // Round requested_size up to a multiple of 0x10
  requested_size = (requested_size + 0x0F) & (~0x0F);

  // Find the smallest free block with enough space, and put the allocated block
  // in the first part of it
  auto free_block_it = this->free_regions_by_size.lower_bound(requested_size);
  uint32_t free_block_addr = free_block_it->second;
  uint32_t free_block_size = free_block_it->first;
  this->free_regions_by_addr.erase(free_block_it->second);
  this->free_regions_by_size.erase(free_block_it);

  // Figure out how to split up the free block
  size_t remaining_size = free_block_size - requested_size;
  uint32_t allocated_block_addr;
  uint32_t new_free_block_addr;
  if (align_to_end) {
    allocated_block_addr = free_block_addr + free_block_size - requested_size;
    new_free_block_addr = free_block_addr;
  } else {
    allocated_block_addr = free_block_addr;
    new_free_block_addr = free_block_addr + requested_size;
  }

  // Create a new allocated block, and a new free block if any space remains
  this->allocated_regions_by_addr.emplace(allocated_block_addr, requested_size);
  if (remaining_size > 0) {
    this->free_regions_by_addr.emplace(new_free_block_addr, remaining_size);
    this->free_regions_by_size.emplace(remaining_size, new_free_block_addr);
  }

  return this->at(allocated_block_addr);
}

void MemoryContext::free(void* ptr) {
  // Sanity checks first
  ptrdiff_t ptr_diff = reinterpret_cast<ptrdiff_t>(ptr);
  ptrdiff_t base_diff = reinterpret_cast<ptrdiff_t>(this->base_ptr);
  if ((ptr_diff < base_diff) || (ptr_diff >= base_diff + static_cast<ssize_t>(this->size_bytes))) {
    throw invalid_argument("MemoryContext::free ptr is out of range");
  }
  uint32_t local_addr = ptr_diff - base_diff;
  if (local_addr == 0) {
    throw invalid_argument("MemoryContext::free ptr points to be beginning of memory");
  }

  auto allocated_region_it = this->allocated_regions_by_addr.find(local_addr);
  if (allocated_region_it == this->allocated_regions_by_addr.end()) {
    throw invalid_argument("MemoryContext::free ptr was not allocated");
  }

  // Deallocate the region
  uint32_t size = allocated_region_it->second;
  this->allocated_regions_by_addr.erase(allocated_region_it);

  // If there are no free regions at all, make one
  if (this->free_regions_by_addr.empty()) {
    this->free_regions_by_addr.emplace(local_addr, size);
    this->free_regions_by_size.emplace(size, local_addr);

  // If there are free regions, check the regions before and after the freed
  // region; if either or both directly border it, then coalesce them into a
  // single free region
  } else {
    auto begin_it = this->free_regions_by_addr.begin();
    auto end_it = this->free_regions_by_addr.end();
    auto next_it = this->free_regions_by_addr.lower_bound(local_addr);

    // This is like `(next_it != begin_it) ? (next_it - 1) : end_it` but
    // iterators can't be used in expressions like `x - 1`... :(
    auto prev_it = next_it;
    if (prev_it == begin_it) {
      prev_it = end_it;
    } else {
      prev_it--;
    }

    uint32_t freed_addr = local_addr;
    uint32_t freed_size = size;
    if (next_it != end_it && next_it->first == freed_addr + freed_size) {
      freed_size += next_it->second;
      this->free_regions_by_size.erase(next_it->second);
      this->free_regions_by_addr.erase(next_it);
    }
    if (prev_it != end_it && prev_it->first + prev_it->second == freed_addr) {
      freed_addr = prev_it->first;
      freed_size += prev_it->second;
      this->free_regions_by_size.erase(prev_it->second);
      this->free_regions_by_addr.erase(prev_it);
    }

    this->free_regions_by_size.emplace(freed_size, freed_addr);
    this->free_regions_by_addr.emplace(freed_addr, freed_size);
  }
}

void MemoryContext::set_symbol_addr(const char* name, uint32_t addr) {
  if (!this->symbol_addrs.emplace(name, addr).second) {
    throw runtime_error("cannot redefine symbol");
  }
}

uint32_t MemoryContext::get_symbol_addr(const char* name) {
  return this->symbol_addrs.at(name);
}
