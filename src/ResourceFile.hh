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



#define RESOURCE_TYPE_actb  0x61637462
#define RESOURCE_TYPE_acur  0x61637572
#define RESOURCE_TYPE_ADBS  0x41444253
#define RESOURCE_TYPE_ALRT  0x414C5254
#define RESOURCE_TYPE_APPL  0x4150504C
#define RESOURCE_TYPE_BNDL  0x424E444C
#define RESOURCE_TYPE_bstr  0x62737472
#define RESOURCE_TYPE_card  0x63617264
#define RESOURCE_TYPE_cctb  0x63637462
#define RESOURCE_TYPE_CDEF  0x43444546
#define RESOURCE_TYPE_cdek  0x6364656B
#define RESOURCE_TYPE_cdev  0x63646576
#define RESOURCE_TYPE_cfrg  0x63667267
#define RESOURCE_TYPE_cicn  0x6369636E
#define RESOURCE_TYPE_citt  0x63697474
#define RESOURCE_TYPE_clok  0x636C6F6B
#define RESOURCE_TYPE_clut  0x636C7574
#define RESOURCE_TYPE_CMDK  0x434D444B
#define RESOURCE_TYPE_cmid  0x636D6964
#define RESOURCE_TYPE_CMNU  0x434D4E55
#define RESOURCE_TYPE_cmnu  0x636D6E75
#define RESOURCE_TYPE_cmtb  0x636D7462
#define RESOURCE_TYPE_cmuN  0x636D7521
#define RESOURCE_TYPE_CNTL  0x434E544C
#define RESOURCE_TYPE_CODE  0x434F4445
#define RESOURCE_TYPE_code  0x636F6465
#define RESOURCE_TYPE_crsr  0x63727372
#define RESOURCE_TYPE_csnd  0x63736E64
#define RESOURCE_TYPE_CTYN  0x43545923
#define RESOURCE_TYPE_CURS  0x43555253
#define RESOURCE_TYPE_dcmp  0x64636D70
#define RESOURCE_TYPE_dcod  0x64636F64
#define RESOURCE_TYPE_dctb  0x64637462
#define RESOURCE_TYPE_dem   0x64656D20
#define RESOURCE_TYPE_DITL  0x4449544C
#define RESOURCE_TYPE_DLOG  0x444C4F47
#define RESOURCE_TYPE_DRVR  0x44525652
#define RESOURCE_TYPE_drvr  0x64727672
#define RESOURCE_TYPE_ecmi  0x65636D69
#define RESOURCE_TYPE_emid  0x656D6964
#define RESOURCE_TYPE_enet  0x656E6574
#define RESOURCE_TYPE_epch  0x65706368
#define RESOURCE_TYPE_errs  0x65727273
#define RESOURCE_TYPE_ESnd  0x45536E64
#define RESOURCE_TYPE_esnd  0x65736E64
#define RESOURCE_TYPE_expt  0x65787074
#define RESOURCE_TYPE_FBTN  0x4642544E
#define RESOURCE_TYPE_fctb  0x66637462
#define RESOURCE_TYPE_FDIR  0x46444952
#define RESOURCE_TYPE_finf  0x66696E66
#define RESOURCE_TYPE_fldN  0x666C6423
#define RESOURCE_TYPE_FONT  0x464F4E54
#define RESOURCE_TYPE_fovr  0x666F7672
#define RESOURCE_TYPE_FREF  0x46524546
#define RESOURCE_TYPE_FRSV  0x46525356
#define RESOURCE_TYPE_FWID  0x46574944
#define RESOURCE_TYPE_gcko  0x67636B6F
#define RESOURCE_TYPE_GDEF  0x47444546
#define RESOURCE_TYPE_gdef  0x67646566
#define RESOURCE_TYPE_gnld  0x676E6C64
#define RESOURCE_TYPE_GNRL  0x474E524C
#define RESOURCE_TYPE_gpch  0x67706368
#define RESOURCE_TYPE_hqda  0x68716461
#define RESOURCE_TYPE_hwin  0x6877696E
#define RESOURCE_TYPE_icl4  0x69636C34
#define RESOURCE_TYPE_icl8  0x69636C38
#define RESOURCE_TYPE_icm4  0x69636D34
#define RESOURCE_TYPE_icm8  0x69636D38
#define RESOURCE_TYPE_icmN  0x69636D23
#define RESOURCE_TYPE_icmt  0x69636D74
#define RESOURCE_TYPE_ICNN  0x49434E23
#define RESOURCE_TYPE_icns  0x69636E73
#define RESOURCE_TYPE_ICON  0x49434F4E
#define RESOURCE_TYPE_ics4  0x69637334
#define RESOURCE_TYPE_ics8  0x69637338
#define RESOURCE_TYPE_icsN  0x69637323
#define RESOURCE_TYPE_inbb  0x696E6262
#define RESOURCE_TYPE_indm  0x696E646D
#define RESOURCE_TYPE_infs  0x696E6673
#define RESOURCE_TYPE_INIT  0x494E4954
#define RESOURCE_TYPE_inpk  0x696E706B
#define RESOURCE_TYPE_inra  0x696E7261
#define RESOURCE_TYPE_insc  0x696E7363
#define RESOURCE_TYPE_INST  0x494E5354
#define RESOURCE_TYPE_ITL1  0x49544C31
#define RESOURCE_TYPE_itlb  0x69746C62
#define RESOURCE_TYPE_itlc  0x69746C63
#define RESOURCE_TYPE_itlk  0x69746C6B
#define RESOURCE_TYPE_KBDN  0x4B42444E
#define RESOURCE_TYPE_kcs4  0x6B637334
#define RESOURCE_TYPE_kcs8  0x6B637338
#define RESOURCE_TYPE_kcsN  0x6B637323
#define RESOURCE_TYPE_krnl  0x6B726E6C
#define RESOURCE_TYPE_LAYO  0x4C41594F
#define RESOURCE_TYPE_LDEF  0x4C444546
#define RESOURCE_TYPE_lmgr  0x6C6D6772
#define RESOURCE_TYPE_lodr  0x6C6F6472
#define RESOURCE_TYPE_ltlk  0x6C746C6B
#define RESOURCE_TYPE_MADH  0x4D414448
#define RESOURCE_TYPE_MADI  0x4D414449
#define RESOURCE_TYPE_MBAR  0x4D424152
#define RESOURCE_TYPE_MBDF  0x4D424446
#define RESOURCE_TYPE_mcky  0x6D636B79
#define RESOURCE_TYPE_MDEF  0x4D444546
#define RESOURCE_TYPE_MENU  0x4D454E55
#define RESOURCE_TYPE_MIDI  0x4D494449
#define RESOURCE_TYPE_Midi  0x4D696469
#define RESOURCE_TYPE_midi  0x6D696469
#define RESOURCE_TYPE_MOOV  0x4D4F4F56
#define RESOURCE_TYPE_MooV  0x4D6F6F56
#define RESOURCE_TYPE_moov  0x6D6F6F76
#define RESOURCE_TYPE_ncmp  0x6E636D70
#define RESOURCE_TYPE_ndmc  0x6E646D63
#define RESOURCE_TYPE_ndrv  0x6E647276
#define RESOURCE_TYPE_NFNT  0x4E464E54
#define RESOURCE_TYPE_nift  0x6E696674
#define RESOURCE_TYPE_nitt  0x6E697474
#define RESOURCE_TYPE_nlib  0x6E6C6962
#define RESOURCE_TYPE_nrct  0x6E726374
#define RESOURCE_TYPE_nsnd  0x6E736E64
#define RESOURCE_TYPE_nsrd  0x6E737264
#define RESOURCE_TYPE_ntrb  0x6E747262
#define RESOURCE_TYPE_osl   0x6F736C20
#define RESOURCE_TYPE_otdr  0x6F746472
#define RESOURCE_TYPE_otlm  0x6F746C6D
#define RESOURCE_TYPE_PACK  0x5041434B
#define RESOURCE_TYPE_PAPA  0x50415041
#define RESOURCE_TYPE_PAT   0x50415420
#define RESOURCE_TYPE_PATN  0x50415423
#define RESOURCE_TYPE_PICK  0x5049434B
#define RESOURCE_TYPE_PICT  0x50494354
#define RESOURCE_TYPE_pltt  0x706C7474
#define RESOURCE_TYPE_pnll  0x706E6C6C
#define RESOURCE_TYPE_ppat  0x70706174
#define RESOURCE_TYPE_ppcc  0x70706363
#define RESOURCE_TYPE_ppct  0x70706374
#define RESOURCE_TYPE_pptN  0x70707423
#define RESOURCE_TYPE_PRC0  0x50524330
#define RESOURCE_TYPE_PRC3  0x50524333
#define RESOURCE_TYPE_proc  0x70726F63
#define RESOURCE_TYPE_PSAP  0x50534150
#define RESOURCE_TYPE_PTCH  0x50544348
#define RESOURCE_TYPE_ptch  0x70746368
#define RESOURCE_TYPE_pthg  0x70746867
#define RESOURCE_TYPE_qrsc  0x71727363
#define RESOURCE_TYPE_qtcm  0x7174636D
#define RESOURCE_TYPE_resf  0x72657366
#define RESOURCE_TYPE_RMAP  0x524D4150
#define RESOURCE_TYPE_ROvN  0x524F7623
#define RESOURCE_TYPE_ROvr  0x524F7672
#define RESOURCE_TYPE_RVEW  0x52564557
#define RESOURCE_TYPE_scal  0x7363616C
#define RESOURCE_TYPE_scod  0x73636F64
#define RESOURCE_TYPE_scrn  0x7363726E
#define RESOURCE_TYPE_sect  0x73656374
#define RESOURCE_TYPE_SERD  0x53455244
#define RESOURCE_TYPE_sfnt  0x73666E74
#define RESOURCE_TYPE_sfvr  0x73667672
#define RESOURCE_TYPE_shal  0x7368616C
#define RESOURCE_TYPE_SICN  0x5349434E
#define RESOURCE_TYPE_sift  0x73696674
#define RESOURCE_TYPE_SIGN  0x5349474E
#define RESOURCE_TYPE_SIZE  0x53495A45
#define RESOURCE_TYPE_SMOD  0x534D4F44
#define RESOURCE_TYPE_SMSD  0x534D5344
#define RESOURCE_TYPE_snd   0x736E6420
#define RESOURCE_TYPE_snth  0x736E7468
#define RESOURCE_TYPE_SONG  0x534F4E47
#define RESOURCE_TYPE_STR   0x53545220
#define RESOURCE_TYPE_STRN  0x53545223
#define RESOURCE_TYPE_styl  0x7374796C
#define RESOURCE_TYPE_tdig  0x74646967
#define RESOURCE_TYPE_TEXT  0x54455854
#define RESOURCE_TYPE_TMPL  0x544D504C
#define RESOURCE_TYPE_tokn  0x746F6B6E
#define RESOURCE_TYPE_TOOL  0x544F4F4C
#define RESOURCE_TYPE_Tune  0x54756E65
#define RESOURCE_TYPE_vdig  0x76646967
#define RESOURCE_TYPE_vers  0x76657273
#define RESOURCE_TYPE_wart  0x77617274
#define RESOURCE_TYPE_wctb  0x77637462
#define RESOURCE_TYPE_WDEF  0x57444546
#define RESOURCE_TYPE_WIND  0x57494E44
#define RESOURCE_TYPE_wstr  0x77737472
#define RESOURCE_TYPE_XCMD  0x58434D44
#define RESOURCE_TYPE_XFCN  0x5846434E

