#pragma once

#include <stdint.h>

#include <memory>

#include "ResourceFile.hh"



enum DecompressionFlag {
  DISABLED         = 0x0001, // Don't decompress any resources
  VERBOSE          = 0x0002, // Print state and info while decompressing
  TRACE            = 0x0004, // Print CPU state when running dcmp/ncmp resources
  SKIP_FILE_DCMP   = 0x0008, // Don't use dcmps from context_rf
  SKIP_FILE_NCMP   = 0x0010, // Don't use ncmps from context_rf
  SKIP_SYSTEM_DCMP = 0x0020, // Don't use system dcmp resources
  SKIP_SYSTEM_NCMP = 0x0040, // Don't use system ncmp resources
  SKIP_INTERNAL    = 0x0080, // Don't use internal decompressors
  RETRY            = 0x0100, // Decompress even if res has DECOMPRESSION_FAILED failed
};

std::shared_ptr<const ResourceFile::Resource> get_system_decompressor(
    bool use_ncmp, int16_t resource_id);

void decompress_resource(
    std::shared_ptr<ResourceFile::Resource> res,
    uint64_t flags,
    ResourceFile* context_rf);
