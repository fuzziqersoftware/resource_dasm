#include "Instrument.hh"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <format>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <vector>

#include "QuickTimeInstrument.hh"

namespace ResourceDASM {
namespace Audio {

static constexpr uint32_t SSAI_TYPE = 0x73736169; // 'ssai'
static constexpr uint32_t SEAN_TYPE = 0x7365616E; // 'sean'
static constexpr uint32_t TONE_TYPE = 0x746F6E65; // 'tone'
static constexpr uint32_t KNBL_TYPE = 0x6B6E626C; // 'knbl'
static constexpr uint32_t SINF_TYPE = 0x73696E66; // 'sinf'
static constexpr uint32_t SDSC_TYPE = 0x73647363; // 'sdsc'
// static constexpr uint32_t SMIN_TYPE = 0x736D696E; // 'smin'
static constexpr uint32_t SDAT_TYPE = 0x73646174; // 'sdat'
// static constexpr uint32_t QUAL_TYPE = 0x7175616C; // 'qual'
// static constexpr uint32_t IINF_TYPE = 0x69696E66; // 'iinf'
// static constexpr uint32_t COPYRIGHT_WRT_TYPE = 0xA9777274; // '©wrt' (in MacRoman)
// static constexpr uint32_t COPYRIGHT_CPY_TYPE = 0xA9637079; // '©cpy' (in MacRoman)
// static constexpr uint32_t STR_TYPE = 0x73747220; // 'str '

struct FileHeader {
  /* 00 */ phosg::be_uint32_t size = 0; // Includes this header
  /* 04 */ phosg::be_uint32_t type = 0;
  /* 08 */ phosg::be_uint32_t block_number = 0;
  /* 0C */
} __attribute__((packed));

struct BlockHeader {
  /* 00 */ phosg::be_uint32_t size = 0; // Includes this header
  /* 04 */ phosg::be_uint32_t type = 0;
  /* 08 */ phosg::be_uint32_t block_number = 0;
  /* 0C */ phosg::be_uint32_t child_count = 0;
  /* 10 */ phosg::be_uint32_t unknown_a1 = 0;
  /* 14 */
} __attribute__((packed));

struct ToneBlock {
  /* 14 */ phosg::be_uint32_t unknown_a1[9] = {};
  /* 38 */ uint8_t name[0x20] = {}; // pstring; size is uncertain (may be shorter)
  /* 58 */ phosg::be_uint32_t unknown_a2[2] = {};
  /* 60 */
} __attribute__((packed));

struct KNBLBlock { // Knob list?
  struct Entry {
    uint8_t unknown_a1 = 0;
    uint8_t unknown_a2 = 0;
    phosg::be_uint16_t unknown_a3 = 0;
    phosg::be_uint32_t unknown_a4 = 0;
  } __attribute__((packed));

