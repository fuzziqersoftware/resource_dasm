#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Image.hh>


#define RESOURCE_TYPE_CICN  0x6369636E
#define RESOURCE_TYPE_PICT  0x50494354
#define RESOURCE_TYPE_SND   0x736E6420
#define RESOURCE_TYPE_TEXT  0x54455854
#define RESOURCE_TYPE_STRN  0x53545223

void load_resource_from_file(const char* filename, uint32_t resource_type,
    int16_t resource_id, void** data, size_t* size);
std::vector<std::pair<uint32_t, int16_t>> enum_file_resources(
    const char* filename);
Image decode_cicn(const void* data, size_t size, uint8_t r, uint8_t g,
    uint8_t b);
Image decode_pict(const void* data, size_t size);
std::vector<uint8_t> decode_snd(const void* data, size_t size);
std::vector<std::string> decode_strN(const void* data, size_t size);
