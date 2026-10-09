#pragma once

#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <bitset>
#include <map>
#include <memory>
#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ResourceDASM {

class MemoryContext {
public:
  explicit MemoryContext(bool strict = false);
  MemoryContext(const MemoryContext&) = delete;
  MemoryContext(MemoryContext&&);
  MemoryContext& operator=(const MemoryContext&) = delete;
  MemoryContext& operator=(MemoryContext&&);
  ~MemoryContext() = default;

  // This isn't a copy constructor because copying a MemoryContext is very expensive, so we don't want to allow the
  // caller to do it accidentally.
  MemoryContext duplicate() const;

  template <typename T, typename AddrT = uint32_t>
    requires(std::is_convertible_v<AddrT, uint32_t> && std::is_convertible_v<uint32_t, AddrT>)
  struct Ptr {
    AddrT addr;

    constexpr Ptr() : addr(0) {}
    constexpr Ptr(std::nullptr_t) : addr(0) {}
    constexpr explicit Ptr(uint32_t addr) : addr(addr) {}
    constexpr Ptr(const Ptr<T>&) = default;
    constexpr Ptr(Ptr<T>&&) = default;
    constexpr Ptr<T>& operator=(const Ptr<T>&) = default;
    constexpr Ptr<T>& operator=(Ptr<T>&&) = default;
    constexpr bool operator==(const Ptr<T>&) const = default;
    constexpr bool operator!=(const Ptr<T>&) const = default;

    template <typename U>
      requires(std::is_convertible_v<T, U>)
    constexpr Ptr(Ptr<U> other) : addr(other.addr()) {}

    constexpr operator uint32_t() const {
      return this->addr;
    }
    constexpr operator Ptr<void>() const {
      return Ptr<void>(this->addr);
    }

    template <typename U>
    constexpr Ptr<U> cast() const {
      return Ptr<U>{this->addr};
    }

    constexpr Ptr<T> operator+(ssize_t count) const {
      return Ptr<T>{this->addr + (count * sizeof(T))};
    }
    constexpr Ptr<T> operator-(ssize_t count) const {
      return Ptr<T>{this->addr + (count * sizeof(T))};
    }
    constexpr Ptr<T>& operator+=(ssize_t count) const {
      this->addr += (count * sizeof(T));
      return *this;
    }
    constexpr Ptr<T>& operator-=(ssize_t count) const {
      this->addr -= (count * sizeof(T));
      return *this;
    }

    // We don't implement operator bool() in order to avoid incorrect implicit conversions to integer types othe than
    // uint32_t
    constexpr bool is_null() const {
      return (this->addr == 0);
    }
  };
  static_assert(sizeof(Ptr<void>) == 4, "MemoryContext::Ptr<void> size is incorrect");

