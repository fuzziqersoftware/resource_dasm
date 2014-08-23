#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "Image.hh"

using namespace std;

#define RESOURCE_TYPE_CICN  0x6369636E
#define RESOURCE_TYPE_PICT  0x50494354
#define RESOURCE_TYPE_SND   0x736E6420

void load_resource_from_file(const char* filename, uint32_t resource_type,
    int16_t resource_id, void** data, size_t* size);
vector<pair<uint32_t, int16_t>> enum_file_resources(const char* filename);
Image decode_cicn32(void* data, size_t size, uint8_t r, uint8_t g, uint8_t b);
Image decode_pict(void* data, size_t size);
vector<uint8_t> decode_snd(void* data, size_t size);
