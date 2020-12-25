#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>

#include <vector>

#include "mc68k.hh"
#include "quickdraw_formats.hh"
#include "pict.hh"



#define RESOURCE_TYPE_ADBS  0x41444253
#define RESOURCE_TYPE_CDEF  0x43444546
#define RESOURCE_TYPE_cfrg  0x63667267
#define RESOURCE_TYPE_cicn  0x6369636E
#define RESOURCE_TYPE_clok  0x636C6F6B
#define RESOURCE_TYPE_clut  0x636C7574
#define RESOURCE_TYPE_cmid  0x636D6964
#define RESOURCE_TYPE_CODE  0x434F4445
#define RESOURCE_TYPE_crsr  0x63727372
#define RESOURCE_TYPE_csnd  0x63736E64
#define RESOURCE_TYPE_CURS  0x43555253
#define RESOURCE_TYPE_dcmp  0x64636D70
#define RESOURCE_TYPE_ecmi  0x65636D69
#define RESOURCE_TYPE_emid  0x656D6964
#define RESOURCE_TYPE_ESnd  0x45536E64
#define RESOURCE_TYPE_esnd  0x65736E64
#define RESOURCE_TYPE_icl4  0x69636C34
#define RESOURCE_TYPE_icl8  0x69636C38
#define RESOURCE_TYPE_icm4  0x69636D34
#define RESOURCE_TYPE_icm8  0x69636D38
#define RESOURCE_TYPE_icmN  0x69636D23
#define RESOURCE_TYPE_ICNN  0x49434E23
#define RESOURCE_TYPE_icns  0x69636E73
#define RESOURCE_TYPE_ICON  0x49434F4E
#define RESOURCE_TYPE_ics4  0x69637334
#define RESOURCE_TYPE_ics8  0x69637338
#define RESOURCE_TYPE_icsN  0x69637323
#define RESOURCE_TYPE_INIT  0x494E4954
#define RESOURCE_TYPE_INST  0x494E5354
#define RESOURCE_TYPE_kcs4  0x6B637334
#define RESOURCE_TYPE_kcs8  0x6B637338
#define RESOURCE_TYPE_kcsN  0x6B637323
#define RESOURCE_TYPE_LDEF  0x4C444546
#define RESOURCE_TYPE_MADH  0x4D414448
#define RESOURCE_TYPE_MDBF  0x4D444246
#define RESOURCE_TYPE_MDEF  0x4D444546
#define RESOURCE_TYPE_MIDI  0x4D494449
#define RESOURCE_TYPE_Midi  0x4D696469
#define RESOURCE_TYPE_midi  0x6D696469
#define RESOURCE_TYPE_MOOV  0x4D4F4F56
#define RESOURCE_TYPE_MooV  0x4D6F6F56
#define RESOURCE_TYPE_moov  0x6D6F6F76
#define RESOURCE_TYPE_PACK  0x5041434B
#define RESOURCE_TYPE_PAT   0x50415420
#define RESOURCE_TYPE_PATN  0x50415423
#define RESOURCE_TYPE_PICT  0x50494354
#define RESOURCE_TYPE_pltt  0x706C7474
#define RESOURCE_TYPE_ppat  0x70706174
#define RESOURCE_TYPE_pptN  0x70707423
#define RESOURCE_TYPE_proc  0x70726F63
#define RESOURCE_TYPE_PTCH  0x50544348
#define RESOURCE_TYPE_ptch  0x70746368
#define RESOURCE_TYPE_ROvr  0x524F7672
#define RESOURCE_TYPE_SERD  0x53455244
#define RESOURCE_TYPE_SICN  0x5349434E
#define RESOURCE_TYPE_SIZE  0x53495A45
#define RESOURCE_TYPE_SMOD  0x534D4F44
#define RESOURCE_TYPE_SMSD  0x534D5344
#define RESOURCE_TYPE_snd   0x736E6420
#define RESOURCE_TYPE_snth  0x736E7468
#define RESOURCE_TYPE_SONG  0x534F4E47
#define RESOURCE_TYPE_STR   0x53545220
#define RESOURCE_TYPE_STRN  0x53545223
#define RESOURCE_TYPE_styl  0x7374796C
#define RESOURCE_TYPE_TEXT  0x54455854
#define RESOURCE_TYPE_Tune  0x54756E65
#define RESOURCE_TYPE_WDEF  0x57444546

std::string string_for_resource_type(uint32_t type);


struct ResourceForkHeader {
  uint32_t resource_data_offset;
  uint32_t resource_map_offset;
  uint32_t resource_data_size;
  uint32_t resource_map_size;

  void read(int fd, size_t offset);
};

