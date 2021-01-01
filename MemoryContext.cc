#include "MemoryContext.hh"

#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>

using namespace std;


MemoryContext::MemoryContext() : page_size(sysconf(_SC_PAGESIZE)) {
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
  }

  if (!this->page_bits) {
    throw invalid_argument("system page bits is zero");
  }

  size_t total_pages = (0x100000000 >> this->page_bits) - 1;
  this->page_host_addrs.resize(total_pages, nullptr);
  this->free_page_regions_by_count.emplace(total_pages, 0);
}

MemoryContext::~MemoryContext() {
  for (const auto& it : this->allocated_page_regions_by_index) {
    munmap(this->page_host_addrs[it.first], it.second << this->page_bits);
  }
}

uint32_t MemoryContext::allocate(size_t requested_size, bool align_to_end) {
  // Round requested_size up to a multiple of 0x10
  requested_size = (requested_size + 0x0F) & (~0x0F);

  // Find the smallest free block with enough space, and put the allocated block
  // in the first part of it
  auto free_block_it = this->free_regions_by_size.lower_bound(requested_size);
  if (free_block_it == this->free_regions_by_size.end()) {

    // There's no free page region of sufficient size - we'll have to allocate
    // some more pages
    size_t needed_page_count = (requested_size + (this->page_size - 1)) >> this->page_bits;
    auto free_page_it = this->free_page_regions_by_count.lower_bound(needed_page_count);
    if (free_page_it == this->free_page_regions_by_count.end()) {
      return 0;
    }

    // Allocate the page region
    void* region_base = mmap(nullptr, needed_page_count << this->page_bits,
        PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (region_base == MAP_FAILED) {
      return 0;
    }

    // Figure out how to split up the block
    uint32_t free_page_index = free_page_it->second;
    uint32_t free_page_count = free_page_it->first;
    size_t remaining_page_count = free_page_count - needed_page_count;
    uint32_t allocated_page_index;
    uint32_t new_free_page_index;
    if (align_to_end) {
      allocated_page_index = free_page_index + free_page_count - needed_page_count;
      new_free_page_index = free_page_index;
    } else {
      allocated_page_index = free_page_index;
      new_free_page_index = free_page_index + needed_page_count;
    }

    // Delete the old free page region and create a new allocated page region,
    // and create a new free page region if any space remains
    this->free_page_regions_by_count.erase(free_page_it);
    this->allocated_page_regions_by_index.emplace(allocated_page_index, needed_page_count);
    if (remaining_page_count > 0) {
      this->free_page_regions_by_count.emplace(remaining_page_count, new_free_page_index);
    }

    // Update the host address index appropriately
    for (size_t x = 0; x < needed_page_count; x++) {
      size_t page_index = allocated_page_index + x;
      if (this->page_host_addrs[page_index]) {
        throw logic_error("page already has host address");
      }
      this->page_host_addrs[page_index] = reinterpret_cast<uint8_t*>(region_base) + (x << this->page_bits);
    }

    // Add the newly-created free space to the index
    // TODO: rewrite this to just allocate the block directly here instead of
    // falling through to the case below where there's a large-enough free block
    uint32_t allocated_region_addr = allocated_page_index << this->page_bits;
    uint32_t allocated_region_size = needed_page_count << this->page_bits;
    this->free_regions_by_addr.emplace(allocated_region_addr, allocated_region_size);
    free_block_it = this->free_regions_by_size.emplace(allocated_region_size, allocated_region_addr).first;
  }

  // Figure out how to split up the free block
  uint32_t free_block_addr = free_block_it->second;
  uint32_t free_block_size = free_block_it->first;
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

  // Delete the old free block, create a new allocated block, and create a new
  // free block if any space remains
  this->free_regions_by_addr.erase(free_block_it->second);
  this->free_regions_by_size.erase(free_block_it);

  this->allocated_regions_by_addr.emplace(allocated_block_addr, requested_size);
  if (remaining_size > 0) {
    this->free_regions_by_addr.emplace(new_free_block_addr, remaining_size);
    this->free_regions_by_size.emplace(remaining_size, new_free_block_addr);
  }

  return allocated_block_addr;
}

void MemoryContext::free(uint32_t addr) {
  // Sanity checks first
  uint32_t page_index = addr >> this->page_bits;
  if (!this->page_host_addrs[page_index]) {
    throw invalid_argument("pointer being freed is not part of any page");
  }

  auto allocated_region_it = this->allocated_regions_by_addr.find(addr);
  if (allocated_region_it == this->allocated_regions_by_addr.end()) {
    throw invalid_argument("pointer being freed was not allocated");
  }

  // Deallocate the region
  uint32_t size = allocated_region_it->second;
  this->allocated_regions_by_addr.erase(allocated_region_it);

  // If there are no free regions at all, make one
  if (this->free_regions_by_addr.empty()) {
    this->free_regions_by_addr.emplace(addr, size);
    this->free_regions_by_size.emplace(size, addr);

  // If there are free regions, check the regions before and after the freed
  // region; if either or both directly border it, then coalesce them into a
  // single free region. But, take care not to coalesce across page boundaries.
  } else {
    auto begin_it = this->free_regions_by_addr.begin();
    auto end_it = this->free_regions_by_addr.end();
    auto next_it = this->free_regions_by_addr.lower_bound(addr);

    bool freed_region_begins_on_page_boundary = this->allocated_page_regions_by_index.count(addr >> this->page_bits);
    bool next_region_begins_on_page_boundary = (next_it == end_it)
        ? false
        : this->allocated_page_regions_by_index.count(next_it->first >> this->page_bits);

    // This is like `(next_it != begin_it) ? (next_it - 1) : end_it` but
    // iterators can't be used in expressions like `x - 1`... :(
    auto prev_it = next_it;
    if (prev_it == begin_it) {
      prev_it = end_it;
    } else {
      prev_it--;
    }

    uint32_t freed_addr = addr;
    uint32_t freed_size = size;
    if (!next_region_begins_on_page_boundary && (next_it != end_it) && (next_it->first == freed_addr + freed_size)) {
      freed_size += next_it->second;
      this->free_regions_by_size.erase(next_it->second);
      this->free_regions_by_addr.erase(next_it);
    }
    if (!freed_region_begins_on_page_boundary && (prev_it != end_it) && (prev_it->first + prev_it->second == freed_addr)) {
      freed_addr = prev_it->first;
      freed_size += prev_it->second;
      this->free_regions_by_size.erase(prev_it->second);
      this->free_regions_by_addr.erase(prev_it);
    }

    this->free_regions_by_size.emplace(freed_size, freed_addr);
    this->free_regions_by_addr.emplace(freed_addr, freed_size);
  }

  // TODO: check if the entire page region is now free and unmap it if so
}

void MemoryContext::set_symbol_addr(const char* name, uint32_t addr) {
  if (!this->symbol_addrs.emplace(name, addr).second) {
    throw runtime_error("cannot redefine symbol");
  }
}

uint32_t MemoryContext::get_symbol_addr(const char* name) {
  return this->symbol_addrs.at(name);
}

void MemoryContext::print_state(FILE* stream) const {
  fprintf(stream, "[mem bits=%hhu alloc=[", this->page_bits);
  for (const auto& it : this->allocated_regions_by_addr) {
    fprintf(stream, "(%X,%X),", it.first, it.second);
  }
  fprintf(stream, "] free=[");
  for (const auto& it : this->free_regions_by_addr) {
    fprintf(stream, "(%X,%X),", it.first, it.second);
  }
  fprintf(stream, "] frees=[");
  for (const auto& it : this->free_regions_by_size) {
    fprintf(stream, "(%X,%X),", it.first, it.second);
  }
  fprintf(stream, "] allocp=[");
  for (const auto& it : this->allocated_page_regions_by_index) {
    fprintf(stream, "(%X,%X),", it.first, it.second);
  }
  fprintf(stream, "] freepc=[");
  for (const auto& it : this->free_page_regions_by_count) {
    fprintf(stream, "(%X,%X),", it.first, it.second);
  }
  fprintf(stream, "]\n");
}
