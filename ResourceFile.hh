#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <map>
#include <unordered_map>
#include <vector>

#include "QuickDrawFormats.hh"
#include "PEFFFile.hh"



// TODO:
// mctb (http://mirror.informatimago.com/next/developer.apple.com/documentation/mac/Toolbox/Toolbox-185.html)
// ictb (http://mirror.informatimago.com/next/developer.apple.com/documentation/mac/Toolbox/Toolbox-441.html)
// MBAR (http://mirror.informatimago.com/next/developer.apple.com/documentation/mac/Toolbox/Toolbox-184.html)
// MENU (http://mirror.informatimago.com/next/developer.apple.com/documentation/mac/Toolbox/Toolbox-183.html)
// vers (http://mirror.informatimago.com/next/developer.apple.com/documentation/mac/Toolbox/Toolbox-487.html)



#define RESOURCE_TYPE_actb  0x61637462
#define RESOURCE_TYPE_ADBS  0x41444253
#define RESOURCE_TYPE_cctb  0x63637462
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
#define RESOURCE_TYPE_dctb  0x64637462
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
#define RESOURCE_TYPE_MADI  0x4D414449
#define RESOURCE_TYPE_MDBF  0x4D444246
#define RESOURCE_TYPE_MDEF  0x4D444546
#define RESOURCE_TYPE_MIDI  0x4D494449
#define RESOURCE_TYPE_Midi  0x4D696469
#define RESOURCE_TYPE_midi  0x6D696469
#define RESOURCE_TYPE_MOOV  0x4D4F4F56
#define RESOURCE_TYPE_MooV  0x4D6F6F56
#define RESOURCE_TYPE_moov  0x6D6F6F76
#define RESOURCE_TYPE_ncmp  0x6E636D70
#define RESOURCE_TYPE_ndmc  0x6E646D63
#define RESOURCE_TYPE_ndrv  0x6E647276
#define RESOURCE_TYPE_nift  0x6E696674
#define RESOURCE_TYPE_nitt  0x6E697474
#define RESOURCE_TYPE_nlib  0x6E6C6962
#define RESOURCE_TYPE_nsnd  0x6E736E64
#define RESOURCE_TYPE_ntrb  0x6E747262
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
#define RESOURCE_TYPE_wctb  0x77637462
#define RESOURCE_TYPE_WDEF  0x57444546

std::string string_for_resource_type(uint32_t type);



enum DecompressionFlag {
  DISABLED = 0x01,
  VERBOSE = 0x02,
  SKIP_FILE_DCMP = 0x04,
  SKIP_FILE_NCMP = 0x08,
  SKIP_SYSTEM_DCMP = 0x10,
  SKIP_SYSTEM_NCMP = 0x20,
};

enum ResourceFlag {
  // The low 8 bits come from the resource itself; the high 8 bits are reserved
  // for resource_dasm
  FLAG_DECOMPRESSED = 0x0200, // decompressor ran successfully
  FLAG_DECOMPRESSION_FAILED = 0x0100, // so we don't try to decompress again
  FLAG_LOAD_IN_SYSTEM_HEAP = 0x0040,
  FLAG_PURGEABLE = 0x0020,
  FLAG_LOCKED = 0x0010,
  FLAG_PROTECTED = 0x0008,
  FLAG_PRELOAD = 0x0004,
  FLAG_DIRTY = 0x0002, // only used while loaded; set if needs to be written to disk
  FLAG_COMPRESSED = 0x0001,
};



class ResourceFile {
public:
  struct Resource {
    uint32_t type;
    int16_t id;
    uint16_t flags; // bits from ResourceFlag enum
    std::string name;
    std::string data;

    Resource();
    Resource(uint32_t type, int16_t id, const std::string& data);
    Resource(uint32_t type, int16_t id, std::string&& data);
    Resource(uint32_t type, int16_t id, uint16_t flags, const std::string& name, const std::string& data);
    Resource(uint32_t type, int16_t id, uint16_t flags, std::string&& name, std::string&& data);
  };

  // File-parsing constructors
  ResourceFile(const std::string& data);
  ResourceFile(const void* data, size_t size);

  // Existing-resource constructors
  ResourceFile(const Resource& res);
  ResourceFile(Resource&& res);
  ResourceFile(const std::vector<Resource>& ress);
  ResourceFile(std::vector<Resource>&& ress);

  ~ResourceFile() = default;

