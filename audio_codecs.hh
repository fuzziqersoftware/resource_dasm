#include <stdint.h>

#include <vector>

std::vector<int16_t> decode_mace(const uint8_t* data, size_t size, bool stereo,
    bool is_mace3);
std::vector<int16_t> decode_ima4(const uint8_t* data, size_t size, bool stereo);
std::vector<int16_t> decode_alaw(const uint8_t* data, size_t size);
std::vector<int16_t> decode_ulaw(const uint8_t* data, size_t size);
