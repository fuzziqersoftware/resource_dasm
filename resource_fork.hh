#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Image.hh>


#define RESOURCE_TYPE_CICN  0x6369636E
#define RESOURCE_TYPE_CURS  0x43555253
#define RESOURCE_TYPE_CRSR  0x63727372
#define RESOURCE_TYPE_ICON  0x49434F4E
#define RESOURCE_TYPE_ICL4  0x69636C34
#define RESOURCE_TYPE_ICS4  0x69637334
#define RESOURCE_TYPE_ICL8  0x69636C38
#define RESOURCE_TYPE_ICS8  0x69637338
#define RESOURCE_TYPE_ICNN  0x49434E23
#define RESOURCE_TYPE_ICSN  0x69637323
#define RESOURCE_TYPE_PICT  0x50494354
#define RESOURCE_TYPE_SND   0x736E6420
#define RESOURCE_TYPE_TEXT  0x54455854
#define RESOURCE_TYPE_STR   0x53545220
#define RESOURCE_TYPE_STRN  0x53545223
#define RESOURCE_TYPE_MOOV  0x6D6F6F76

std::string load_resource_from_file(const char* filename,
    uint32_t resource_type, int16_t resource_id);
std::vector<std::pair<uint32_t, int16_t>> enum_file_resources(
    const char* filename);

struct decoded_cicn {
  Image image;
  Image bitmap;
  Image mask;

  decoded_cicn(Image&& image, Image&& bitmap, Image&& mask);
};

struct decoded_curs {
  Image bitmap;
  Image mask;
  uint16_t hotspot_x;
  uint16_t hotspot_y;

  decoded_curs(Image&& bitmap, Image&& mask, uint16_t x, uint16_t y);
};

struct decoded_crsr {
  Image image;
  Image bitmap;
  Image mask;
  uint16_t hotspot_x;
  uint16_t hotspot_y;

  decoded_crsr(Image&& image, Image&& bitmap, Image&& mask, uint16_t x,
      uint16_t y);
};

decoded_cicn decode_cicn(const void* data, size_t size);
decoded_curs decode_curs(const void* data, size_t size);
decoded_crsr decode_crsr(const void* data, size_t size);
Image decode_icl8(const void* data, size_t size);
Image decode_ics8(const void* data, size_t size);
Image decode_icl4(const void* data, size_t size);
Image decode_ics4(const void* data, size_t size);
Image decode_icon(const void* data, size_t size);
std::pair<Image, Image> decode_icnN(const void* vdata, size_t size);
std::pair<Image, Image> decode_icsN(const void* vdata, size_t size);
Image decode_pict(const void* data, size_t size);
std::vector<uint8_t> decode_snd(const void* data, size_t size);
std::pair<std::string, std::string> decode_str(const void* vdata, size_t size);
std::vector<std::string> decode_strN(const void* data, size_t size);
std::string decode_text(const void* data, size_t size);
