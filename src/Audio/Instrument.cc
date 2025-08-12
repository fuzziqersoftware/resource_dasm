#include "Instrument.hh"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <format>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <vector>

#include "../AudioCodecs.hh"

using namespace std;

namespace ResourceDASM {
namespace Audio {

const vector<float>& Sound::samples() const {
  if (this->decoded_samples.empty()) {
    this->decoded_samples = decode_afc(this->afc_data.data(), this->afc_data.size(), this->afc_large_frames);
    this->afc_data.clear();
  }
  return this->decoded_samples;
}

VelocityRegion::VelocityRegion(uint8_t vel_low, uint8_t vel_high,
    uint16_t sample_bank_id, uint16_t sound_id, float freq_mult,
    float volume_mult, int8_t base_note, bool constant_pitch)
    : vel_low(vel_low),
      vel_high(vel_high),
      sample_bank_id(sample_bank_id),
      sound_id(sound_id),
      freq_mult(freq_mult),
      volume_mult(volume_mult),
      constant_pitch(constant_pitch),
      base_note(base_note),
      sound(nullptr) {}

KeyRegion::KeyRegion(uint8_t key_low, uint8_t key_high)
    : key_low(key_low),
      key_high(key_high) {}

const VelocityRegion& KeyRegion::region_for_velocity(uint8_t velocity) const {
  for (const VelocityRegion& r : this->vel_regions) {
    if (r.vel_low <= velocity && r.vel_high >= velocity) {
      return r;
    }
  }
  throw out_of_range("no such velocity");
}

Instrument::Instrument(uint32_t id) : id(id) {}

const KeyRegion& Instrument::region_for_key(uint8_t key) const {
  for (const KeyRegion& r : this->key_regions) {
    if (r.key_low <= key && r.key_high >= key) {
      return r;
    }
  }
  throw out_of_range("no such key");
}

InstrumentBank::InstrumentBank(uint32_t id)
    : id(id),
      chunk_id(0) {}

struct ibnk_inst_inst_vel_region {
  uint8_t vel_high;
  uint8_t unknown1[3];
  phosg::be_uint16_t sample_bank_id;
  phosg::be_uint16_t sample_num;
  phosg::be_float volume_mult;
  phosg::be_float freq_mult;
} __attribute__((packed));

struct ibnk_inst_inst_key_region {
  uint8_t key_high;
  uint8_t unknown1[3];
  phosg::be_uint32_t vel_region_count;
  phosg::be_uint32_t vel_region_offsets[0];
} __attribute__((packed));

struct ibnk_inst_inst_header {
  phosg::be_uint32_t magic;
  phosg::be_uint32_t unknown;
  phosg::be_float freq_mult;
  phosg::be_float volume_mult;
  phosg::be_uint32_t osc_offsets[2];
  phosg::be_uint32_t eff_offsets[2];
  phosg::be_uint32_t sen_offsets[2];
  phosg::be_uint32_t key_region_count;
  phosg::be_uint32_t key_region_offsets[0];
} __attribute__((packed));

struct ibnk_inst_per2_key_region {
  phosg::be_float freq_mult;
  phosg::be_float volume_mult;
  phosg::be_uint32_t unknown2[2];
  phosg::be_uint32_t vel_region_count;
  phosg::be_uint32_t vel_region_offsets[0];
} __attribute__((packed));

struct ibnk_inst_per2_header {
  phosg::be_uint32_t magic;
  phosg::be_uint32_t unknown1[0x21];
  phosg::be_uint32_t key_region_offsets[100];
} __attribute__((packed));

struct ibnk_inst_perc_header {
  // total guess: PERC instruments are just 0x7F key regions after the magic
  // number. there don't appear to be any size/count fields in the structure.
  // another guess: the key region format appears to match the per2 key region
  // format; assume they're the same
  phosg::be_uint32_t magic;
  phosg::be_uint32_t key_region_offsets[0x7F];
} __attribute__((packed));

struct ibnk_inst_percnew_header {
  phosg::be_uint32_t magic; // 'Perc'
  phosg::be_uint32_t count;
  phosg::be_uint32_t pmap_offsets[0];
} __attribute__((packed));

struct ibnk_inst_instnew_vel_region {
  uint8_t vel_high;
  uint8_t unused[3];
  phosg::be_uint16_t sample_bank_id;
  phosg::be_uint16_t sample_num;
  phosg::be_float volume_mult;
  phosg::be_float freq_mult;
} __attribute__((packed));

struct ibnk_inst_instnew_key_region {
  uint8_t key_high;
  uint8_t unused[3];
  phosg::be_uint32_t vel_region_count;
} __attribute__((packed));

struct ibnk_inst_instnew_header {
  phosg::be_uint32_t magic; // 'Inst'
  phosg::be_uint32_t osc;
  phosg::be_uint32_t inst_id;
  // TODO: this appears to control the instrument format somehow. usually it's
  // zero but if it's a small number, it appears to specify the number of 32-bit
  // fields following it (of unknown purpose) before the key region count. for
  // example, Twilight Princess has an instrument that looks like this:
  // 496E7374 00000001 00000021 00000002 000014D8 00001518 00000003 3D000000 ...
  // (the 3D is probably key_high for the first key region)
  phosg::be_uint32_t unknown1;
  phosg::be_uint32_t key_region_count;
} __attribute__((packed));

struct ibnk_inst_instnew_footer {
  phosg::be_float volume_mult;
  phosg::be_float freq_mult;
} __attribute__((packed));

struct ibnk_inst_pmap_header {
  phosg::be_uint32_t magic;
  phosg::be_float volume_mult;
  phosg::be_float freq_mult;
  phosg::be_uint32_t unknown[2];
  phosg::be_uint32_t vel_region_count;
  ibnk_inst_instnew_vel_region vel_regions[0];
} __attribute__((packed));

struct ibnk_bank_header {
  phosg::be_uint32_t magic; // 'BANK'
  phosg::be_uint32_t inst_offsets[245];
} __attribute__((packed));

struct ibnk_list_header {
  phosg::be_uint32_t magic; // 'LIST'
  phosg::be_uint32_t size;
  phosg::be_uint32_t count;
  phosg::be_uint32_t inst_offsets[0];
} __attribute__((packed));

struct ibnk_header {
  phosg::be_uint32_t magic; // 'IBNK'
  phosg::be_uint32_t size;
  phosg::be_uint32_t bank_id;
  phosg::be_uint32_t unknown1[5];
} __attribute__((packed));

struct ibnk_chunk_header {
  phosg::be_uint32_t magic;
  phosg::be_uint32_t size;
} __attribute__((packed));

Instrument ibnk_inst_decode(const void* vdata, size_t offset, size_t inst_id) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);
  const uint8_t* inst_data = data + offset;
  Instrument result_inst(inst_id);

