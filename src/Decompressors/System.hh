#include <stdint.h>

#include <string>
#include <phosg/Encoding.hh>

struct CompressedResourceHeader {
  be_uint32_t magic; // 0xA89F6572
  be_uint16_t header_size; // may be zero apparently
  uint8_t header_version; // 8 or 9
  uint8_t attributes; // bit 0 specifies compression
  be_uint32_t decompressed_size;

  union {
    struct {
      uint8_t working_buffer_fractional_size; // length of compressed data relative to length of uncompressed data, out of 256
      uint8_t output_extra_bytes;
      be_int16_t dcmp_resource_id;
      // TODO: Do some decompressors use these bytes as extra parameters? So far
      // I haven't seen any that use header8 and use these bytes.
      be_uint16_t unused;
    } header8;

    struct {
      be_uint16_t dcmp_resource_id;
      be_uint16_t output_extra_bytes;
      // Some decompressors use these bytes as extra parameters; for example,
      // System dcmp 2 uses them to specify the presence and size of an extra
      // const words table.
      uint8_t param1;
      uint8_t param2;
    } header9;
  };
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