std::string string_for_resource_type(uint32_t type);
std::string raw_string_for_resource_type(uint32_t type);



enum DecompressionFlag {
  DISABLED = 0x01,
  VERBOSE = 0x02,
  TRACE = 0x04,
  SKIP_FILE_DCMP = 0x08,
  SKIP_FILE_NCMP = 0x10,
  SKIP_SYSTEM_DCMP = 0x20,
  SKIP_SYSTEM_NCMP = 0x40,
  SKIP_INTERNAL = 0x80,
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

  ResourceFile() = default;
  ResourceFile(const ResourceFile&) = default;
  ResourceFile(ResourceFile&&) = default;
  ResourceFile& operator=(const ResourceFile&) = default;
  ResourceFile& operator=(ResourceFile&&) = default;
  ~ResourceFile() = default;

  void add(const Resource& res);
  void add(Resource&& res);
  void add(const std::vector<Resource>& ress);
  void add(std::vector<Resource>&& ress);

  bool resource_exists(uint32_t type, int16_t id) const;
  bool resource_exists(uint32_t type, const char* name) const;
  const Resource& get_resource(uint32_t type, int16_t id,
      uint64_t decompression_flags = 0);
  const Resource& get_resource(uint32_t type, const char* name,
      uint64_t decompression_flags = 0);
  std::vector<int16_t> all_resources_of_type(uint32_t type) const;
  std::vector<std::pair<uint32_t, int16_t>> all_resources() const;

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
    std::string name;
    // TODO: support extensions
    uint16_t extension_count;
    std::string extension_data;
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