  bool resource_exists(uint32_t type, int16_t id);
  bool resource_exists(uint32_t type, const char* name);
  const Resource& get_resource(uint32_t type, int16_t id,
      uint64_t decompression_flags = 0);
  const Resource& get_resource(uint32_t type, const char* name,
      uint64_t decompression_flags = 0);
  std::vector<int16_t> all_resources_of_type(uint32_t type);
  std::vector<std::pair<uint32_t, int16_t>> all_resources();

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

  struct DecodedPictResource {
    Image image;
    std::string embedded_image_format;
    std::string embedded_image_data;
  };

  // Code metadata resources
  DecodedSizeResource decode_SIZE(int16_t id, uint32_t type = RESOURCE_TYPE_SIZE);
  static DecodedSizeResource decode_SIZE(const Resource& res);
  static DecodedSizeResource decode_SIZE(const void* data, size_t size);
  std::vector<DecodedCodeFragmentEntry> decode_cfrg(int16_t id, uint32_t type = RESOURCE_TYPE_cfrg);
  static std::vector<DecodedCodeFragmentEntry> decode_cfrg(const Resource& res);
  static std::vector<DecodedCodeFragmentEntry> decode_cfrg(const void* vdata, size_t size);

  // 68K code resources
  DecodedCode0Resource decode_CODE_0(int16_t id = 0, uint32_t type = RESOURCE_TYPE_CODE);
  static DecodedCode0Resource decode_CODE_0(const Resource& res);
  static DecodedCode0Resource decode_CODE_0(const void* vdata, size_t size);
  DecodedCodeResource decode_CODE(int16_t id, uint32_t type = RESOURCE_TYPE_CODE);
  static DecodedCodeResource decode_CODE(const Resource& res);
  static DecodedCodeResource decode_CODE(const void* vdata, size_t size);
  std::string decode_dcmp(int16_t id, uint32_t type = RESOURCE_TYPE_dcmp);
  static std::string decode_dcmp(const Resource& res);
  static std::string decode_dcmp(const void* vdata, size_t size);
  std::string decode_CDEF(int16_t id, uint32_t type = RESOURCE_TYPE_CDEF);
  static std::string decode_CDEF(const Resource& res);
  static std::string decode_CDEF(const void* data, size_t size);
  std::string decode_INIT(int16_t id, uint32_t type = RESOURCE_TYPE_INIT);
  static std::string decode_INIT(const Resource& res);
  static std::string decode_INIT(const void* data, size_t size);
  std::string decode_LDEF(int16_t id, uint32_t type = RESOURCE_TYPE_LDEF);
  static std::string decode_LDEF(const Resource& res);
  static std::string decode_LDEF(const void* data, size_t size);
  std::string decode_MDBF(int16_t id, uint32_t type = RESOURCE_TYPE_MDBF);
  static std::string decode_MDBF(const Resource& res);
  static std::string decode_MDBF(const void* data, size_t size);
  std::string decode_MDEF(int16_t id, uint32_t type = RESOURCE_TYPE_MDEF);
  static std::string decode_MDEF(const Resource& res);
  static std::string decode_MDEF(const void* data, size_t size);
  std::string decode_PACK(int16_t id, uint32_t type = RESOURCE_TYPE_PACK);
  static std::string decode_PACK(const Resource& res);
  static std::string decode_PACK(const void* data, size_t size);
  std::string decode_PTCH(int16_t id, uint32_t type = RESOURCE_TYPE_PTCH);
  static std::string decode_PTCH(const Resource& res);
  static std::string decode_PTCH(const void* data, size_t size);
  std::string decode_WDEF(int16_t id, uint32_t type = RESOURCE_TYPE_WDEF);
  static std::string decode_WDEF(const Resource& res);
  static std::string decode_WDEF(const void* data, size_t size);
  std::string decode_ADBS(int16_t id, uint32_t type = RESOURCE_TYPE_ADBS);
  static std::string decode_ADBS(const Resource& res);
  static std::string decode_ADBS(const void* data, size_t size);
  std::string decode_clok(int16_t id, uint32_t type = RESOURCE_TYPE_clok);
  static std::string decode_clok(const Resource& res);
  static std::string decode_clok(const void* data, size_t size);
  std::string decode_proc(int16_t id, uint32_t type = RESOURCE_TYPE_proc);
  static std::string decode_proc(const Resource& res);
  static std::string decode_proc(const void* data, size_t size);
  std::string decode_ptch(int16_t id, uint32_t type = RESOURCE_TYPE_ptch);
  static std::string decode_ptch(const Resource& res);
  static std::string decode_ptch(const void* data, size_t size);
  std::string decode_ROvr(int16_t id, uint32_t type = RESOURCE_TYPE_ROvr);
  static std::string decode_ROvr(const Resource& res);
  static std::string decode_ROvr(const void* data, size_t size);
  std::string decode_SERD(int16_t id, uint32_t type = RESOURCE_TYPE_SERD);
  static std::string decode_SERD(const Resource& res);
  static std::string decode_SERD(const void* data, size_t size);
  std::string decode_snth(int16_t id, uint32_t type = RESOURCE_TYPE_snth);
  static std::string decode_snth(const Resource& res);
  static std::string decode_snth(const void* data, size_t size);
  std::string decode_SMOD(int16_t id, uint32_t type = RESOURCE_TYPE_SMOD);
  static std::string decode_SMOD(const Resource& res);
  static std::string decode_SMOD(const void* data, size_t size);