  // old-style instrument (Luigi's Mansion / Pikmin era)
  if (!memcmp(inst_data, "INST", 4)) {
    const ibnk_inst_inst_header* inst = reinterpret_cast<const ibnk_inst_inst_header*>(inst_data);

    const phosg::be_float default_mult = 1.0;
    // TODO: Use this in the appropriate places
    // float freq_mult = (inst->freq_mult == 0.0) ? default_mult : inst->freq_mult;
    float volume_mult = (inst->volume_mult == 0.0) ? default_mult : inst->volume_mult;

    uint8_t key_low = 0;
    for (uint32_t x = 0; x < inst->key_region_count; x++) {
      const ibnk_inst_inst_key_region* key_region = reinterpret_cast<const ibnk_inst_inst_key_region*>(
          data + inst->key_region_offsets[x]);

      result_inst.key_regions.emplace_back(key_low, key_region->key_high);
      auto& result_key_region = result_inst.key_regions.back();

      uint8_t vel_low = 0;
      for (uint32_t y = 0; y < key_region->vel_region_count; y++) {
        const ibnk_inst_inst_vel_region* vel_region = reinterpret_cast<const ibnk_inst_inst_vel_region*>(
            data + key_region->vel_region_offsets[y]);

        // TODO: we should also multiply by inst->freq_mult here, but it makes
        // Sunshine sequences sound wrong (especially k_dolpic). figure out
        // why and fix it
        result_key_region.vel_regions.emplace_back(vel_low,
            vel_region->vel_high, vel_region->sample_bank_id,
            vel_region->sample_num, vel_region->freq_mult,
            vel_region->volume_mult * volume_mult);

        vel_low = vel_region->vel_high + 1;
      }
      key_low = key_region->key_high + 1;
    }
    return result_inst;
  }

