#include <stdint.h>
#include <stddef.h>

#include <vector>
#include <phosg/Encoding.hh>

std::vector<le_int16_t> decode_mace(const uint8_t* data, size_t size,
    bool stereo, bool is_mace3);
std::vector<le_int16_t> decode_ima4(const uint8_t* data, size_t size,
    bool stereo);
std::vector<le_int16_t> decode_alaw(const uint8_t* data, size_t size);
std::vector<le_int16_t> decode_ulaw(const uint8_t* data, size_t size);
