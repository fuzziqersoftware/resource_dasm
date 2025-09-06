#include "AAFArchive.hh"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <format>
#include <map>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <unordered_map>
#include <vector>

#include "Instrument.hh"
#include "WAVFile.hh"

using namespace std;

namespace ResourceDASM {
namespace Audio {

struct WaveTableEntry {
  uint8_t unknown1;
  uint8_t type;
  uint8_t base_note;
  uint8_t unknown2;
  phosg::be_uint32_t flags2;
  phosg::be_uint32_t offset;
  phosg::be_uint32_t size;
  phosg::be_uint32_t loop_flag; // 0xFFFFFFFF means has a loop
  phosg::be_uint32_t loop_start;
  phosg::be_uint32_t loop_end;
  phosg::be_uint32_t unknown3[4];

  inline uint32_t sample_rate() const {
    return (this->flags2 >> 9) & 0x0000FFFF;
  }
} __attribute__((packed));

struct AWFileEntry {
  char filename[112];
  phosg::be_uint32_t wav_count;
  phosg::be_uint32_t wav_entry_offsets[0];
} __attribute__((packed));

struct WINFHeader {
  phosg::be_uint32_t magic; // 'WINF'
  phosg::be_uint32_t aw_file_count;
  phosg::be_uint32_t aw_file_entry_offsets[0];
} __attribute__((packed));

struct CDFRecord {
  phosg::be_uint16_t aw_file_index;
  phosg::be_uint16_t sound_id;
  phosg::be_uint32_t unknown1[13];
} __attribute__((packed));

struct CDFHeader {
  phosg::be_uint32_t magic; // 'C-DF'
  phosg::be_uint32_t record_count;
  phosg::be_uint32_t record_offsets[0];
} __attribute__((packed));

struct SCNEHeader {
  phosg::be_uint32_t magic; // 'SCNE'
  phosg::be_uint32_t unknown1[2];
  phosg::be_uint32_t cdf_offset;
} __attribute__((packed));

struct WBCTHeader {
  phosg::be_uint32_t magic; // 'WBCT'
  phosg::be_uint32_t unknown1;
  phosg::be_uint32_t scne_count;
  phosg::be_uint32_t scne_offsets[0];
} __attribute__((packed));

struct WSYSHeader {
  phosg::be_uint32_t magic; // 'WSYS'
  phosg::be_uint32_t size;
  phosg::be_uint32_t wsys_id;
  phosg::be_uint32_t unknown1;
  phosg::be_uint32_t winf_offset;
  phosg::be_uint32_t wbct_offset;
} __attribute__((packed));

pair<uint32_t, vector<Sound>> wsys_decode(const void* vdata, const char* base_directory) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  const WSYSHeader* wsys = reinterpret_cast<const WSYSHeader*>(data);
  if (wsys->magic != 0x57535953) {
    throw invalid_argument("WSYS file not at expected offset");
  }

  const WINFHeader* winf = reinterpret_cast<const WINFHeader*>(data + wsys->winf_offset);
  if (winf->magic != 0x57494E46) {
    throw invalid_argument("WINF file not at expected offset");
  }

  // get all sample IDs before processing aw files
  // this map is {(aw_file_index, wave_table_entry_index): sound_id}
  map<pair<size_t, size_t>, size_t> aw_file_and_sound_index_to_cdf_id;

  const WBCTHeader* wbct = reinterpret_cast<const WBCTHeader*>(data + wsys->wbct_offset);
  if (wbct->magic != 0x57424354) {
    throw invalid_argument("WBCT file not at expected offset");
  }

  for (size_t x = 0; x < wbct->scne_count; x++) {
    const SCNEHeader* scne = reinterpret_cast<const SCNEHeader*>(data + wbct->scne_offsets[x]);
    if (scne->magic != 0x53434E45) {
      throw invalid_argument("SCNE file not at expected offset");
    }

    const CDFHeader* cdf = reinterpret_cast<const CDFHeader*>(data + scne->cdf_offset);
    if (cdf->magic != 0x432D4446) {
      throw invalid_argument("C-DF file not at expected offset");
    }

    for (size_t y = 0; y < cdf->record_count; y++) {
      const CDFRecord* record = reinterpret_cast<const CDFRecord*>(data + cdf->record_offsets[y]);
      phosg::fwrite_fmt(stderr, "[SoundEnvironment/debug] CDF {} => {},{} => {}\n",
          y, record->aw_file_index.load(), y, record->sound_id.load());
      if (!aw_file_and_sound_index_to_cdf_id.emplace(make_pair(record->aw_file_index, y), record->sound_id).second) {
        phosg::fwrite_fmt(stderr, "[SoundEnvironment] warning: duplicate sound ID: {},{} => {}\n",
            record->aw_file_index.load(), y, record->sound_id.load());
      }
    }
  }

