#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>

#include <vector>

#include "mc68k.hh"


#define RESOURCE_TYPE_CICN  0x6369636E
#define RESOURCE_TYPE_CURS  0x43555253
#define RESOURCE_TYPE_CRSR  0x63727372
#define RESOURCE_TYPE_DCMP  0x64636D70
#define RESOURCE_TYPE_ICON  0x49434F4E
#define RESOURCE_TYPE_ICL4  0x69636C34
#define RESOURCE_TYPE_ICS4  0x69637334
#define RESOURCE_TYPE_ICL8  0x69636C38
#define RESOURCE_TYPE_ICS8  0x69637338
#define RESOURCE_TYPE_ICNN  0x49434E23
#define RESOURCE_TYPE_ICSN  0x69637323
#define RESOURCE_TYPE_PAT   0x50415420
#define RESOURCE_TYPE_PATN  0x50415423
#define RESOURCE_TYPE_PICT  0x50494354
#define RESOURCE_TYPE_PPAT  0x70706174
#define RESOURCE_TYPE_SICN  0x5349434E
#define RESOURCE_TYPE_SND   0x736E6420
#define RESOURCE_TYPE_TEXT  0x54455854
#define RESOURCE_TYPE_STR   0x53545220
#define RESOURCE_TYPE_STRN  0x53545223
#define RESOURCE_TYPE_MOOV  0x6D6F6F76

std::string string_for_resource_type(uint32_t type);


struct resource_fork_header {
  uint32_t resource_data_offset;
  uint32_t resource_map_offset;
  uint32_t resource_data_size;
  uint32_t resource_map_size;

  void read(int fd, size_t offset);
};

struct resource_map_header {
  uint8_t reserved[16];
  uint32_t reserved_handle;
  uint16_t reserved_file_ref_num;
  uint16_t attributes;
  uint16_t resource_type_list_offset; // relative to start of this struct
  uint16_t resource_name_list_offset; // relative to start of this struct

  void read(int fd, size_t offset);
};

struct resource_type_list_entry {
  uint32_t resource_type;
  uint16_t num_items; // actually num_items - 1
  uint16_t reference_list_offset; // relative to start of type list

  void read(int fd, size_t offset);
};

struct resource_type_list {
  uint16_t num_types; // actually num_types - 1
  std::vector<resource_type_list_entry> entries;

  void read(int fd, size_t offset);
};

struct resource_reference_list_entry {
  int16_t resource_id;
  uint16_t name_offset;
  uint32_t attributes_and_offset; // attr = high 8 bits; offset relative to resource data segment start
  uint32_t reserved;

  void read(int fd, size_t offset);
};


class ResourceFile {
public:
  ResourceFile(const char* filename);

  std::string get_resource_data(uint32_t type, int16_t id,
      bool decompress = true,
      DebuggingMode decompress_debug = DebuggingMode::Disabled);
  bool resource_is_compressed(uint32_t type, int16_t id);
  std::vector<std::pair<uint32_t, int16_t>> all_resources();

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

  static decoded_cicn decode_cicn(const void* data, size_t size);
  static decoded_curs decode_curs(const void* data, size_t size);
  static decoded_crsr decode_crsr(const void* data, size_t size);
  static std::pair<Image, Image> decode_ppat(const void* data, size_t size);
  static Image decode_pat(const void* data, size_t size);
  static std::vector<Image> decode_patN(const void* data, size_t size);
  static std::vector<Image> decode_sicn(const void* data, size_t size);
  static Image decode_icl8(const void* data, size_t size);
  static Image decode_ics8(const void* data, size_t size);
  static Image decode_icl4(const void* data, size_t size);
  static Image decode_ics4(const void* data, size_t size);
  static Image decode_icon(const void* data, size_t size);
  static std::pair<Image, Image> decode_icnN(const void* data, size_t size);
  static std::pair<Image, Image> decode_icsN(const void* data, size_t size);
  static Image decode_pict(const void* data, size_t size);
  static std::vector<uint8_t> decode_snd(const void* data, size_t size);
  static std::pair<std::string, std::string> decode_str(const void* data, size_t size);
  static std::vector<std::string> decode_strN(const void* data, size_t size);
  static std::string decode_text(const void* data, size_t size);

  decoded_cicn decode_cicn(int16_t id);
  decoded_curs decode_curs(int16_t id);
  decoded_crsr decode_crsr(int16_t id);
  std::pair<Image, Image> decode_ppat(int16_t id);
  Image decode_pat(int16_t id);
  std::vector<Image> decode_patN(int16_t id);
  std::vector<Image> decode_sicn(int16_t id);
  Image decode_icl8(int16_t id);
  Image decode_ics8(int16_t id);
  Image decode_icl4(int16_t id);
  Image decode_ics4(int16_t id);
  Image decode_icon(int16_t id);
  std::pair<Image, Image> decode_icnN(int16_t id);
  std::pair<Image, Image> decode_icsN(int16_t id);
  Image decode_pict(int16_t id);
  std::vector<uint8_t> decode_snd(int16_t id);
  std::pair<std::string, std::string> decode_str(int16_t id);
  std::vector<std::string> decode_strN(int16_t id);
  std::string decode_text(int16_t id);

private:
  scoped_fd fd;

  resource_fork_header header;
  resource_map_header map_header;
  resource_type_list map_type_list;
  std::unordered_map<uint32_t, std::vector<resource_reference_list_entry>> reference_list_cache;

  std::vector<resource_reference_list_entry>* get_reference_list(uint32_t type);
  std::string decompress_resource(const std::string& data,
      DebuggingMode debug = DebuggingMode::Disabled);
  static const std::string& get_system_decompressor(int16_t resource_id);
};