  struct DecodedDriverResource {
    enum Flag {
      EnableRead = 0x0100,
      EnableWrite = 0x0200,
      EnableControl = 0x0400,
      EnableStatus = 0x0800,
      NeedGoodbye = 0x1000,
      NeedTime = 0x2000,
      NeedLock = 0x4000,
    };
    uint16_t flags;
    uint16_t delay;
    uint16_t event_mask;
    int16_t menu_id;
    // If any of these are -1, the label is missing
    int32_t open_label;
    int32_t prime_label;
    int32_t control_label;
    int32_t status_label;
    int32_t close_label;
    std::string name;
    std::string code;
  };

  struct DecodedDecompressorResource {
    int32_t init_label;
    int32_t decompress_label;
    int32_t exit_label;
    uint32_t pc_offset;
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

  struct DecodedVersionResource {
    uint8_t major_version;
    uint8_t minor_version;
    uint8_t development_stage;
    uint8_t prerelease_version_level;
    uint16_t region_code;
    std::string version_number;
    std::string version_message;
  };

  struct DecodedPictResource {
    Image image;
    std::string embedded_image_format;
    std::string embedded_image_data;
  };

  struct DecodedFontResource {
    uint8_t source_bit_depth;
    std::vector<ColorTableEntry> color_table;
    bool is_dynamic;
    bool has_non_black_colors;
    bool fixed_width;
    uint16_t first_char;
    uint16_t last_char;
    uint16_t max_width;
    int16_t max_kerning;
    uint16_t rect_width;
    uint16_t rect_height;
    int16_t max_ascent;
    int16_t max_descent;
    int16_t leading;