  // now process aw files
  vector<Sound> ret;
  for (size_t x = 0; x < winf->aw_file_count; x++) {
    const AWFileEntry* entry = reinterpret_cast<const AWFileEntry*>(data + winf->aw_file_entry_offsets[x]);

    // pikmin has a case where the aw filename is blank and the entry count is
    // zero. wtf? just handle it manually I guess
    if (entry->wav_count == 0) {
      continue;
    }

    string aw_file_contents;

    // try both Banks and Waves subdirectories
    static const vector<string> directory_names({"Banks", "Waves"});
    for (const auto& directory_name : directory_names) {
      string aw_filename = format("{}/{}/{}", base_directory, directory_name, entry->filename);
      try {
        aw_file_contents = phosg::load_file(aw_filename.c_str());
        break;
      } catch (const phosg::cannot_open_file&) {
        continue;
      }
    }
    if (aw_file_contents.empty()) {
      throw runtime_error(format("{} does not exist in any checked subdirectory", entry->filename));
    }

    for (size_t y = 0; y < entry->wav_count; y++) {
      const WaveTableEntry* wav_entry = reinterpret_cast<const WaveTableEntry*>(data + entry->wav_entry_offsets[y]);

      uint16_t sound_id = aw_file_and_sound_index_to_cdf_id.at(make_pair(x, y));

      ret.emplace_back();
      Sound& ret_snd = ret.back();
      ret_snd.sample_rate = wav_entry->sample_rate();
      ret_snd.base_note = wav_entry->base_note;
      if (wav_entry->loop_flag == 0xFFFFFFFF) {
        ret_snd.loop_start = wav_entry->loop_start;
        ret_snd.loop_end = wav_entry->loop_end;
      } else {
        ret_snd.loop_start = 0;
        ret_snd.loop_end = 0;
      }

      ret_snd.source_filename = entry->filename;
      ret_snd.source_offset = wav_entry->offset;
      ret_snd.source_size = wav_entry->size;

      ret_snd.aw_file_index = x;
      ret_snd.wave_table_index = y;
      ret_snd.sound_id = sound_id;

      if (wav_entry->type < 2) {
        ret_snd.afc_data = string(aw_file_contents.data() + wav_entry->offset, wav_entry->size);
        ret_snd.afc_large_frames = (wav_entry->type == 1);
        ret_snd.num_channels = 1;
      } else if (wav_entry->type < 4) {
        // uncompressed big-endian mono/stereo apparently
        bool is_stereo = (wav_entry->type == 3);
        if (is_stereo && (wav_entry->size & 3)) {
          throw invalid_argument("stereo data size not a multiple of 4");
        } else if (!is_stereo && (wav_entry->size & 2)) {
          throw invalid_argument("mono data size not a multiple of 2");
        }

        // hack: type 2 are too fast, so half their sample rate. I suspect they
        // might be stereo also, but then why are they a different type?
        if (wav_entry->type == 2) {
          ret_snd.sample_rate /= 2;
        }

        size_t num_samples = wav_entry->size / 2; // 16-bit samples
        ret_snd.decoded_samples.reserve(num_samples);
        const phosg::be_int16_t* samples = reinterpret_cast<const phosg::be_int16_t*>(
            aw_file_contents.data() + wav_entry->offset);
        for (size_t z = 0; z < num_samples; z++) {
          int16_t sample = samples[z];
          float decoded = (sample == -0x8000) ? -1.0 : (static_cast<float>(sample) / 32767.0f);
          ret_snd.decoded_samples.emplace_back(decoded);
        }
        ret_snd.num_channels = is_stereo ? 2 : 1;
      } else {
        throw runtime_error(format("unknown wav entry type: 0x{:X}", wav_entry->type));
      }
    }
  }