  template <typename T = void, size_t DefaultSize = sizeof(std::conditional_t<std::is_same_v<T, void>, uint8_t, T>)>
  T* at(uint32_t addr, size_t size = DefaultSize, bool skip_strict = false) {
    if (!this->exists(addr, size, skip_strict)) {
      throw std::runtime_error("Attempted to access unallocated memory");
    }
    return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(this->base) + addr);
  }
  template <typename T = void, size_t DefaultSize = sizeof(std::conditional_t<std::is_same_v<T, void>, uint8_t, T>)>
  const T* at(uint32_t addr, size_t size = DefaultSize, bool skip_strict = false) const {
    return const_cast<MemoryContext*>(this)->at<T>(addr, size, skip_strict);
  }
  template <typename T = void, size_t DefaultSize = sizeof(std::conditional_t<std::is_same_v<T, void>, uint8_t, T>)>
  T* at(Ptr<T> addr, bool skip_strict = false) {
    return const_cast<MemoryContext*>(this)->at<T>(addr, DefaultSize, skip_strict);
  }
  template <typename T = void, size_t DefaultSize = sizeof(std::conditional_t<std::is_same_v<T, void>, uint8_t, T>)>
  const T* at(Ptr<T> addr, bool skip_strict = false) const {
    return const_cast<MemoryContext*>(this)->at<T>(addr, DefaultSize, skip_strict);
  }

  template <typename T = void, size_t DefaultSize = sizeof(std::conditional_t<std::is_same_v<T, void>, uint8_t, T>)>
  uint32_t at(const T* host_addr, size_t size = DefaultSize, bool skip_strict = false) const {
    ptrdiff_t addr = reinterpret_cast<const uint8_t*>(host_addr) - reinterpret_cast<const uint8_t*>(this->base);
    if ((addr < 0) || (addr > 0x100000000)) {
      throw std::out_of_range("Host address is not within context space");
    }
    if (!this->exists(addr, size, skip_strict)) {
      throw std::runtime_error("Attempted to access unallocated memory");
    }
    return addr;
  }

  template <typename T>
  T read(uint32_t addr) const {
    return *this->at<T>(addr);
  }
  template <typename T>
  void write(uint32_t addr, const T& obj) {
    *this->at<T>(addr) = obj;
  }

  inline std::string read(uint32_t addr, size_t size) const {
    std::string data(size, '\0');
    this->memcpy(data.data(), addr, size);
    return data;
  }
  inline phosg::StringReader reader(uint32_t addr, size_t size) const {
    return phosg::StringReader(this->at<void>(addr, size), size);
  }
  inline void write(uint32_t addr, const std::string& data) {
    this->memcpy(addr, data.data(), data.size());
  }

  inline int8_t read_s8(uint32_t addr) const {
    return this->read<int8_t>(addr);
  }
  inline void write_s8(uint32_t addr, int8_t value) {
    this->write<int8_t>(addr, value);
  }
  inline uint8_t read_u8(uint32_t addr) const {
    return this->read<uint8_t>(addr);
  }
  inline void write_u8(uint32_t addr, uint8_t value) {
    this->write<uint8_t>(addr, value);
  }
  inline int16_t read_s16b(uint32_t addr) const {
    return this->read<phosg::be_int16_t>(addr);
  }
  inline void write_s16b(uint32_t addr, int16_t value) {
    this->write<phosg::be_int16_t>(addr, value);
  }
  inline int16_t read_s16l(uint32_t addr) const {
    return this->read<phosg::le_int16_t>(addr);
  }
  inline void write_s16l(uint32_t addr, int16_t value) {
    this->write<phosg::le_int16_t>(addr, value);
  }
  inline uint16_t read_u16b(uint32_t addr) const {
    return this->read<phosg::be_uint16_t>(addr);
  }
  inline void write_u16b(uint32_t addr, uint16_t value) {
    this->write<phosg::be_uint16_t>(addr, value);
  }
  inline uint16_t read_u16l(uint32_t addr) const {
    return this->read<phosg::le_uint16_t>(addr);
  }
  inline void write_u16l(uint32_t addr, uint16_t value) {
    this->write<phosg::le_uint16_t>(addr, value);
  }
  inline int32_t read_s32b(uint32_t addr) const {
    return this->read<phosg::be_int32_t>(addr);
  }
  inline void write_s32b(uint32_t addr, int32_t value) {
    this->write<phosg::be_int32_t>(addr, value);
  }
  inline int32_t read_s32l(uint32_t addr) const {
    return this->read<phosg::le_int32_t>(addr);
  }
  inline void write_s32l(uint32_t addr, int32_t value) {
    this->write<phosg::le_int32_t>(addr, value);
  }
  inline uint32_t read_u32b(uint32_t addr) const {
    return this->read<phosg::be_uint32_t>(addr);
  }
  inline void write_u32b(uint32_t addr, uint32_t value) {
    this->write<phosg::be_uint32_t>(addr, value);
  }
  inline uint32_t read_u32l(uint32_t addr) const {
    return this->read<phosg::le_uint32_t>(addr);
  }
  inline void write_u32l(uint32_t addr, uint32_t value) {
    this->write<phosg::le_uint32_t>(addr, value);
  }
  inline int64_t read_s64b(uint32_t addr) const {
    return this->read<phosg::be_int64_t>(addr);
  }
  inline void write_s64b(uint32_t addr, int64_t value) {
    this->write<phosg::be_int64_t>(addr, value);
  }
  inline int64_t read_s64l(uint32_t addr) const {
    return this->read<phosg::le_int64_t>(addr);
  }
  inline void write_s64l(uint32_t addr, int64_t value) {
    this->write<phosg::le_int64_t>(addr, value);
  }
  inline uint64_t read_u64b(uint32_t addr) const {
    return this->read<phosg::be_uint64_t>(addr);
  }
  inline void write_u64b(uint32_t addr, uint64_t value) {
    this->write<phosg::be_uint64_t>(addr, value);
  }
  inline uint64_t read_u64l(uint32_t addr) const {
    return this->read<phosg::le_uint64_t>(addr);
  }
  inline void write_u64l(uint32_t addr, uint64_t value) {
    this->write<phosg::le_uint64_t>(addr, value);
  }

  inline float read_f32b(uint32_t addr) const {
    return this->read<phosg::be_float>(addr);
  }
  inline void write_f32b(uint32_t addr, float value) {
    this->write<phosg::be_float>(addr, value);
  }
  inline float read_f32l(uint32_t addr) const {
    return this->read<phosg::le_float>(addr);
  }
  inline void write_f32l(uint32_t addr, float value) {
    this->write<phosg::le_float>(addr, value);
  }
  inline double read_f64b(uint32_t addr) const {
    return this->read<phosg::be_double>(addr);
  }
  inline void write_f64b(uint32_t addr, double value) {
    this->write<phosg::be_double>(addr, value);
  }
  inline double read_f64l(uint32_t addr) const {
    return this->read<phosg::le_double>(addr);
  }
  inline void write_f64l(uint32_t addr, double value) {
    this->write<phosg::le_double>(addr, value);
  }

  inline std::string read_cstring(uint32_t addr) {
    std::string ret;
    do {
      ret += this->read_s8(addr++);
    } while (ret.back() != '\0');
    ret.resize(ret.size() - 1);
    return ret;
  }
  inline void write_cstring(uint32_t addr, const char* data) {
    this->memcpy(addr, data, strlen(data) + 1);
  }
  inline void write_cstring(uint32_t addr, const std::string& data) {
    this->memcpy(addr, data.c_str(), data.size() + 1);
  }

  inline std::string read_pstring(uint32_t addr) {
    return this->read(addr + 1, this->read_u8(addr));
  }
  inline void write_pstring(uint32_t addr, const std::string& data) {
    if (data.size() > 0xFF) {
      throw std::invalid_argument("string too long for pstring buffer");
    }
    this->write_u8(addr, data.size());
    this->write(addr + 1, data);
  }

  inline void memcpy(uint32_t addr, const void* src, size_t size) {
    ::memcpy(this->at<void>(addr, size), src, size);
  }
  inline void memcpy(void* addr, uint32_t src, size_t size) const {
    ::memcpy(addr, this->at<void>(src, size), size);
  }
  inline void memcpy(uint32_t addr, uint32_t src, size_t size) {
    ::memcpy(this->at<void>(addr, size), this->at<void>(src, size), size);
  }
  inline int memcmp(uint32_t addr, const void* src, size_t size) const {
    return ::memcmp(this->at<void>(addr, size), src, size);
  }
  inline int memcmp(void* addr, uint32_t src, size_t size) const {
    return ::memcmp(addr, this->at<void>(src, size), size);
  }
  inline int memcmp(uint32_t addr, uint32_t src, size_t size) const {
    return ::memcmp(this->at<void>(addr, size), this->at<void>(src, size), size);
  }
  inline void memset(uint32_t addr, uint8_t v, size_t size) {
    ::memset(this->at<void>(addr, size), v, size);
  }

  struct Allocator {
    Allocator() = delete;
    Allocator(MemoryContext* mem, uint32_t addr_low, uint64_t addr_high);
    Allocator(const Allocator&) = delete;
    Allocator(Allocator&&) = default;
    Allocator& operator=(const Allocator&) = delete;
    Allocator& operator=(Allocator&&) = default;

    // Restricts this Allocator to addresses below addr; returns a new Allocator that covers the rest
    Allocator split(uint32_t addr);

    struct Block {
      uint32_t addr;
      uint64_t requested_size;
      uint64_t actual_size;
      std::string str() const;
    };

    uint32_t allocate(size_t size);
    bool allocate_at(uint32_t addr, size_t size);
    bool free(uint32_t addr);

    // Resizes a block by changing its end address; returns true if resized, false if not enough space
    bool resize(uint32_t addr, size_t new_size);
    // Resizes a block by changing its start address; returns {success, new_address}
    std::pair<bool, uint32_t> resize_reverse(uint32_t addr, size_t new_size);

    size_t get_block_size(uint32_t addr) const;
    bool exists(uint32_t addr, size_t size = 1) const; // Returns true if entire range is allocated in the same region

    inline std::pair<uint32_t, uint64_t> range() const {
      return {this->addr_low, this->addr_high};
    }
    inline const std::map<uint32_t, Block>& all_blocks() const {
      return this->allocated_blocks;
    }

    inline size_t total_allocated_bytes() const {
      return this->allocated_bytes;
    }
    inline size_t total_free_bytes() const {
      return (this->addr_high - this->addr_low) - this->allocated_bytes;
    }
    inline size_t max_contiguous_free_space() const {
      auto it = this->free_blocks_by_size.rbegin();
      return (it != this->free_blocks_by_size.rend()) ? it->first : 0;
    }

    static Allocator import_state(MemoryContext* mem, FILE* stream);
    void export_state(FILE* stream) const;

    void print_state(FILE* stream) const;
    void verify() const;
    [[noreturn]] void verify_failed(const std::string& what) const;

    void add_free_block(uint32_t addr, size_t size);
    std::map<uint32_t, Block>::iterator delete_free_block(std::map<uint32_t, Block>::iterator it);
    std::multimap<size_t, Block*>::iterator delete_free_block(std::multimap<size_t, Block*>::iterator it);

    void reconstruct_free_maps();

    MemoryContext* mem;
    uint32_t addr_low;
    uint64_t addr_high;
    size_t allocated_bytes = 0;
    std::map<uint32_t, Block> allocated_blocks;
    std::multimap<size_t, Block*> free_blocks_by_size; // References into free_blocks_by_addr; keyed on actual_size
    std::map<uint32_t, Block> free_blocks_by_addr;
  };

  Allocator& get_or_split_allocator(uint32_t addr_low, uint32_t addr_high);
  Allocator& get_allocator(uint32_t addr);
  void split_allocators(uint32_t addr);
  inline const std::map<uint32_t, Allocator>& all_allocators() const {
    return this->allocators;
  }

  inline const Allocator& get_allocator(uint32_t addr) const {
    return const_cast<MemoryContext*>(this)->get_allocator(addr);
  }

  inline uint32_t allocate(size_t size) {
    uint32_t ret = this->get_allocator(0).allocate(size);
    if constexpr (VERIFY_OPERATIONS) {
      this->verify();
    }
    return ret;
  }
  inline bool allocate_at(uint32_t addr, size_t size) {
    bool ret = this->get_allocator(addr).allocate_at(addr, size);
    if constexpr (VERIFY_OPERATIONS) {
      this->verify();
    }
    return ret;
  }
  inline bool free(uint32_t addr) {
    bool ret = this->get_allocator(addr).free(addr);
    if constexpr (VERIFY_OPERATIONS) {
      this->verify();
    }
    return ret;
  }
  inline bool resize(uint32_t addr, size_t new_size) {
    bool ret = this->get_allocator(addr).resize(addr, new_size);
    if constexpr (VERIFY_OPERATIONS) {
      this->verify();
    }
    return ret;
  }
  inline std::pair<bool, uint32_t> resize_reverse(uint32_t addr, size_t new_size) {
    auto ret = this->get_allocator(addr).resize_reverse(addr, new_size);
    if constexpr (VERIFY_OPERATIONS) {
      this->verify();
    }
    return ret;
  }
  inline size_t get_block_size(uint32_t addr) const {
    return this->get_allocator(addr).get_block_size(addr);
  }
  inline bool allocated(uint32_t addr, size_t size = 1) const {
    return this->get_allocator(addr).exists(addr, size);
  }

  // Typed versions of the above functions
  template <typename T>
  Ptr<T> allocate() {
    return Ptr<T>{this->allocate(sizeof(T))};
  }
  template <typename T>
  bool allocate_at(Ptr<T> obj) {
    return this->allocate_at(obj.addr, sizeof(T));
  }
  template <typename T>
  void free(Ptr<T> addr) {
    return this->free(addr.addr);
  }
  template <typename T>
  void exists(Ptr<T> addr) {
    return this->exists(addr.addr, sizeof(T));
  }

  // Returns true if ALL of the <size> bytes starting at <addr> are accessible
  bool exists(uint32_t addr, size_t size = 1, bool skip_strict = false) const;

  void set_symbol_addr(const std::string& name, uint32_t addr);
  void delete_symbol(const std::string& name);
  void delete_symbol(uint32_t addr);
  uint32_t get_symbol_addr(const std::string& name) const;
  const std::string& get_symbol_at_addr(uint32_t addr) const;
  const std::unordered_map<std::string, uint32_t> all_symbols() const;

  static MemoryContext import_state(FILE* stream);
  void export_state(FILE* stream) const;

  void print_state(FILE* stream) const;
  void verify() const;
  [[noreturn]] void verify_failed(const std::string& what) const;