  // new-style Perc instruments (Twilight Princess)
  if (!memcmp(inst_data, "Perc", 4)) {
    const ibnk_inst_percnew_header* percnew_header = reinterpret_cast<const ibnk_inst_percnew_header*>(inst_data);

    for (size_t z = 0; z < percnew_header->count; z++) {
      if (!percnew_header->pmap_offsets[z]) {
        continue;
      }

      const ibnk_inst_pmap_header* pmap_header = reinterpret_cast<const ibnk_inst_pmap_header*>(
          data + percnew_header->pmap_offsets[z]);

      result_inst.key_regions.emplace_back(z, z);
      auto& result_key_region = result_inst.key_regions.back();

      uint8_t vel_low = 0;
      for (uint32_t y = 0; y < pmap_header->vel_region_count; y++) {
        // TODO: should we multiply by the pmap's freq_mult here? there's a hack
        // for old-style instruments where we don't use the INST's freq_mult;
        // figure out if we should do the same here (currently we don't)
        auto& vel_region = pmap_header->vel_regions[y];
        result_key_region.vel_regions.emplace_back(vel_low,
            vel_region.vel_high, vel_region.sample_bank_id,
            vel_region.sample_num, vel_region.freq_mult * pmap_header->freq_mult,
            vel_region.volume_mult * pmap_header->volume_mult);
        vel_low = vel_region.vel_high + 1;
      }
    }

    return result_inst;
  }

  // new-style Inst instruments (Twilight Princess)
  if (!memcmp(inst_data, "Inst", 4)) {
    const ibnk_inst_instnew_header* instnew_header = reinterpret_cast<const ibnk_inst_instnew_header*>(inst_data);

    result_inst.id = instnew_header->inst_id;

    if (instnew_header->key_region_count > 0x7F) {
      throw runtime_error("key region count is too large");
    }

    // sigh... why did they specify these structs inline and use offsets
    // everywhere else? just for maximum tedium? we'll reuse offset to keep
    // track of what we've already parsed
    offset += sizeof(ibnk_inst_instnew_header);
    uint8_t key_low = 0;
    for (size_t z = 0; z < instnew_header->key_region_count; z++) {
      const ibnk_inst_instnew_key_region* key_region = reinterpret_cast<const ibnk_inst_instnew_key_region*>(
          data + offset);
      offset += sizeof(ibnk_inst_instnew_key_region);

      result_inst.key_regions.emplace_back(key_low, key_region->key_high);
      auto& result_key_region = result_inst.key_regions.back();

      uint8_t vel_low = 0;
      for (uint32_t y = 0; y < key_region->vel_region_count; y++) {
        const ibnk_inst_instnew_vel_region* vel_region = reinterpret_cast<const ibnk_inst_instnew_vel_region*>(
            data + offset);
        offset += sizeof(ibnk_inst_instnew_vel_region);

        result_key_region.vel_regions.emplace_back(vel_low,
            vel_region->vel_high, vel_region->sample_bank_id,
            vel_region->sample_num, vel_region->freq_mult,
            vel_region->volume_mult);
        vel_low = vel_region->vel_high + 1;
      }
      key_low = key_region->key_high + 1;
    }

    // after all that, there's an instrument-global volume and freq mult. go
    // through all the vel regions and apply these factors appropriately
    const ibnk_inst_instnew_footer* footer = reinterpret_cast<const ibnk_inst_instnew_footer*>(
        data + offset);
    for (auto& key_region : result_inst.key_regions) {
      for (auto& vel_region : key_region.vel_regions) {
        vel_region.volume_mult *= footer->volume_mult;
        vel_region.freq_mult *= footer->freq_mult;
      }
    }

    return result_inst;
  }

  // old-style PERC and PER2 instruments (Luigi's Mansion / Pikmin era)
  const phosg::be_uint32_t* offset_table = nullptr;
  uint32_t count = 0;
  if (!memcmp(inst_data, "PERC", 4)) {
    const ibnk_inst_perc_header* perc = reinterpret_cast<const ibnk_inst_perc_header*>(inst_data);
    offset_table = perc->key_region_offsets;
    count = 0x7F;

  } else if (!memcmp(inst_data, "PER2", 4)) {
    const ibnk_inst_per2_header* per2 = reinterpret_cast<const ibnk_inst_per2_header*>(inst_data);
    offset_table = per2->key_region_offsets;
    count = 0x64;

  } else {
    throw invalid_argument(format("unknown instrument format at {:08X}: {:.4} ({:08X})",
        offset, string_view(reinterpret_cast<const char*>(inst_data), 4),
        *reinterpret_cast<const uint32_t*>(inst_data)));
  }