  return make_pair(wsys->wsys_id, ret);
}

struct BARCEntry {
  char name[14];
  phosg::be_uint16_t unknown1;
  phosg::be_uint32_t unknown2[2];
  phosg::be_uint32_t offset;
  phosg::be_uint32_t size;
};

struct BARCHeader {
  phosg::be_uint32_t magic; // 'BARC'
  phosg::be_uint32_t unknown1; // '----'
  phosg::be_uint32_t unknown2;
  phosg::be_uint32_t entry_count;
  char archive_filename[0x10];
  BARCEntry entries[0];

  size_t bytes() const {
    return sizeof(*this) + this->entry_count * sizeof(this->entries[0]);
  }
};

unordered_map<string, SequenceProgram> barc_decode(const void* vdata, size_t size, const char* base_directory) {
  if (size < sizeof(BARCHeader)) {
    throw invalid_argument("BARC data too small for header");
  }

  const BARCHeader* barc = reinterpret_cast<const BARCHeader*>(vdata);
  if (barc->magic != 0x42415243) {
    throw invalid_argument("BARC file not at expected offset");
  }
  if (size < barc->bytes()) {
    throw invalid_argument("BARC data too small for header");
  }

  string sequence_archive_filename = format("{}/Seqs/{}", base_directory, barc->archive_filename);
  auto f = phosg::fopen_unique(sequence_archive_filename, "rb");

  unordered_map<string, SequenceProgram> ret;
  for (size_t x = 0; x < barc->entry_count; x++) {
    const auto& e = barc->entries[x];
    fseek(f.get(), e.offset, SEEK_SET);
    string data = freadx(f.get(), e.size);
    size_t suffix = 0;
    string effective_name = e.name;
    while (ret.count(effective_name)) {
      effective_name = format("{}@{}", e.name, ++suffix);
    }
    ret.emplace(piecewise_construct, forward_as_tuple(effective_name), forward_as_tuple(x, std::move(data)));
  }

  return ret;
}

SequenceProgram::SequenceProgram(uint32_t index, std::string&& data)
    : index(index),
      data(std::move(data)) {}

void SoundEnvironment::resolve_pointers() {
  // postprocessing: resolve all sample bank pointers

  // build an index of {wsys_id: {sound_id: index within wsys}}
  unordered_map<uint32_t, unordered_map<int64_t, size_t>> sound_id_to_index;
  for (const auto& wsys_it : this->sample_banks) {
    for (size_t x = 0; x < wsys_it.second.size(); x++) {
      const auto& sound = wsys_it.second[x];
      phosg::fwrite_fmt(stderr, "[SoundEnvironment/debug] {},{} => {} {} {}\n",
          wsys_it.first, x, sound.aw_file_index, sound.wave_table_index, sound.sound_id);
      bool ret = sound_id_to_index[wsys_it.first].emplace(sound.sound_id, x).second;
      if (!ret) {
        phosg::fwrite_fmt(stderr, "[SoundEnvironment] warning: duplicate sound id {}\n", wsys_it.second[x].sound_id);
      }
    }
  }

  // hack: if all vel regions have sample_bank_id = 0, set their sample bank ids
  // to the instrument bank's chunk id (this is needed for Sunshine apparently)
  // TODO: find a way to short-circuit these loops that doesn't look stupid
  bool override_wsys_id = true;
  for (const auto& bank_it : this->instrument_banks) {
    for (const auto& instrument_it : bank_it.second.id_to_instrument) {
      for (const auto& key_region : instrument_it.second.key_regions) {
        for (const auto& vel_region : key_region.vel_regions) {
          if (vel_region.sample_bank_id != 0) {
            override_wsys_id = false;
            break;
          }
        }
        if (!override_wsys_id) {
          break;
        }
      }
      if (!override_wsys_id) {
        break;
      }
    }
    if (!override_wsys_id) {
      break;
    }
  }

  if (override_wsys_id) {
    phosg::fwrite_fmt(stderr, "[SoundEnvironment] note: ignoring instrument sample bank ids\n");
    for (auto& bank_it : this->instrument_banks) {
      for (auto& instrument_it : bank_it.second.id_to_instrument) {
        for (auto& key_region : instrument_it.second.key_regions) {
          for (auto& vel_region : key_region.vel_regions) {
            vel_region.sample_bank_id = bank_it.second.chunk_id;
          }
        }
      }
    }
  }

  // map all velocity region pointers to the correct Sound objects
  size_t total_sounds = 0, unresolved_sounds = 0;
  for (auto& bank_it : this->instrument_banks) {
    auto& bank = bank_it.second;
    for (auto& instrument_it : bank.id_to_instrument) {
      for (auto& key_region : instrument_it.second.key_regions) {
        for (auto& vel_region : key_region.vel_regions) {
          // try to resolve first using the sample bank id, then using the
          // instrument bank id
          vector<uint32_t> wsys_ids({vel_region.sample_bank_id, bank.chunk_id});
          for (uint32_t wsys_id : wsys_ids) {
            try {
              const auto& wsys_bank = this->sample_banks.at(wsys_id);
              const auto& wsys_indexes = sound_id_to_index.at(wsys_id);
              vel_region.sound = &wsys_bank[wsys_indexes.at(vel_region.sound_id)];
              break;
            } catch (const out_of_range&) {
            }
          }

          total_sounds++;
          if (!vel_region.sound) {
            phosg::fwrite_fmt(stderr, "[SoundEnvironment] error: can\'t resolve sound: bank={} (chunk={}) inst={} key_rgn=[{:X},{:X}] "
                                      "vel_rgn=[{:X}, {:X}, base={:X}, sample_bank_id={:X}, sound_id={:X}]\n",
                bank_it.first, bank.chunk_id,
                instrument_it.first, key_region.key_low, key_region.key_high,
                vel_region.vel_low, vel_region.vel_high, vel_region.base_note,
                vel_region.sample_bank_id, vel_region.sound_id);
            unresolved_sounds++;
          }
        }
      }
    }
  }
  if (unresolved_sounds) {
    phosg::fwrite_fmt(stderr, "[SoundEnvironment] warning: {}/{} ({:g}%) of all sounds are unresolved\n",
        unresolved_sounds, total_sounds, static_cast<double>(unresolved_sounds * 100) / total_sounds);
  }
}

void SoundEnvironment::merge_from(SoundEnvironment&& other) {
  for (auto& it : other.instrument_banks) {
    this->instrument_banks.emplace(std::move(it.first), std::move(it.second));
  }
  for (auto& it : other.sample_banks) {
    this->sample_banks.emplace(std::move(it.first), std::move(it.second));
  }
  for (auto& it : other.sequence_programs) {
    this->sequence_programs.emplace(std::move(it.first), std::move(it.second));
  }
}

SoundEnvironment aaf_decode(const void* vdata, size_t size, const char* base_directory) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);
  size_t offset = 0;

  SoundEnvironment ret;
  while (offset < size) {
    uint32_t chunk_offset, chunk_size, chunk_id;
    uint32_t chunk_type = *reinterpret_cast<const phosg::be_uint32_t*>(data + offset);

    switch (chunk_type) {
      case 1:
      case 5:
      case 6:
      case 7:
        chunk_offset = *reinterpret_cast<const phosg::be_uint32_t*>(data + offset + 4);
        chunk_size = *reinterpret_cast<const phosg::be_uint32_t*>(data + offset + 8);
        // unused int32 after size apparently?
        offset += 0x10;
        break;

      case 2:
      case 3:
        offset += 0x04;
        while (offset < size) {
          chunk_offset = *reinterpret_cast<const phosg::be_uint32_t*>(data + offset);
          if (chunk_offset == 0) {
            offset += 0x04;
            break;
          }

          chunk_size = *reinterpret_cast<const phosg::be_uint32_t*>(data + offset + 4);
          chunk_id = *reinterpret_cast<const phosg::be_uint32_t*>(data + offset + 8);
          if (chunk_type == 2) {
            auto ibnk = ibnk_decode(data + chunk_offset);
            // this is the index of the related wsys block
            ibnk.chunk_id = chunk_id;
            ret.instrument_banks.emplace(ibnk.id, std::move(ibnk));
          } else {
            auto wsys_pair = wsys_decode(data + chunk_offset, base_directory);
            uint32_t wsys_id = wsys_pair.first ? wsys_pair.first : ret.sample_banks.size();
            if (!ret.sample_banks.emplace(wsys_id, std::move(wsys_pair.second)).second) {
              phosg::fwrite_fmt(stderr, "[SoundEnvironment] warning: duplicate wsys id {:X}\n", wsys_id);
            }
          }
          offset += 0x0C;
        }
        break;

      case 4:
        chunk_offset = *reinterpret_cast<const phosg::be_uint32_t*>(data + offset + 4);
        chunk_size = *reinterpret_cast<const phosg::be_uint32_t*>(data + offset + 8);
        ret.sequence_programs = barc_decode(data + chunk_offset, chunk_size, base_directory);
        offset += 0x10;
        break;

      case 0:
        offset = size;
        break;

      default:
        throw invalid_argument(format("unknown chunk type {} ({:08X})",
            string_view(reinterpret_cast<char*>(&chunk_type), 4), chunk_type));
    }
  }

  ret.resolve_pointers();
  return ret;
}

