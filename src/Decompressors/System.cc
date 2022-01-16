#include "System.hh"

#include <phosg/Encoding.hh>

void CompressedResourceHeader::byteswap() {
  this->magic = bswap32(this->magic);
  this->header_size = bswap16(this->header_size);
  this->decompressed_size = bswap32(this->decompressed_size);

  if (this->header_version & 1) {
    this->header9.dcmp_resource_id = bswap16(this->header9.dcmp_resource_id);
    this->header9.output_extra_bytes = bswap16(this->header9.output_extra_bytes);

  } else {
    this->header8.dcmp_resource_id = bswap16(this->header8.dcmp_resource_id);
  }
}