  for (uint32_t x = 0; x < count; x++) {
    if (!offset_table[x]) {
      continue;
    }

    const ibnk_inst_per2_key_region* key_region = reinterpret_cast<const ibnk_inst_per2_key_region*>(
        data + offset_table[x]);

    result_inst.key_regions.emplace_back(x, x);
    auto& result_key_region = result_inst.key_regions.back();

    uint8_t vel_low = 0;
    for (uint32_t y = 0; y < key_region->vel_region_count; y++) {
      const ibnk_inst_inst_vel_region* vel_region = reinterpret_cast<const ibnk_inst_inst_vel_region*>(
          data + key_region->vel_region_offsets[y]);

      // TODO: Luigi's Mansion appears to multiply these by 8. figure out
      // where this comes from and implement it properly (right now we don't
      // implement it, because Pikmin doesn't do this and it sounds terrible
      // if we do)
      float freq_mult = vel_region->freq_mult * key_region->freq_mult;
      result_key_region.vel_regions.emplace_back(vel_low,
          vel_region->vel_high, vel_region->sample_bank_id,
          vel_region->sample_num, freq_mult, 1.0, x);

      vel_low = vel_region->vel_high + 1;
    }
  }
  return result_inst;
}

InstrumentBank ibnk_decode(const void* vdata) {
  if (memcmp(vdata, "IBNK", 4)) {
    throw invalid_argument("IBNK file not at expected offset");
  }

  const ibnk_header* ibnk = reinterpret_cast<const ibnk_header*>(vdata);
  InstrumentBank result_bank(ibnk->bank_id);

  // for older games, the BANK chunk immediately follows the IBNK header. for
  // newer games, there's no BANK chunk at all.
  size_t offset = sizeof(ibnk_header);
  {
    const ibnk_chunk_header* first_chunk_header = reinterpret_cast<const ibnk_chunk_header*>(
        reinterpret_cast<const char*>(vdata) + offset);
    if (!memcmp(&first_chunk_header->magic, "BANK", 4)) {
      const ibnk_bank_header* bank_header = reinterpret_cast<const ibnk_bank_header*>(
          reinterpret_cast<const char*>(vdata) + offset);
      for (size_t z = 0; z < 245; z++) {
        if (!bank_header->inst_offsets[z]) {
          continue;
        }
        try {
          auto inst = ibnk_inst_decode(vdata, bank_header->inst_offsets[z], z);
          result_bank.id_to_instrument.emplace(z, inst);
        } catch (const exception& e) {
          phosg::fwrite_fmt(stderr, "warning: failed to decode instrument: {}\n", e.what());
        }
      }
      return result_bank;
    }
  }

  while (offset < ibnk->size) {
    const ibnk_chunk_header* chunk_header = reinterpret_cast<const ibnk_chunk_header*>(
        reinterpret_cast<const char*>(vdata) + offset);

    // note: we skip INST even though it contains relevant data because the LIST
    // chunk countains references to it and we parse it through there instead
    if (!memcmp(&chunk_header->magic, "ENVT", 4) ||
        !memcmp(&chunk_header->magic, "OSCT", 4) ||
        !memcmp(&chunk_header->magic, "PMAP", 4) ||
        !memcmp(&chunk_header->magic, "PERC", 4) ||
        !memcmp(&chunk_header->magic, "RAND", 4) ||
        !memcmp(&chunk_header->magic, "SENS", 4) ||
        !memcmp(&chunk_header->magic, "INST", 4)) {
      // sometimes these chunks aren't aligned to 4-byte boundaries, but all
      // chunk headers are aligned. looks like they just force alignment in the
      // file, so do that here too
      offset = (offset + sizeof(chunk_header) + chunk_header->size + 3) & (~3);

      // there might be a few zeroes to pad out the IBNK block at the end (looks
      // like they want to be aligned to 0x20-byte boundaries?)
    } else if (chunk_header->magic == 0) {
      offset += 4;

    } else if (!memcmp(&chunk_header->magic, "LIST", 4)) {
      const ibnk_list_header* list_header = reinterpret_cast<const ibnk_list_header*>(
          reinterpret_cast<const char*>(vdata) + offset);
      for (size_t z = 0; z < list_header->count; z++) {
        if (!list_header->inst_offsets[z]) {
          continue;
        }
        try {
          auto inst = ibnk_inst_decode(vdata, list_header->inst_offsets[z], z);
          result_bank.id_to_instrument.emplace(z, inst);
        } catch (const exception& e) {
          phosg::fwrite_fmt(stderr, "warning: failed to decode instrument: {}\n", e.what());
        }
      }
      offset += list_header->size + sizeof(ibnk_list_header);

    } else if (!memcmp(&chunk_header->magic, "BANK", 4)) {
      throw runtime_error(format("IBNK contains BANK at {:08X} but it is not first",
          offset));

    } else {
      throw runtime_error(format("unknown IBNK chunk type at {:08X}: {:.4}",
          offset, reinterpret_cast<const char*>(&chunk_header->magic)));
    }
  }

  return result_bank;
}

} // namespace Audio
} // namespace ResourceDASM