struct ResourceMapHeader {
  uint8_t reserved[16];
  uint32_t reserved_handle;
  uint16_t reserved_file_ref_num;
  uint16_t attributes;
  uint16_t resource_type_list_offset; // relative to start of this struct
  uint16_t resource_name_list_offset; // relative to start of this struct

  void read(int fd, size_t offset);
};

struct ResourceTypeListEntry {
  uint32_t resource_type;
  uint16_t num_items; // actually num_items - 1
  uint16_t reference_list_offset; // relative to start of type list

  void read(int fd, size_t offset);
};

struct ResourceTypeList {
  uint16_t num_types; // actually num_types - 1
  std::vector<ResourceTypeListEntry> entries;

  void read(int fd, size_t offset);
};

struct ResourceReferenceListEntry {
  int16_t resource_id;
  uint16_t name_offset;
  uint32_t attributes_and_offset; // attr = high 8 bits; offset relative to resource data segment start
  uint32_t reserved;

  void read(int fd, size_t offset);
};



class ResourceFile {
public:
  ResourceFile(const std::string& filename);
  ResourceFile(const char* filename);
  virtual ~ResourceFile() = default;

  virtual bool resource_exists(uint32_t type, int16_t id);
  virtual std::string get_resource_data(uint32_t type, int16_t id,
      bool decompress = true,
      DebuggingMode decompress_debug = DebuggingMode::Disabled);
  virtual bool resource_is_compressed(uint32_t type, int16_t id);
  virtual std::vector<int16_t> all_resources_of_type(uint32_t type);
  virtual std::vector<std::pair<uint32_t, int16_t>> all_resources();

  uint32_t find_resource_by_id(int16_t id, const std::vector<uint32_t>& types);

  struct DecodedCodeFragmentEntry {
    uint32_t architecture;
    uint8_t update_level;
    uint32_t current_version;
    uint32_t old_def_version;
    uint32_t app_stack_size;
    union {
      int16_t app_subdir_id;
      uint16_t lib_flags;
    };

    enum class Usage {
      IMPORT_LIBRARY = 0,
      APPLICATION = 1,
      DROP_IN_ADDITION = 2,
      STUB_LIBRARY = 3,
      WEAK_STUB_LIBRARY = 4,
    };
    Usage usage;

    enum class Where {
      MEMORY = 0,
      DATA_FORK = 1,
      RESOURCE = 2,
      BYTE_STREAM = 3, // reserved
      NAMED_FRAGMENT = 4, // reserved
    };
    Where where;

    uint32_t offset;
    uint32_t length; // if zero, fragment fills the entire space (e.g. entire data fork)
    union {
      uint32_t space_id;
      uint32_t fork_kind;
    };
    uint16_t fork_instance;
    // TODO: support extensions
    std::string name;
  };

  struct DecodedColorIconResource {
    Image image;
    Image bitmap;

    DecodedColorIconResource(Image&& image, Image&& bitmap);
  };

  struct DecodedCursorResource {
    Image bitmap;
    uint16_t hotspot_x;
    uint16_t hotspot_y;

    DecodedCursorResource(Image&& bitmap, uint16_t x, uint16_t y);
  };

  struct DecodedColorCursorResource {
    Image image;
    Image bitmap;
    uint16_t hotspot_x;
    uint16_t hotspot_y;

    DecodedColorCursorResource(Image&& image, Image&& bitmap, uint16_t x,
        uint16_t y);
  };

  struct DecodedInstrumentResource {
    struct KeyRegion {
      uint8_t key_low;
      uint8_t key_high;
      uint8_t base_note;
      int16_t snd_id;
      uint32_t snd_type; // can be RESOURCE_TYPE_snd or RESOURCE_TYPE_csnd

      KeyRegion(uint8_t key_low, uint8_t key_high, uint8_t base_note,
          int16_t snd_id, uint32_t snd_type);
    };

    std::vector<KeyRegion> key_regions;
    uint8_t base_note;
    bool use_sample_rate;
    bool constant_pitch;
  };

  struct DecodedSongResource {
    int16_t midi_id;
    uint16_t tempo_bias;
    int8_t semitone_shift;
    uint8_t percussion_instrument;
    bool allow_program_change;
    std::unordered_map<uint16_t, uint16_t> instrument_overrides;
  };

  struct DecodedPattern {
    Image pattern;
    Image monochrome_pattern;
  };

  struct DecodedString {
    std::string str;
    std::string after_data;
  };

  struct DecodedStringSequence {
    std::vector<std::string> strs;
    std::string after_data;
  };

  struct DecodedCode0Resource {
    uint32_t above_a5_size;
    uint32_t below_a5_size;

    struct JumpTableEntry {
      int16_t code_resource_id; // entry not valid if this is zero
      uint16_t offset; // offset from end of CODE resource header
    };
    std::vector<JumpTableEntry> jump_table;
  };

