#pragma once

#include <bitset>
#include <stdint.h>

constexpr int MIN_RES_ID = -32768;
constexpr int MAX_RES_ID = 32767;


// A set of resource IDs
class ResourceIDs {
  public:
    explicit ResourceIDs(bool all_included = false) : bits() {
      if (all_included) {
        this->bits.set();
      }
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
    
    void reset(bool all_included) {
      if (all_included) {
        this->bits.set();
      } else {
        this->bits.reset();
      }
    }
    
    void print(FILE* file, bool new_line) const;
    
  private:
    std::bitset<MAX_RES_ID - MIN_RES_ID + 1>  bits;
};