  // PowerPC code resources
  PEFFFile decode_ncmp(int16_t id, uint32_t type = RESOURCE_TYPE_ncmp);
  static PEFFFile decode_ncmp(const Resource& res);
  static PEFFFile decode_ncmp(const void* data, size_t size);
  PEFFFile decode_ndmc(int16_t id, uint32_t type = RESOURCE_TYPE_ndmc);
  static PEFFFile decode_ndmc(const Resource& res);
  static PEFFFile decode_ndmc(const void* data, size_t size);
  PEFFFile decode_ndrv(int16_t id, uint32_t type = RESOURCE_TYPE_ndrv);
  static PEFFFile decode_ndrv(const Resource& res);
  static PEFFFile decode_ndrv(const void* data, size_t size);
  PEFFFile decode_nift(int16_t id, uint32_t type = RESOURCE_TYPE_nift);
  static PEFFFile decode_nift(const Resource& res);
  static PEFFFile decode_nift(const void* data, size_t size);
  PEFFFile decode_nitt(int16_t id, uint32_t type = RESOURCE_TYPE_nitt);
  static PEFFFile decode_nitt(const Resource& res);
  static PEFFFile decode_nitt(const void* data, size_t size);
  PEFFFile decode_nlib(int16_t id, uint32_t type = RESOURCE_TYPE_nlib);
  static PEFFFile decode_nlib(const Resource& res);
  static PEFFFile decode_nlib(const void* data, size_t size);
  PEFFFile decode_nsnd(int16_t id, uint32_t type = RESOURCE_TYPE_nsnd);
  static PEFFFile decode_nsnd(const Resource& res);
  static PEFFFile decode_nsnd(const void* data, size_t size);
  PEFFFile decode_ntrb(int16_t id, uint32_t type = RESOURCE_TYPE_ntrb);
  static PEFFFile decode_ntrb(const Resource& res);
  static PEFFFile decode_ntrb(const void* data, size_t size);

