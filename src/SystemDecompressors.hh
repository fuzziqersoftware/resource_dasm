#include <utility>

#include <stddef.h>
#include <stdint.h>



std::pair<const void*, size_t> get_system_decompressor(
    bool use_ncmp, int16_t resource_id);