  struct DecodedCodeResource {
    // if near model, this is >= 0 and the far model fields are uninitialized:
    int32_t entry_offset;

    // if far model, entry_offset is < 0 and these will all be initialized:
    uint32_t near_entry_start_a5_offset; // offset from A5, so subtract 0x20
    uint32_t near_entry_count;
    uint32_t far_entry_start_a5_offset; // offset from A5, so subtract 0x20
    uint32_t far_entry_count;
    uint32_t a5_relocation_data_offset;
    uint32_t a5;
    uint32_t pc_relocation_data_offset;
    uint32_t load_address; // unintuitive; see docs

    std::string code;
  };

  struct DecodedSizeResource {
    bool save_screen;
    bool accept_suspend_events;
    bool disable_option;
    bool can_background;
    bool activate_on_fg_switch;
    bool only_background;
    bool get_front_clicks;
    bool accept_died_events;
    bool clean_addressing; // "32-bit compatible"
    bool high_level_event_aware;
    bool local_and_remote_high_level_events;
    bool stationery_aware;
    bool use_text_edit_services;
    uint32_t size;
    uint32_t min_size;
  };

  // Code resources
  DecodedSizeResource decode_SIZE(int16_t id, uint32_t type = RESOURCE_TYPE_SIZE);
  std::vector<DecodedCodeFragmentEntry> decode_cfrg(int16_t id, uint32_t type = RESOURCE_TYPE_cfrg);
  DecodedCode0Resource decode_CODE_0(int16_t id = 0, uint32_t type = RESOURCE_TYPE_CODE);
  DecodedCodeResource decode_CODE(int16_t id, uint32_t type = RESOURCE_TYPE_CODE);
  std::string decode_dcmp(int16_t id, uint32_t type = RESOURCE_TYPE_dcmp);
  std::string decode_CDEF(int16_t id, uint32_t type = RESOURCE_TYPE_CDEF);
  std::string decode_INIT(int16_t id, uint32_t type = RESOURCE_TYPE_INIT);
  std::string decode_LDEF(int16_t id, uint32_t type = RESOURCE_TYPE_LDEF);
  std::string decode_MDBF(int16_t id, uint32_t type = RESOURCE_TYPE_MDBF);
  std::string decode_MDEF(int16_t id, uint32_t type = RESOURCE_TYPE_MDEF);
  std::string decode_PACK(int16_t id, uint32_t type = RESOURCE_TYPE_PACK);
  std::string decode_PTCH(int16_t id, uint32_t type = RESOURCE_TYPE_PTCH);
  std::string decode_WDEF(int16_t id, uint32_t type = RESOURCE_TYPE_WDEF);
  std::string decode_ADBS(int16_t id, uint32_t type = RESOURCE_TYPE_ADBS);
  std::string decode_clok(int16_t id, uint32_t type = RESOURCE_TYPE_clok);
  std::string decode_proc(int16_t id, uint32_t type = RESOURCE_TYPE_proc);
  std::string decode_ptch(int16_t id, uint32_t type = RESOURCE_TYPE_ptch);
  std::string decode_ROvr(int16_t id, uint32_t type = RESOURCE_TYPE_ROvr);
  std::string decode_SERD(int16_t id, uint32_t type = RESOURCE_TYPE_SERD);
  std::string decode_snth(int16_t id, uint32_t type = RESOURCE_TYPE_snth);
  std::string decode_SMOD(int16_t id, uint32_t type = RESOURCE_TYPE_SMOD);