  // Image resources
  DecodedColorIconResource decode_cicn(int16_t id, uint32_t type = RESOURCE_TYPE_cicn);
  static DecodedColorIconResource decode_cicn(const Resource& res);
  static DecodedColorIconResource decode_cicn(const void* vdata, size_t size);
  DecodedCursorResource decode_CURS(int16_t id, uint32_t type = RESOURCE_TYPE_CURS);
  static DecodedCursorResource decode_CURS(const Resource& res);
  static DecodedCursorResource decode_CURS(const void* data, size_t size);
  DecodedColorCursorResource decode_crsr(int16_t id, uint32_t type = RESOURCE_TYPE_crsr);
  static DecodedColorCursorResource decode_crsr(const Resource& res);
  static DecodedColorCursorResource decode_crsr(const void* data, size_t size);
  DecodedPattern decode_ppat(int16_t id, uint32_t type = RESOURCE_TYPE_ppat);
  static DecodedPattern decode_ppat(const Resource& res);
  static DecodedPattern decode_ppat(const void* data, size_t size);
  std::vector<DecodedPattern> decode_pptN(int16_t id, uint32_t type = RESOURCE_TYPE_pptN);
  static std::vector<DecodedPattern> decode_pptN(const Resource& res);
  static std::vector<DecodedPattern> decode_pptN(const void* data, size_t size);
  Image decode_PAT(int16_t id, uint32_t type = RESOURCE_TYPE_PAT);
  static Image decode_PAT(const Resource& res);
  static Image decode_PAT(const void* data, size_t size);
  std::vector<Image> decode_PATN(int16_t id, uint32_t type = RESOURCE_TYPE_PATN);
  static std::vector<Image> decode_PATN(const Resource& res);
  static std::vector<Image> decode_PATN(const void* data, size_t size);
  std::vector<Image> decode_SICN(int16_t id, uint32_t type = RESOURCE_TYPE_SICN);
  static std::vector<Image> decode_SICN(const Resource& res);
  static std::vector<Image> decode_SICN(const void* data, size_t size);
  Image decode_icl8(int16_t id, uint32_t type = RESOURCE_TYPE_icl8);
  Image decode_icl8(const Resource& res);
  Image decode_icm8(int16_t id, uint32_t type = RESOURCE_TYPE_icm8);
  Image decode_icm8(const Resource& res);
  Image decode_ics8(int16_t id, uint32_t type = RESOURCE_TYPE_ics8);
  Image decode_ics8(const Resource& res);
  Image decode_kcs8(int16_t id, uint32_t type = RESOURCE_TYPE_kcs8);
  Image decode_kcs8(const Resource& res);
  Image decode_icl4(int16_t id, uint32_t type = RESOURCE_TYPE_icl4);
  Image decode_icl4(const Resource& res);
  Image decode_icm4(int16_t id, uint32_t type = RESOURCE_TYPE_icm4);
  Image decode_icm4(const Resource& res);
  Image decode_ics4(int16_t id, uint32_t type = RESOURCE_TYPE_ics4);
  Image decode_ics4(const Resource& res);
  Image decode_kcs4(int16_t id, uint32_t type = RESOURCE_TYPE_kcs4);
  Image decode_kcs4(const Resource& res);
  Image decode_ICON(int16_t id, uint32_t type = RESOURCE_TYPE_ICON);
  static Image decode_ICON(const Resource& res);
  static Image decode_ICON(const void* data, size_t size);
  Image decode_ICNN(int16_t id, uint32_t type = RESOURCE_TYPE_ICNN);
  static Image decode_ICNN(const Resource& res);
  static Image decode_ICNN(const void* data, size_t size);
  Image decode_icmN(int16_t id, uint32_t type = RESOURCE_TYPE_icmN);
  static Image decode_icmN(const Resource& res);
  static Image decode_icmN(const void* data, size_t size);
  Image decode_icsN(int16_t id, uint32_t type = RESOURCE_TYPE_icsN);
  static Image decode_icsN(const Resource& res);
  static Image decode_icsN(const void* data, size_t size);
  Image decode_kcsN(int16_t id, uint32_t type = RESOURCE_TYPE_kcsN);
  static Image decode_kcsN(const Resource& res);
  static Image decode_kcsN(const void* data, size_t size);
  DecodedPictResource decode_PICT(int16_t id, uint32_t type = RESOURCE_TYPE_PICT);
  DecodedPictResource decode_PICT(const Resource& res);
  DecodedPictResource decode_PICT_internal(int16_t id, uint32_t type = RESOURCE_TYPE_PICT);
  DecodedPictResource decode_PICT_internal(const Resource& res);
  Image decode_PICT_external(int16_t id, uint32_t type = RESOURCE_TYPE_PICT);
  static Image decode_PICT_external(const Resource& res);
  static Image decode_PICT_external(const void* data, size_t size);
  std::vector<Color> decode_pltt(int16_t id, uint32_t type = RESOURCE_TYPE_pltt);
  static std::vector<Color> decode_pltt(const Resource& res);
  static std::vector<Color> decode_pltt(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_clut(int16_t id, uint32_t type = RESOURCE_TYPE_clut);
  static std::vector<ColorTableEntry> decode_clut(const Resource& res);
  static std::vector<ColorTableEntry> decode_clut(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_actb(int16_t id, uint32_t type = RESOURCE_TYPE_actb);
  static std::vector<ColorTableEntry> decode_actb(const Resource& res);
  static std::vector<ColorTableEntry> decode_actb(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_cctb(int16_t id, uint32_t type = RESOURCE_TYPE_cctb);
  static std::vector<ColorTableEntry> decode_cctb(const Resource& res);
  static std::vector<ColorTableEntry> decode_cctb(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_dctb(int16_t id, uint32_t type = RESOURCE_TYPE_dctb);
  static std::vector<ColorTableEntry> decode_dctb(const Resource& res);
  static std::vector<ColorTableEntry> decode_dctb(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_wctb(int16_t id, uint32_t type = RESOURCE_TYPE_wctb);
  static std::vector<ColorTableEntry> decode_wctb(const Resource& res);
  static std::vector<ColorTableEntry> decode_wctb(const void* data, size_t size);

  // Sound resources
  // Note: return types may change here in the future to improve structuring and
  // to make it easier for callers of the library to use the returned data in
  // any way other than just saving it to WAV/MIDI files
  DecodedInstrumentResource decode_INST(int16_t id, uint32_t type = RESOURCE_TYPE_INST);
  DecodedInstrumentResource decode_INST(const Resource& res);
  DecodedSongResource decode_SONG(int16_t id, uint32_t type = RESOURCE_TYPE_SONG);
  static DecodedSongResource decode_SONG(const Resource& res);
  static DecodedSongResource decode_SONG(const void* data, size_t size);
  // The strings returned by these functions contain raw uncompressed WAV files
  std::string decode_snd(int16_t id, uint32_t type = RESOURCE_TYPE_snd);
  static std::string decode_snd(const Resource& res);
  static std::string decode_snd(const void* data, size_t size);
  std::string decode_csnd(int16_t id, uint32_t type = RESOURCE_TYPE_csnd);
  static std::string decode_csnd(const Resource& res);
  static std::string decode_csnd(const void* data, size_t size);
  std::string decode_esnd(int16_t id, uint32_t type = RESOURCE_TYPE_esnd);
  static std::string decode_esnd(const Resource& res);
  static std::string decode_esnd(const void* data, size_t size);
  std::string decode_ESnd(int16_t id, uint32_t type = RESOURCE_TYPE_ESnd);
  static std::string decode_ESnd(const Resource& res);
  static std::string decode_ESnd(const void* data, size_t size);
  std::string decode_SMSD(int16_t id, uint32_t type = RESOURCE_TYPE_SMSD);
  static std::string decode_SMSD(const Resource& res);
  static std::string decode_SMSD(const void* data, size_t size);
  // The strings returned by these functions contain raw MIDI files
  std::string decode_cmid(int16_t id, uint32_t type = RESOURCE_TYPE_cmid);
  static std::string decode_cmid(const Resource& res);
  static std::string decode_cmid(const void* data, size_t size);
  std::string decode_emid(int16_t id, uint32_t type = RESOURCE_TYPE_emid);
  static std::string decode_emid(const Resource& res);
  static std::string decode_emid(const void* data, size_t size);
  std::string decode_ecmi(int16_t id, uint32_t type = RESOURCE_TYPE_ecmi);
  static std::string decode_ecmi(const Resource& res);
  static std::string decode_ecmi(const void* data, size_t size);
  std::string decode_Tune(int16_t id, uint32_t type = RESOURCE_TYPE_Tune);
  static std::string decode_Tune(const Resource& res);
  static std::string decode_Tune(const void* data, size_t size);

  // Text resources
  DecodedString decode_STR(int16_t id, uint32_t type = RESOURCE_TYPE_STR);
  static DecodedString decode_STR(const Resource& res);
  static DecodedString decode_STR(const void* data, size_t size);
  DecodedStringSequence decode_STRN(int16_t id, uint32_t type = RESOURCE_TYPE_STRN);
  static DecodedStringSequence decode_STRN(const Resource& res);
  static DecodedStringSequence decode_STRN(const void* data, size_t size);
  std::string decode_TEXT(int16_t id, uint32_t type = RESOURCE_TYPE_TEXT);
  static std::string decode_TEXT(const Resource& res);
  static std::string decode_TEXT(const void* data, size_t size);
  std::string decode_styl(int16_t id, uint32_t type = RESOURCE_TYPE_styl);
  std::string decode_styl(const Resource& res);

private:
  std::map<uint64_t, Resource> resources;
  std::multimap<std::string, uint64_t> name_to_resource_key;
  std::unordered_map<int16_t, Resource> system_dcmp_cache;

  static uint64_t make_resource_key(uint32_t type, int16_t id);
  static uint32_t type_from_resource_key(uint64_t key);
  static int16_t id_from_resource_key(uint64_t key);
  void parse_structure(StringReader& r);

  std::string decompress_resource(const std::string& data, uint64_t flags);
  static const Resource& get_system_decompressor(bool use_ncmp, int16_t resource_id);
};



std::string decode_mac_roman(const char* data, size_t size);
std::string decode_mac_roman(const std::string& data);