    struct Glyph {
      int16_t ch;
      uint16_t bitmap_offset;
      uint16_t bitmap_width;
      int8_t offset;
      uint8_t width;
      Image img;
    };
    Glyph missing_glyph;
    std::vector<Glyph> glyphs;
  };

  enum TextStyleFlag {
    BOLD = 0x01,
    ITALIC = 0x02,
    UNDERLINE = 0x04,
    OUTLINE = 0x08,
    SHADOW = 0x10,
    CONDENSED = 0x20,
    EXTENDED = 0x40,
  };

  struct DecodedFontInfo {
    uint16_t font_id;
    uint16_t style_flags;
    uint16_t size;
  };

  struct DecodedROMOverride {
    uint32_t type;
    int16_t id;

    void byteswap();
  } __attribute__((packed));

  struct DecodedROMOverridesResource {
    uint16_t rom_version;
    std::vector<DecodedROMOverride> overrides;
  };

  struct DecodedPEFFDriver {
    std::string header;
    PEFFFile peff;
  };

  struct TemplateEntry {
    enum class Type {
      VOID, // DVDR
      INTEGER, // DBYT, BWRD, DLNG, HBYT, HWRD, HLNG, CHAR, TNAM
      ALIGNMENT, // AWORD, ALNG
      ZERO_FILL, // FBYT, FWRD, FLNG
      EOF_STRING, // HEXD
      FIXED_POINT, // FIXD (.width is number of bytes for each field)
      POINT_2D, // 'PNT ' (.width is number of bytes for each dimension)
      STRING, // Hxxx (.width is number of bytes)
      PSTRING, // PSTR, WSTR, LSTR, ESTR, OSTR (.width is width of length field)
      CSTRING, // CSTR, ECST, OCST
      FIXED_PSTRING, // P0xx (length byte not included in xx)
      FIXED_CSTRING, // Cxxx
      BOOL, // BOOL (two bytes, for some reason...)
      BITFIELD, // BBIT (list_entries contains 8 BOOL entries)
      RECT, // RECT (.width is width of each field)
      COLOR, // COLR (.width is the width of each channel)
      LIST_ZERO_BYTE, // LSTZ+LSTE
      LIST_ZERO_COUNT, // ZCNT+LSTC+LSTE (count is a word)
      LIST_ONE_COUNT, // OCNT+LSTC+LSTE (count is a word)
      LIST_EOF, // LSTB+LSTE
    };
    enum class Format {
      DECIMAL,
      HEX,
      TEXT, // For integers, this results in hex+text
      FLAG,
      DATE,
    };

