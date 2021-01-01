#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <phosg/Encoding.hh>


class MemoryContext {
public:
  MemoryContext(size_t size);
  ~MemoryContext();

  inline uint32_t at(const void* ptr) {
    ptrdiff_t d = reinterpret_cast<ptrdiff_t>(ptr) - reinterpret_cast<ptrdiff_t>(this->base_ptr);
    if (d < 0 || d >= static_cast<ssize_t>(this->size_bytes)) {
      throw std::out_of_range("address out of range");
    }
    if (d < 0x4000) {
      fprintf(stderr, "[MemoryContext] low-memory address conversion at %08lX\n", d);
    }
    return d;
  }
  inline void* at(uint32_t addr) {
    if (addr >= this->size_bytes) {
      throw std::out_of_range("address out of range");
    }
    return reinterpret_cast<uint8_t*>(this->base_ptr) + addr;
  }
  template <typename T>
  T* obj(uint32_t addr, uint32_t size = sizeof(T)) {
    if (addr + size > this->size_bytes) {
      throw std::out_of_range("address out of range");
    }
    if (addr < 0x4000) {
      fprintf(stderr, "[MemoryContext] low-memory address conversion for access at %08X (%u bytes)\n", addr, size);
    }
    return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(this->base_ptr) + addr);
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

  void* base();
  size_t size();

  void* alloc(size_t size, bool align_to_end = false);
  void free(void* ptr);
  void free(uint32_t addr);

  template <typename T>
  T* alloc_obj(size_t size = sizeof(T), bool align_to_end = false) {
    return reinterpret_cast<T*>(this->alloc(size, align_to_end));
  }

  void set_symbol_addr(const char* name, uint32_t addr);
  uint32_t get_symbol_addr(const char* name);

private:
  std::map<uint32_t, uint32_t> allocated_regions_by_addr;
  std::map<uint32_t, uint32_t> free_regions_by_addr;
  std::map<uint32_t, uint32_t> free_regions_by_size;
  std::unordered_map<std::string, uint32_t> symbol_addrs;

  void* base_ptr;
  size_t size_bytes;
};
