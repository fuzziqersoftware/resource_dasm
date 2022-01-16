#include <stdint.h>

#include <string>

struct CompressedResourceHeader {
  uint32_t magic; // 0xA89F6572
  uint16_t header_size; // may be zero apparently
  uint8_t header_version; // 8 or 9
  uint8_t attributes; // bit 0 specifies compression
  uint32_t decompressed_size;

  union {
    struct {
      uint8_t working_buffer_fractional_size; // length of compressed data relative to length of uncompressed data, out of 256
      uint8_t output_extra_bytes;
      int16_t dcmp_resource_id;
      uint16_t unused;
    } header8;

    struct {
      uint16_t dcmp_resource_id;
      uint16_t output_extra_bytes;
      // Some decompressors use these bytes as extra parameters; for example,
      // System dcmp 2 uses them to specify the presence and size of an extra
      // const words table.
      uint8_t param1;
      uint8_t param2;
    } header9;
  };

  void byteswap();
} __attribute__((packed));

std::string decompress_system0(
    const CompressedResourceHeader& header,
    const void* source,
    size_t size);
std::string decompress_system1(
    const CompressedResourceHeader& header,
    const void* source,
    size_t size);
std::string decompress_system2(
    const CompressedResourceHeader& header,
    const void* source,
    size_t size);
std::string decompress_system3(
    const CompressedResourceHeader& header,
    const void* source,
    size_t size);