    std::string name;
    Type type;
    Format format;
    uint16_t width;
    uint8_t end_alignment;
    uint8_t align_offset; // 1 for odd-aligned strings, for example
    bool is_signed;

    std::vector<std::shared_ptr<TemplateEntry>> list_entries;
    std::map<int32_t, std::string> case_names;

    TemplateEntry(std::string&& name,
        Type type,
        Format format = Format::DECIMAL,
        uint16_t width = 0,
        uint8_t end_alignment = 0,
        uint8_t align_offset = 0,
        bool is_signed = true);
    TemplateEntry(std::string&& name,
        Type type,
        std::vector<std::shared_ptr<TemplateEntry>>&& list_entries);
  };
  using TemplateEntryList = std::vector<std::shared_ptr<ResourceFile::TemplateEntry>>;

  // Meta resources
  TemplateEntryList decode_TMPL(int16_t id, uint32_t type = RESOURCE_TYPE_TMPL);
  static TemplateEntryList decode_TMPL(const Resource& res);
  static TemplateEntryList decode_TMPL(const void* data, size_t size);

  static std::string disassemble_from_template(
      const void* data, size_t size, const TemplateEntryList& tmpl);

  // Code metadata resources
  DecodedSizeResource decode_SIZE(int16_t id, uint32_t type = RESOURCE_TYPE_SIZE);
  static DecodedSizeResource decode_SIZE(const Resource& res);
  static DecodedSizeResource decode_SIZE(const void* data, size_t size);
  DecodedVersionResource decode_vers(int16_t id, uint32_t type = RESOURCE_TYPE_vers);
  static DecodedVersionResource decode_vers(const Resource& res);
  static DecodedVersionResource decode_vers(const void* data, size_t size);
  std::vector<DecodedCodeFragmentEntry> decode_cfrg(int16_t id, uint32_t type = RESOURCE_TYPE_cfrg);
  static std::vector<DecodedCodeFragmentEntry> decode_cfrg(const Resource& res);
  static std::vector<DecodedCodeFragmentEntry> decode_cfrg(const void* vdata, size_t size);
  DecodedROMOverridesResource decode_ROvN(int16_t id, uint32_t type = RESOURCE_TYPE_ROvN);
  static DecodedROMOverridesResource decode_ROvN(const Resource& res);
  static DecodedROMOverridesResource decode_ROvN(const void* data, size_t size);

