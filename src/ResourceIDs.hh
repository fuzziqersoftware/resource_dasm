#pragma once

#include <stdint.h>

#include <algorithm>
#include <bitset>

constexpr int MIN_RES_ID = -32768;
constexpr int MAX_RES_ID = 32767;

// A set of resource IDs
class ResourceIDs {
public:
  enum class Init {
    ALL,
    NONE,
  };

  explicit ResourceIDs(Init init) : bits() {
    this->reset(init);
  }

  bool operator[](int res_id) const {
    return this->bits.test(res_id - MIN_RES_ID);
  }

  ResourceIDs& operator+=(int res_id) {
    this->bits.set(res_id - MIN_RES_ID);
    return *this;
  }

  ResourceIDs& operator-=(int res_id) {
    this->bits.reset(res_id - MIN_RES_ID);
    return *this;
  }

  ResourceIDs& operator-=(const ResourceIDs& res_ids) {
    this->bits &= ~res_ids.bits;
    return *this;
  }

  void reset(Init init) {
    if (init == Init::ALL) {
      this->bits.set();
    } else {
      this->bits.reset();
    }
  }

  bool empty() const {
    return this->bits.none();
  }

  void print(FILE* file, bool new_line) const;

private:
  std::bitset<MAX_RES_ID - MIN_RES_ID + 1> bits;
};