  // Image resources
  DecodedColorIconResource decode_cicn(int16_t id, uint32_t type = RESOURCE_TYPE_cicn);
  DecodedCursorResource decode_CURS(int16_t id, uint32_t type = RESOURCE_TYPE_CURS);
  DecodedColorCursorResource decode_crsr(int16_t id, uint32_t type = RESOURCE_TYPE_crsr);
  DecodedPattern decode_ppat(int16_t id, uint32_t type = RESOURCE_TYPE_ppat);
  std::vector<DecodedPattern> decode_pptN(int16_t id, uint32_t type = RESOURCE_TYPE_pptN);
  Image decode_PAT(int16_t id, uint32_t type = RESOURCE_TYPE_PAT);
  std::vector<Image> decode_PATN(int16_t id, uint32_t type = RESOURCE_TYPE_PATN);
  std::vector<Image> decode_SICN(int16_t id, uint32_t type = RESOURCE_TYPE_SICN);
  Image decode_icl8(int16_t id, uint32_t type = RESOURCE_TYPE_icl8);
  Image decode_icm8(int16_t id, uint32_t type = RESOURCE_TYPE_icm8);
  Image decode_ics8(int16_t id, uint32_t type = RESOURCE_TYPE_ics8);
  Image decode_kcs8(int16_t id, uint32_t type = RESOURCE_TYPE_kcs8);
  Image decode_icl4(int16_t id, uint32_t type = RESOURCE_TYPE_icl4);
  Image decode_icm4(int16_t id, uint32_t type = RESOURCE_TYPE_icm4);
  Image decode_ics4(int16_t id, uint32_t type = RESOURCE_TYPE_ics4);
  Image decode_kcs4(int16_t id, uint32_t type = RESOURCE_TYPE_kcs4);
  Image decode_ICON(int16_t id, uint32_t type = RESOURCE_TYPE_ICON);
  Image decode_ICNN(int16_t id, uint32_t type = RESOURCE_TYPE_ICNN);
  Image decode_icmN(int16_t id, uint32_t type = RESOURCE_TYPE_icmN);
  Image decode_icsN(int16_t id, uint32_t type = RESOURCE_TYPE_icsN);
  Image decode_kcsN(int16_t id, uint32_t type = RESOURCE_TYPE_kcsN);
  PictRenderResult decode_PICT(int16_t id, uint32_t type = RESOURCE_TYPE_PICT);
  std::vector<Color> decode_pltt(int16_t id, uint32_t type = RESOURCE_TYPE_pltt);
  std::vector<Color> decode_clut(int16_t id, uint32_t type = RESOURCE_TYPE_clut);

  // Sound resources
  // Note: return types may change here in the future to improve structuring and
  // to make it easier for callers of the library to use the returned data in
  // any way other than just saving it to WAV/MIDI files
  DecodedInstrumentResource decode_INST(int16_t id, uint32_t type = RESOURCE_TYPE_INST);
  DecodedSongResource decode_SONG(int16_t id, uint32_t type = RESOURCE_TYPE_SONG);
  // The strings returned by these functions contain raw uncompressed WAV files
  std::string decode_snd(int16_t id, uint32_t type = RESOURCE_TYPE_snd);
  std::string decode_csnd(int16_t id, uint32_t type = RESOURCE_TYPE_csnd);
  std::string decode_esnd(int16_t id, uint32_t type = RESOURCE_TYPE_esnd);
  std::string decode_ESnd(int16_t id, uint32_t type = RESOURCE_TYPE_ESnd);
  std::string decode_SMSD(int16_t id, uint32_t type = RESOURCE_TYPE_SMSD);
  // The strings returned by these functions contain raw MIDI files
  std::string decode_cmid(int16_t id, uint32_t type = RESOURCE_TYPE_cmid);
  std::string decode_emid(int16_t id, uint32_t type = RESOURCE_TYPE_emid);
  std::string decode_ecmi(int16_t id, uint32_t type = RESOURCE_TYPE_ecmi);
  std::string decode_Tune(int16_t id, uint32_t type = RESOURCE_TYPE_Tune);

  // Text resources
  DecodedString decode_STR(int16_t id, uint32_t type = RESOURCE_TYPE_STR);
  DecodedStringSequence decode_STRN(int16_t id, uint32_t type = RESOURCE_TYPE_STRN);
  std::string decode_TEXT(int16_t id, uint32_t type = RESOURCE_TYPE_TEXT);
  std::string decode_styl(int16_t id, uint32_t type = RESOURCE_TYPE_styl);

private:
  scoped_fd fd;

  bool empty;
  ResourceForkHeader header;
  ResourceMapHeader map_header;
  ResourceTypeList map_type_list;
  std::unordered_map<uint32_t, std::vector<ResourceReferenceListEntry>> reference_list_cache;

  std::unordered_map<uint64_t, std::string> resource_data_cache;

  std::vector<ResourceReferenceListEntry>* get_reference_list(uint32_t type);
  std::string decompress_resource(const std::string& data,
      DebuggingMode debug = DebuggingMode::Disabled);
  static const std::string& get_system_decompressor(int16_t resource_id);
};


class SingleResourceFile : public ResourceFile {
public:
  SingleResourceFile(uint32_t type, int16_t id, const void* data, size_t size);
  SingleResourceFile(uint32_t type, int16_t id, const std::string& data);
  virtual ~SingleResourceFile() = default;

  virtual bool resource_exists(uint32_t type, int16_t id);
  virtual std::string get_resource_data(uint32_t type, int16_t id,
      bool decompress = true,
      DebuggingMode decompress_debug = DebuggingMode::Disabled);
  virtual bool resource_is_compressed(uint32_t type, int16_t id);
  virtual std::vector<int16_t> all_resources_of_type(uint32_t type);
  virtual std::vector<std::pair<uint32_t, int16_t>> all_resources();

private:
  uint32_t type;
  int16_t id;
  const std::string data;
};



std::string decode_mac_roman(const char* data, size_t size);
std::string decode_mac_roman(const std::string& data);