SoundEnvironment baa_decode(const void* vdata, size_t size, const char* base_directory) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);
  const phosg::be_uint32_t* data_fields = reinterpret_cast<const phosg::be_uint32_t*>(vdata);
  size_t field_offset = 1;

  if (size < 8) {
    throw runtime_error("baa file is too small for header");
  }
  if (data_fields[0] != 0x41415F3C) { // 'AA_<'
    throw runtime_error("baa file does not appear to be an audio archive");
  }

  SoundEnvironment ret;
  bool complete = false;
  while (!complete && (field_offset < size)) {
    uint32_t chunk_type = data_fields[field_offset++];
    switch (chunk_type) {

      case 0x62736674: // 'bsft'
      case 0x62666361: // 'bfca'
        field_offset++; // offset
        break;

      case 0x62737420: // 'bst '
      case 0x6273746E: // 'bstn'
      case 0x62736320: // 'bsc '
        field_offset += 2; // offset and end_offset
        break;

      case 0x77732020: { // 'ws  '
        uint32_t wsys_id = data_fields[field_offset++];
        uint32_t offset = data_fields[field_offset++];
        field_offset++; // unclear what this field is

        // TODO: should we trust wsys_id here or use the same logic as for aaf?
        auto wsys_pair = wsys_decode(data + offset, base_directory);
        wsys_id = wsys_pair.first ? wsys_pair.first : wsys_id;
        if (!ret.sample_banks.emplace(wsys_id, std::move(wsys_pair.second)).second) {
          phosg::fwrite_fmt(stderr, "[SoundEnvironment] warning: duplicate wsys id {:X}\n", wsys_id);
        }
        break;
      }

      case 0x626E6B20: { // 'bnk '
        uint32_t chunk_id = data_fields[field_offset++];
        uint32_t offset = data_fields[field_offset++];
        // unlike 'ws  ' above, there isn't an extra unused field here
        auto ibnk = ibnk_decode(data + offset);
        ibnk.chunk_id = chunk_id;
        ret.instrument_banks.emplace(ibnk.id, std::move(ibnk));
        break;
      }

      case 0x626D7320: { // 'bms '
        // TODO: figure out if this mask is correct
        uint32_t id = data_fields[field_offset++] & 0x0000FFFF;
        uint32_t offset = data_fields[field_offset++];
        uint32_t end_offset = data_fields[field_offset++];
        ret.sequence_programs.emplace(piecewise_construct,
            forward_as_tuple(format("seq{}", id)),
            forward_as_tuple(id, string(reinterpret_cast<const char*>(data + offset), end_offset - offset)));
        break;
      }

      case 0x62616163: { // 'baac'
        uint32_t offset = data_fields[field_offset++];
        uint32_t end_offset = data_fields[field_offset++];
        if (end_offset - offset < 0x18) {
          throw invalid_argument("embedded baa is too small for header");
        }
        // there are 4 4-byte fields before the baa apparently
        ret.merge_from(baa_decode(data + offset + 0x10, end_offset - offset - 0x10, base_directory));
        break;
      }

      case 0x3E5F4141: // '>_AA'
        complete = true;
        break;

      default:
        phosg::be_uint32_t chunk_type_be = chunk_type;
        throw invalid_argument(format("unknown chunk type {} ({:08X})",
            string_view(reinterpret_cast<char*>(&chunk_type_be), 4), chunk_type));
    }
  }

  ret.resolve_pointers();
  return ret;
}