  // 68K code resources
  DecodedCode0Resource decode_CODE_0(int16_t id = 0, uint32_t type = RESOURCE_TYPE_CODE);
  static DecodedCode0Resource decode_CODE_0(const Resource& res);
  static DecodedCode0Resource decode_CODE_0(const void* vdata, size_t size);
  DecodedCodeResource decode_CODE(int16_t id, uint32_t type = RESOURCE_TYPE_CODE);
  static DecodedCodeResource decode_CODE(const Resource& res);
  static DecodedCodeResource decode_CODE(const void* vdata, size_t size);
  DecodedDriverResource decode_DRVR(int16_t id, uint32_t type = RESOURCE_TYPE_DRVR);
  static DecodedDriverResource decode_DRVR(const Resource& res);
  static DecodedDriverResource decode_DRVR(const void* vdata, size_t size);
  DecodedDecompressorResource decode_dcmp(int16_t id, uint32_t type = RESOURCE_TYPE_dcmp);
  static DecodedDecompressorResource decode_dcmp(const Resource& res);
  static DecodedDecompressorResource decode_dcmp(const void* vdata, size_t size);

  // PowerPC code resources
  PEFFFile decode_peff(int16_t id, uint32_t type);
  static PEFFFile decode_peff(const Resource& res);
  static PEFFFile decode_peff(const void* data, size_t size);
  DecodedPEFFDriver decode_expt(int16_t id, uint32_t type = RESOURCE_TYPE_expt);
  static DecodedPEFFDriver decode_expt(const Resource& res);
  static DecodedPEFFDriver decode_expt(const void* data, size_t size);
  DecodedPEFFDriver decode_nsrd(int16_t id, uint32_t type = RESOURCE_TYPE_nsrd);
  static DecodedPEFFDriver decode_nsrd(const Resource& res);
  static DecodedPEFFDriver decode_nsrd(const void* data, size_t size);

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
  std::vector<ColorTableEntry> decode_fctb(int16_t id, uint32_t type = RESOURCE_TYPE_fctb);
  static std::vector<ColorTableEntry> decode_fctb(const Resource& res);
  static std::vector<ColorTableEntry> decode_fctb(const void* data, size_t size);
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
  std::string decode_card(int16_t id, uint32_t type = RESOURCE_TYPE_STR);
  static std::string decode_card(const Resource& res);
  static std::string decode_card(const void* data, size_t size);
  DecodedStringSequence decode_STRN(int16_t id, uint32_t type = RESOURCE_TYPE_STRN);
  static DecodedStringSequence decode_STRN(const Resource& res);
  static DecodedStringSequence decode_STRN(const void* data, size_t size);
  std::string decode_TEXT(int16_t id, uint32_t type = RESOURCE_TYPE_TEXT);
  static std::string decode_TEXT(const Resource& res);
  static std::string decode_TEXT(const void* data, size_t size);
  std::string decode_styl(int16_t id, uint32_t type = RESOURCE_TYPE_styl);
  std::string decode_styl(const Resource& res);

  // Font resources
  DecodedFontResource decode_FONT(int16_t id, uint32_t type = RESOURCE_TYPE_FONT);
  DecodedFontResource decode_FONT(const Resource& res);
  DecodedFontResource decode_NFNT(int16_t id, uint32_t type = RESOURCE_TYPE_NFNT);
  DecodedFontResource decode_NFNT(const Resource& res);
  std::vector<DecodedFontInfo> decode_finf(int16_t id, uint32_t type = RESOURCE_TYPE_finf);
  static std::vector<DecodedFontInfo> decode_finf(const Resource& res);
  static std::vector<DecodedFontInfo> decode_finf(const void* data, size_t size);

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
  static const std::vector<std::shared_ptr<TemplateEntry>>& get_system_template(uint32_t type);
};



std::string decode_mac_roman(const char* data, size_t size);
std::string decode_mac_roman(const std::string& data);

const char* name_for_region_code(uint16_t region_code);
const char* name_for_font_id(uint16_t font_id);
