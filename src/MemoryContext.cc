#include "MemoryContext.hh"

#include <inttypes.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>

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

  this->free_all();
}

MemoryContext::~MemoryContext() {
  for (const auto& it : this->allocated_page_regions_by_index) {
    munmap(this->page_host_addrs[it.first], it.second << this->page_bits);
  }
}

uint32_t MemoryContext::allocate(size_t requested_size) {
  // Find the smallest free block with enough space, and put the allocated block
  // in the first part of it
  auto free_block_it = this->free_regions_by_size.lower_bound(requested_size);
  if (free_block_it == this->free_regions_by_size.end()) {

    // There's no free page region of sufficient size - we'll have to allocate
    // some more pages

    // Allocate at least 1MB at a time
    size_t needed_page_count = (requested_size + (this->page_size - 1)) >> this->page_bits;
    if (needed_page_count << this->page_bits < 0x100000) {
      needed_page_count = 0x100000 >> this->page_bits;
    }

    // Find an address space region with enough space for this page region
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
    uint32_t allocated_page_index = free_page_index;
    uint32_t new_free_page_index = free_page_index + needed_page_count;

    // Delete the old free page region and create a new allocated page region,
    // and create a new free page region if any space remains
    this->free_page_regions_by_index.erase(free_page_it->second);
    this->free_page_regions_by_count.erase(free_page_it);
    this->allocated_page_regions_by_index.emplace(allocated_page_index, needed_page_count);
    if (remaining_page_count > 0) {
      this->free_page_regions_by_index.emplace(new_free_page_index, remaining_page_count);
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
  uint32_t allocated_block_addr = free_block_addr;
  uint32_t new_free_block_addr = free_block_addr + requested_size;

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

uint32_t MemoryContext::allocate_at(uint32_t addr, size_t requested_size) {
  // Make sure no allocated block overlaps with the requested block
  {
    auto existing_block_it = this->allocated_regions_by_addr.lower_bound(addr);
    if (existing_block_it != this->allocated_regions_by_addr.end()) {
      if (existing_block_it->first < addr + requested_size) {
        return 0; // next block begins before requested block ends
      }
    }
    if (existing_block_it != this->allocated_regions_by_addr.begin()) {
      existing_block_it--;
      if (existing_block_it->first + existing_block_it->second > addr) {
        return 0; // prev block ends after requested block begins
      }
    }
  }

  // TODO: For now, this function always allocates a new page region, so the
  // requested range must not have any currently-valid pages in it. In the
  // future we may want to support allocating from existing pages.
  uint32_t start_page_index = addr >> this->page_bits;
  uint32_t needed_page_count = ((addr + requested_size + this->page_size - 1) >> this->page_bits) - start_page_index;
  auto free_page_it = this->free_page_regions_by_index.upper_bound(start_page_index);
  if (free_page_it == this->free_page_regions_by_index.begin()) {
    return 0;
  }
  free_page_it--;
  if ((free_page_it->first > start_page_index) ||
      (free_page_it->first + free_page_it->second < start_page_index + needed_page_count)) {
    return 0;
  }

  // Allocate the page region and update the host address index
  void* region_base = mmap(nullptr, needed_page_count << this->page_bits,
      PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (region_base == MAP_FAILED) {
    return 0;
  }
  for (size_t x = 0; x < needed_page_count; x++) {
    size_t page_index = start_page_index + x;
    if (this->page_host_addrs[page_index]) {
      throw logic_error("page already has host address");
    }
    this->page_host_addrs[page_index] = reinterpret_cast<uint8_t*>(region_base) + (x << this->page_bits);
  }

  // Split the free page region into multiple (if needed)
  uint32_t existing_free_page_index = free_page_it->first;
  uint32_t existing_free_page_count = free_page_it->second;
  uint32_t before_free_pages = start_page_index - existing_free_page_index;
  uint32_t after_free_pages = (existing_free_page_index + existing_free_page_count) - (start_page_index + needed_page_count);
  this->free_page_regions_by_count.erase(free_page_it->second);
  this->free_page_regions_by_index.erase(free_page_it);
  if (before_free_pages) {
    this->free_page_regions_by_count.emplace(before_free_pages, existing_free_page_index);
    this->free_page_regions_by_index.emplace(existing_free_page_index, before_free_pages);
  }
  if (after_free_pages) {
    this->free_page_regions_by_count.emplace(after_free_pages, start_page_index + needed_page_count);
    this->free_page_regions_by_index.emplace(start_page_index + needed_page_count, after_free_pages);
  }

  // Create the allocated block and free blocks if needed
  uint32_t allocated_page_region_start_addr = start_page_index << this->page_bits;
  uint32_t allocated_page_region_end_addr = (start_page_index + needed_page_count) << this->page_bits;
  if (allocated_page_region_start_addr < addr) {
    size_t remaining_size = addr - allocated_page_region_start_addr;
    this->free_regions_by_size.emplace(remaining_size, allocated_page_region_start_addr);
    this->free_regions_by_addr.emplace(allocated_page_region_start_addr, remaining_size);
  }
  if (addr + requested_size < allocated_page_region_end_addr) {
    size_t remaining_size = allocated_page_region_end_addr - (addr + requested_size);
    this->free_regions_by_size.emplace(remaining_size, addr + requested_size);
    this->free_regions_by_addr.emplace(addr + requested_size, remaining_size);
  }
  this->allocated_regions_by_addr.emplace(addr, requested_size);

  return addr;
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

void MemoryContext::free_all() {
  for (const auto& it : this->allocated_page_regions_by_index) {
    munmap(this->page_host_addrs[it.first], it.second << this->page_bits);
  }

  this->allocated_regions_by_addr.clear();

  this->allocated_page_regions_by_index.clear();
  this->free_page_regions_by_count.clear();
  this->free_page_regions_by_index.clear();

  this->free_regions_by_addr.clear();
  this->free_regions_by_size.clear();
  this->symbol_addrs.clear();

  for (auto& addr : this->page_host_addrs) {
    addr = nullptr;
  }

  size_t total_pages = (0x100000000 >> this->page_bits) - 1;
  this->page_host_addrs.clear();
  this->page_host_addrs.resize(total_pages, nullptr);
  this->free_page_regions_by_count.emplace(total_pages, 0);
  this->free_page_regions_by_index.emplace(0, total_pages);
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
  return this->allocated_regions_by_addr.at(addr);
}

size_t MemoryContext::get_page_size() const {
  return this->page_size;
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
  fprintf(stream, "] freep=[");
  for (const auto& it : this->free_page_regions_by_index) {
    fprintf(stream, "(%X,%X),", it.first, it.second);
  }
  fprintf(stream, "] freepc=[");
  for (const auto& it : this->free_page_regions_by_count) {
    fprintf(stream, "(%X,%X),", it.first, it.second);
  }
  fprintf(stream, "]\n");
}

void MemoryContext::print_contents(FILE* stream) const {
  for (const auto& it : this->allocated_regions_by_addr) {
    print_data(stream, page_host_addrs.at(it.first >> this->page_bits), it.second, it.first);
  }
}

void MemoryContext::import_state(FILE* stream) {
  this->free_all();

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
    if (this->allocate_at(addr, size) != addr) {
      throw runtime_error("cannot allocate memory");
    }
    freadx(stream, this->at(addr, size), size);
  }
}

void MemoryContext::export_state(FILE* stream) const {
  uint8_t version = 0;
  fwritex(stream, &version, sizeof(version));

  uint64_t region_count = this->allocated_regions_by_addr.size();
  fwritex(stream, &region_count, sizeof(region_count));
  for (const auto& region_it : this->allocated_regions_by_addr) {
    uint32_t addr = region_it.first;
    uint32_t size = region_it.second;
    fwritex(stream, &addr, sizeof(addr));
    fwritex(stream, &size, sizeof(size));
    fwritex(stream, this->at(addr, size), size);
  }
}