struct BXHeader {
  phosg::be_uint32_t wsys_table_offset;
  phosg::be_uint32_t wsys_count;
  phosg::be_uint32_t ibnk_table_offset;
  phosg::be_uint32_t ibnk_count;
};

struct BXTableEntry {
  phosg::be_uint32_t offset;
  phosg::be_uint32_t size;
};

SoundEnvironment bx_decode(const void* vdata, size_t, const char* base_directory) {
  // TODO: Be less lazy and implement bounds checks here.

  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);
  const BXHeader* header = reinterpret_cast<const BXHeader*>(vdata);

  SoundEnvironment ret;
  const BXTableEntry* entry = reinterpret_cast<const BXTableEntry*>(data + header->wsys_table_offset);
  for (size_t x = 0; x < header->wsys_count; x++) {
    if (entry->size == 0) {
      ret.sample_banks.emplace(ret.sample_banks.size(), vector<Sound>());
    } else {
      auto wsys_pair = wsys_decode(data + entry->offset, base_directory);
      uint32_t wsys_id = wsys_pair.first ? wsys_pair.first : ret.sample_banks.size();
      if (!ret.sample_banks.emplace(wsys_id, std::move(wsys_pair.second)).second) {
        phosg::fwrite_fmt(stderr, "[SoundEnvironment] warning: duplicate wsys id {:X}\n", wsys_id);
      }
    }
    entry++;
  }

  entry = reinterpret_cast<const BXTableEntry*>(data + header->ibnk_table_offset);
  for (size_t x = 0; x < header->ibnk_count; x++) {
    if (entry->size != 0) {
      auto ibnk = ibnk_decode(data + entry->offset);
      ibnk.chunk_id = x;
      ret.instrument_banks.emplace(x, std::move(ibnk));
    } else {
      ret.instrument_banks.emplace(piecewise_construct, forward_as_tuple(x), forward_as_tuple(x));
    }
    entry++;
  }

  ret.resolve_pointers();
  return ret;
}