  /* 14 */ phosg::be_uint32_t entry_count = 0;
  /* 18 */ phosg::be_uint32_t unknown_a3 = 0;
  /* 1C */ // Entries follow here
} __attribute__((packed));

struct SDSCBlock { // Sample description
  /* 14 */ phosg::be_uint32_t format = 0; // E.g. 'raw '
  /* 18 */ phosg::be_uint16_t num_channels = 0;
  /* 1A */ phosg::be_uint16_t bits_per_sample = 0;
  /* 1C */ phosg::be_uint16_t sample_rate_integer = 0; // Whole number part of a Fixed
  /* 1E */ phosg::be_uint16_t sample_rate_fractional = 0; // Fractional part of a Fixed
  /* 20 */ phosg::be_uint16_t sdat_block_number = 0; // Block number of relevant sdat block
  /* 22 */ phosg::be_uint32_t unknown_a4 = 0;
  /* 26 */ phosg::be_uint32_t frame_count = 0; // TODO: Could also be sample_count or just num_sample_bytes
  /* 2A */ phosg::be_uint32_t unknown_a5 = 0;
  /* 2E */ phosg::be_uint32_t loop_start_offset = 0; // TODO: Could be in frames, samples, or bytes; we assume frames
  /* 32 */ phosg::be_uint32_t loop_end_offset = 0; // TODO: Could be in frames, samples, or bytes; we assume frames
  /* 36 */ phosg::be_uint32_t base_note = 0;
  /* 3A */ phosg::be_uint32_t key_low = 0;
  /* 3E */ phosg::be_uint32_t key_high = 0;
  /* 42 */
} __attribute__((packed));

struct QualBlock {
  /* 14 */ uint8_t unknown_a3[4] = {};
  /* 18 */
} __attribute__((packed));

template <size_t BufSize>
std::string decode_pstring(const uint8_t* data) {
  if (*data > (BufSize - 1)) {
    throw std::runtime_error("Pascal string overflows buffer");
  }
  return std::string(reinterpret_cast<const char*>(data + 1), *data);
}

SSAIInstrument::SSAIInstrument(const void* data, size_t size) {
  phosg::StringReader r(data, size);

  const auto& root_header = r.get<FileHeader>();
  if (root_header.type != SSAI_TYPE) {
    throw std::runtime_error("Input is not an ssai file");
  }
  if (root_header.size != r.size()) {
    throw std::runtime_error("Header size field does not match file size");
  }

  this->parse_blocks(r, 1, nullptr);
}

template <typename T>
static const T& get_fixed_block(phosg::StringReader& r, uint32_t block_type) {
  if (r.remaining() != sizeof(T)) {
    throw std::runtime_error(std::format("{:08X} block size is incorrect", block_type));
  }
  return r.get<T>();
}

void SSAIInstrument::parse_blocks(phosg::StringReader& r, size_t block_count, KeyRegion* current_key_region) {
  for (; block_count > 0; block_count--) {
    const auto& header = r.get<BlockHeader>();
    if (header.size < sizeof(BlockHeader)) {
      throw std::runtime_error("Invalid block header size");
    }
    this->parse_block(
        header.type,
        header.block_number,
        header.child_count,
        r.sub(r.where(), header.size - sizeof(header)),
        current_key_region);
    r.skip(header.size - sizeof(header));
  }
}

void SSAIInstrument::parse_block(
    uint32_t block_type,
    uint32_t block_number,
    uint32_t child_count,
    phosg::StringReader r,
    KeyRegion* current_key_region) {
  switch (block_type) {
    case SEAN_TYPE:
      this->parse_blocks(r, child_count, current_key_region);
      break;

    case SINF_TYPE:
      this->parse_blocks(r, child_count, &this->key_regions[block_number]);
      break;

    case TONE_TYPE: {
      const auto& tone_block = get_fixed_block<ToneBlock>(r, block_type);
      // TODO: There might be other important stuff in ToneBlock too
      this->name = decode_pstring<0x20>(tone_block.name);
      break;
    }

    case KNBL_TYPE: {
      const auto& knbl_block = r.get<KNBLBlock>();
      auto* knob_list = current_key_region ? &current_key_region->knobs : &this->knobs;
      if (!knob_list->empty()) {
        throw std::runtime_error("Received multiple knob lists in same context");
      }
      for (size_t z = 0; z < knbl_block.entry_count; z++) {
        const auto& entry = r.get<KNBLBlock::Entry>();
        knob_list->emplace_back(KnobEntry{entry.unknown_a1, entry.unknown_a2, entry.unknown_a3, entry.unknown_a4});
      }
      break;
    }

    case SDSC_TYPE: {
      const auto& sdsc = r.get<SDSCBlock>();
      auto& rgn = this->key_regions[sdsc.sdat_block_number];
      rgn.format = sdsc.format;
      rgn.num_channels = sdsc.num_channels;
      rgn.bits_per_sample = sdsc.bits_per_sample;
      rgn.sample_rate = sdsc.sample_rate_integer + (static_cast<float>(sdsc.sample_rate_fractional) / 0x10000);
      rgn.sample_data_number = sdsc.sdat_block_number;
      rgn.frame_count = sdsc.frame_count;
      rgn.loop_start_offset = sdsc.loop_start_offset;
      rgn.loop_end_offset = sdsc.loop_end_offset;
      rgn.base_note = sdsc.base_note;
      rgn.key_low = sdsc.key_low;
      rgn.key_high = sdsc.key_high;
      break;
    }

    case SDAT_TYPE: {
      if (!this->sample_datas.emplace(block_number, r.read(r.remaining())).second) {
        throw std::runtime_error("Received multiple sdat blocks for the same key region");
      }
      break;
    }

    // TODO: Re-implement these
    // case SMIN_TYPE: {
    //   if (block_header.size < sizeof(BlockHeader) + sizeof(BlockHeader)) {
    //     throw std::runtime_error("smin block is too small");
    //   }
    //   if (block_header.child_count != 1) {
    //     throw std::runtime_error("smin block has incorrect child count");
    //   }
    //   const auto& sdat_header = r.get<BlockHeader>(SDAT_TYPE);
    //   if (sdat_header.child_count > 0) {
    //     throw std::runtime_error("sdat child count is incorrect");
    //   }
    //   size_t data_size = sdat_header.size - sizeof(BlockHeader);
    //   const auto* data = r.getv(data_size);
    //   TODO; // Do something with sample data
    //   break;
    // }
    // case QUAL_TYPE:
    //   get_fixed_block<QualBlock>(r, block_header);
    //   break;
    // case IINF_TYPE: {
    //   auto iinf_r = r.sub(r.where(), block_header.size - sizeof(BlockHeader));
    //   while (!iinf_r.eof()) {
    //     const auto& block_header = iinf_r.get<BlockHeader>();
    //     switch (block_header.type) {
    //       case COPYRIGHT_WRT_TYPE:
    //         phosg::fwrite_fmt(stdout, "Copyright WRT: {}\n", r.read(block_header.size));
    //         break;
    //       case COPYRIGHT_CPY_TYPE:
    //         phosg::fwrite_fmt(stdout, "Copyright CPY: {}\n", r.read(block_header.size));
    //         break;
    //       case STR_TYPE:
    //         phosg::fwrite_fmt(stdout, "Info string: {}\n", r.read(block_header.size));
    //         break;
    //       default:
    //         throw std::runtime_error(std::format("Unknown iinf child block: {:08X}", block_header.type));
    //     }
    //   }
    //   break;
    // }
    default:
      throw std::runtime_error(std::format("Unknown root block: {:08X}", block_type));
  }
}

} // namespace Audio
} // namespace ResourceDASM