private:
  static constexpr bool VERIFY_OPERATIONS = false; // Enable for debugging
  static constexpr size_t PAGE_BITS = 16; // 64KB pages, 65536 of them
  static constexpr size_t PAGE_SIZE = (1ULL << PAGE_BITS);
  static constexpr size_t PAGE_COUNT = (1ULL << (32 - PAGE_BITS));
  static constexpr size_t TOTAL_SIZE = (1ULL << 32);

  bool strict = false;

  void* base;
  std::array<uint8_t, (PAGE_COUNT >> 3)> pages_valid;

  std::map<uint32_t, Allocator> allocators;

  std::unordered_map<std::string, uint32_t> symbol_addrs;
  std::unordered_map<uint32_t, std::string> addr_symbols;

  inline bool page_is_valid(size_t page_num) const {
    return this->pages_valid[page_num >> 3] & (0x80 >> (page_num & 7));
  }
  void make_pages_valid(uint32_t addr, uint32_t size);
  bool check_pages_valid(uint32_t addr, uint32_t size) const;

  inline uint32_t page_base_for_addr(uint32_t addr) const {
    return (addr & ~(this->PAGE_SIZE - 1));
  }

  inline uint32_t page_number_for_addr(uint32_t addr) const {
    return this->page_base_for_addr(addr) >> this->PAGE_BITS;
  }

  inline uint32_t addr_for_page_number(uint32_t page_num) const {
    return page_num << this->PAGE_BITS;
  }

  inline size_t page_size_for_size(size_t size) const {
    return ((size + (this->PAGE_SIZE - 1)) & ~(this->PAGE_SIZE - 1));
  }

  inline size_t page_count_for_size(size_t size) const {
    return this->page_size_for_size(size) >> this->PAGE_BITS;
  }
};

} // namespace ResourceDASM