SoundEnvironment load_sound_environment(const char* base_directory) {
  // Pikmin: pikibank.bx has almost everything; the sequence index is inside
  // default.dol (sigh) so it has to be manually extracted. search for 'BARC' in
  // default.dol in a hex editor and copy the resulting data (through the end of
  // the sequence names) to sequence.barc in the Seqs directory
  {
    string filename = format("{}/Banks/pikibank.bx", base_directory);
    if (filesystem::is_regular_file(filename)) {
      string data = phosg::load_file(filename);
      auto env = bx_decode(data.data(), data.size(), base_directory);

      data = phosg::load_file(format("{}/Seqs/sequence.barc", base_directory));
      env.sequence_programs = barc_decode(data.data(), data.size(), base_directory);

      return env;
    }
  }

  {
    static const vector<string> filenames = {"/JaiInit.aaf", "/msound.aaf"};
    for (const auto& filename : filenames) {
      string data;
      try {
        data = phosg::load_file(base_directory + filename);
      } catch (const phosg::cannot_open_file&) {
        continue;
      }
      return aaf_decode(data.data(), data.size(), base_directory);
    }
  }

  {
    static const vector<string> filenames = {"/GCKart.baa", "/Z2Sound.baa", "/SMR.baa"};
    for (const auto& filename : filenames) {
      string data;
      try {
        data = phosg::load_file(base_directory + filename);
      } catch (const phosg::cannot_open_file&) {
        continue;
      }
      return baa_decode(data.data(), data.size(), base_directory);
    }
  }

  throw runtime_error("no index file found");
}

