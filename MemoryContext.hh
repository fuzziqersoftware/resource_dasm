#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>
#include <phosg/Encoding.hh>


class MemoryContext {
public:
  MemoryContext();
  ~MemoryContext();

  uint32_t guest_addr_for_host_addr(const void* ptr);

  inline void* at(uint32_t addr, size_t size = 1) {
    size_t start_page_index = addr >> this->page_bits;
    size_t end_page_index = (addr + size) >> this->page_bits;
    void* page_addr = this->page_host_addrs[start_page_index];
    if (!page_addr) {
      throw std::out_of_range("address not within allocated pages");
    }
    for (size_t index = start_page_index + 1; index < end_page_index; index++) {
      if (!this->page_host_addrs[start_page_index]) {
        throw std::out_of_range("data not contained within allocated pages");
      }
    }
    return reinterpret_cast<uint8_t*>(page_addr) + (addr & 0xFFF);
  }

  template <typename T>
  T* obj(uint32_t addr, uint32_t size = sizeof(T)) {
    return reinterpret_cast<T*>(this->at(addr, size));
  }

  template <typename T>
  T read(uint32_t addr) {
    return *this->obj<T>(addr);
  }
  template <typename T>
  void write(uint32_t addr, const T& obj) {
    *this->obj<T>(addr) = obj;
  }

  inline int8_t read_s8(uint32_t addr) {
    return this->read<int8_t>(addr);
  }
  inline void write_s8(uint32_t addr, int8_t value) {
    this->write<int8_t>(addr, value);
  }
  inline uint8_t read_u8(uint32_t addr) {
    return this->read<uint8_t>(addr);
  }
  inline void write_u8(uint32_t addr, uint8_t value) {
    this->write<uint8_t>(addr, value);
  }
  inline int16_t read_s16(uint32_t addr) {
    return bswap16(this->read<int16_t>(addr));
  }
  inline void write_s16(uint32_t addr, int16_t value) {
    this->write<int16_t>(addr, bswap16(value));
  }
  inline uint16_t read_u16(uint32_t addr) {
    return bswap16(this->read<uint16_t>(addr));
  }
  inline void write_u16(uint32_t addr, uint16_t value) {
    this->write<uint16_t>(addr, bswap16(value));
  }
  inline int32_t read_s32(uint32_t addr) {
    return bswap32(this->read<int32_t>(addr));
  }
  inline void write_s32(uint32_t addr, int32_t value) {
    this->write<int32_t>(addr, bswap32(value));
  }
  inline uint32_t read_u32(uint32_t addr) {
    return bswap32(this->read<uint32_t>(addr));
  }
  inline void write_u32(uint32_t addr, uint32_t value) {
    this->write<uint32_t>(addr, bswap32(value));
  }

  uint32_t allocate(size_t size, bool align_to_end = false);
  void free(uint32_t addr);

  void set_symbol_addr(const char* name, uint32_t addr);
  uint32_t get_symbol_addr(const char* name);

  void print_state(FILE* stream) const;

private:
  size_t page_size;
  uint8_t page_bits;

  std::map<uint32_t, uint32_t> allocated_regions_by_addr;

  std::map<uint32_t, uint32_t> allocated_page_regions_by_index;
  std::map<uint32_t, uint32_t> free_page_regions_by_count;

  std::map<uint32_t, uint32_t> free_regions_by_addr;
  std::map<uint32_t, uint32_t> free_regions_by_size;
  std::unordered_map<std::string, uint32_t> symbol_addrs;

  std::vector<void*> page_host_addrs;
};