SoundEnvironment create_midi_sound_environment(const unordered_map<int16_t, InstrumentMetadata>& instrument_metadata) {
  SoundEnvironment env;

  // create instrument bank 0
  auto& inst_bank = env.instrument_banks.emplace(piecewise_construct, forward_as_tuple(0), forward_as_tuple(0)).first->second;
  for (const auto& it : instrument_metadata) {
    // TODO: do we need to pass in base_note for the vel region?
    auto& inst = inst_bank.id_to_instrument.emplace(piecewise_construct, forward_as_tuple(it.first), forward_as_tuple(it.first)).first->second;
    inst.key_regions.emplace_back(0, 0x7F);
    auto& key_region = inst.key_regions.back();
    key_region.vel_regions.emplace_back(0, 0x7F, 0, it.first, 1, 1);
  }

  // create sample bank 0
  auto& sample_bank = env.sample_banks.emplace(piecewise_construct, forward_as_tuple(0), forward_as_tuple(0)).first->second;
  for (const auto& it : instrument_metadata) {
    sample_bank.emplace_back();
    Sound& s = sample_bank.back();

    auto f = phosg::fopen_unique(it.second.filename);
    auto wav = load_wav(f.get());
    s.decoded_samples = wav.samples;
    s.num_channels = wav.num_channels;
    s.sample_rate = wav.sample_rate;
    if (it.second.base_note >= 0) {
      s.base_note = it.second.base_note;
    } else if (wav.base_note >= 0) {
      s.base_note = wav.base_note;
    } else {
      s.base_note = 0x3C;
    }
    if (wav.loops.size() == 1) {
      s.loop_start = wav.loops[0].start;
      s.loop_end = wav.loops[0].end;
    } else {
      s.loop_start = 0;
      s.loop_end = 0;
    }
    s.sound_id = it.first;
    s.source_filename = it.second.filename;
    s.source_offset = 0;
    s.source_size = 0;
    s.aw_file_index = 0;
    s.wave_table_index = 0;
  }

  env.resolve_pointers();
  return env;
}

SoundEnvironment create_json_sound_environment(const phosg::JSON& instruments_json, const string& directory) {
  SoundEnvironment env;

  // create instrument bank 0 and sample bank 0
  auto& inst_bank = env.instrument_banks.emplace(piecewise_construct, forward_as_tuple(0), forward_as_tuple(0)).first->second;
  auto& sample_bank = env.sample_banks.emplace(piecewise_construct, forward_as_tuple(0), forward_as_tuple(0)).first->second;

  // create instruments
  size_t sound_id = 1;
  for (const auto& inst_json : instruments_json.as_list()) {
    int64_t id = inst_json->at("id").as_int();
    auto& inst = inst_bank.id_to_instrument.emplace(piecewise_construct, forward_as_tuple(id), forward_as_tuple(id)).first->second;

    // phosg::fwrite_fmt(stderr, "[create_json_sound_environment] creating instrument {}\n", id);

    for (const auto& rgn_json : inst_json->at("regions").as_list()) {
      int64_t key_low = rgn_json->at("key_low").as_int();
      int64_t key_high = rgn_json->at("key_high").as_int();
      int64_t base_note = rgn_json->at("base_note").as_int();
      string filename = directory + "/" + rgn_json->at("filename").as_string();

      double freq_mult = rgn_json->get_float("freq_mult", 1.0);
      bool constant_pitch = rgn_json->get_bool("constant_pitch", false);

      SampledSound wav;
      try {
        auto f = phosg::fopen_unique(filename);
        wav = load_wav(f.get());
      } catch (const exception& e) {
        phosg::fwrite_fmt(stderr, "[create_json_sound_environment] creating region {:02X}:{:02X}@{:02X} -> {} ({}) for instrument {} failed: {}\n",
            key_low, key_high, base_note, filename.c_str(), sound_id, id, e.what());
        continue;
      }

      // create the sound object
      sample_bank.emplace_back();
      Sound& s = sample_bank.back();

      s.decoded_samples = wav.samples;
      s.num_channels = wav.num_channels;
      s.sample_rate = wav.sample_rate;
      if (base_note > 0) {
        s.base_note = base_note;
      } else if (wav.base_note >= 0) {
        s.base_note = wav.base_note;
      } else {
        s.base_note = 0x3C;
      }
      if (wav.loops.size() == 1) {
        s.loop_start = wav.loops[0].start;
        s.loop_end = wav.loops[0].end;
      } else {
        s.loop_start = 0;
        s.loop_end = 0;
      }
      s.sound_id = sound_id;
      s.source_filename = filename;
      s.source_offset = 0;
      s.source_size = 0;
      s.aw_file_index = 0;
      s.wave_table_index = 0;

      // create the key region and vel region objects
      inst.key_regions.emplace_back(key_low, key_high);
      auto& key_rgn = inst.key_regions.back();
      key_rgn.vel_regions.emplace_back(0, 0x7F, 0, sound_id, freq_mult, 1, s.base_note, constant_pitch);

      // use up the sound id
      sound_id++;
    }
  }

  env.resolve_pointers();
  return env;
}

} // namespace Audio
} // namespace ResourceDASM
