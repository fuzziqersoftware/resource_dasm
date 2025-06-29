#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <map>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <unordered_map>
#include <vector>

#include "Emulators/M68KEmulator.hh"
#include "ExecutableFormats/PEFFile.hh"
#include "QuickDrawFormats.hh"
#include "ResourceFormats.hh"
#include "ResourceTypes.hh"

namespace ResourceDASM {

using namespace phosg;

enum class IndexFormat {
  NONE = 0, // For ResourceFiles constructed in memory
  RESOURCE_FORK,
  APPLESINGLE_APPLEDOUBLE,
  MACBINARY,
  MOHAWK,
  HIRF,
  DC_DATA,
  CBAG,
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
  // This class defines the loaded representation of a resource archive, and
  // includes functions to decode resources and add/remove/change the archive
  // contents. To parse an existing archive and get a ResourceFile object, use a
  // function defined in one of the headers in the IndexFormats directory. The
  // constructors defined in this class will only create an empty ResourceFile.

  ResourceFile();
  explicit ResourceFile(IndexFormat format);
  ResourceFile(const ResourceFile&) = default;
  ResourceFile(ResourceFile&&) = default;
  ResourceFile& operator=(const ResourceFile&) = default;
  ResourceFile& operator=(ResourceFile&&) = default;
  ~ResourceFile() = default;

  struct Resource {
    uint32_t type;
    int16_t id;
    uint16_t flags; // bits from ResourceFlag enum
    std::string name;
    std::string data;
    std::shared_ptr<const Resource> decompressed_resource;

    Resource();
    Resource(const Resource&) = default;
    Resource(Resource&&) = default;
    Resource& operator=(const Resource&) = default;
    Resource& operator=(Resource&&) = default;
    Resource(uint32_t type, int16_t id, const std::string& data);
    Resource(uint32_t type, int16_t id, std::string&& data);
    Resource(uint32_t type, int16_t id, uint16_t flags, const std::string& name, const std::string& data);
    Resource(uint32_t type, int16_t id, uint16_t flags, std::string&& name, std::string&& data);
  };

  // add() does not overwrite a resource if one already exists with the same
  // name. To replace an existing resource, remove() it first. (Note that
  // remove() will invalidate all references to the deleted resource that were
  // previously returned by get_resource().)
  bool add(const Resource& res);
  bool add(Resource&& res);
  bool add(std::shared_ptr<Resource> res);
  bool remove(uint32_t type, int16_t id);
  bool change_id(uint32_t type, int16_t current_id, int16_t new_id);
  bool rename(uint32_t type, int16_t id, const std::string& new_name);

  IndexFormat index_format() const;

  bool empty() const;
  bool resource_exists(uint32_t type, int16_t id) const;
  bool resource_exists(uint32_t type, const char* name) const;
  std::shared_ptr<const Resource> get_resource(uint32_t type, int16_t id, uint64_t decompression_flags = 0) const;
  std::shared_ptr<const Resource> get_resource(uint32_t type, const char* name, uint64_t decompression_flags = 0) const;
  const std::string& get_resource_name(uint32_t type, int16_t id) const;
  size_t count_resources_of_type(uint32_t type) const;
  size_t count_resources() const;
  std::vector<int16_t> all_resources_of_type(uint32_t type) const;
  std::vector<uint32_t> all_resource_types() const;
  std::vector<std::pair<uint32_t, int16_t>> all_resources() const;

  uint32_t find_resource_by_id(int16_t id, const std::vector<uint32_t>& types) const;

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
    ImageRGBA8888 image;
    ImageGA11 bitmap;
  };

  struct DecodedIconListResource {
    // .empty() will be true for exactly one of these in all cases.
    // Specifically, if there are two icons in the resource, it is assumed that
    // they are a bitmap and mask (respectively) and they are combined into
    // .composite; for any other number of images, no compositing is done and
    // the images are decoded individually and put into .images.
    ImageGA11 composite;
    std::vector<ImageG1> images;
  };

  struct DecodedIconImagesResource {
    std::unordered_multimap<uint32_t, ImageRGBA8888> type_to_image;
    std::unordered_multimap<uint32_t, ImageRGBA8888> type_to_composite_image;
    std::unordered_multimap<uint32_t, std::string> type_to_jpeg2000_data;
    std::unordered_multimap<uint32_t, std::string> type_to_png_data;
    std::string toc_data;
    float icon_composer_version;
    std::string name;
    std::string info_plist;
    std::shared_ptr<DecodedIconImagesResource> template_icns;
    std::shared_ptr<DecodedIconImagesResource> selected_icns;
    std::shared_ptr<DecodedIconImagesResource> dark_icns;

    DecodedIconImagesResource();
  };

  struct DecodedCursorResource {
    ImageGA11 bitmap;
    uint16_t hotspot_x;
    uint16_t hotspot_y;
  };

  struct DecodedColorCursorResource {
    ImageRGBA8888 image;
    ImageGA11 bitmap;
    uint16_t hotspot_x;
    uint16_t hotspot_y;
  };

  struct DecodedSoundResource {
    bool is_mp3 = false;
    uint32_t sample_rate = 0;
    uint8_t base_note = 0;

    uint8_t num_channels = 0;
    uint8_t bits_per_sample = 0;

    size_t loop_start_sample_offset = 0;
    size_t loop_end_sample_offset = 0;
    size_t loop_repeat_count = 0; // 0 = loop forever
    enum class LoopType {
      NORMAL = 0,
      ALTERNATE = 1,
      REVERSE = 2,
    };
    LoopType loop_type = LoopType::NORMAL;

    // This string contains a raw WAV or MP3 file (determined by is_mp3); in
    // the WAV case, the actual samples start at sample_start_offset within the
    // data string
    size_t sample_start_offset = 0;
    std::string data;
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
    std::vector<uint16_t> tremolo_data;
    std::string copyright;
    std::string author;
  };

  struct DecodedSongResource {
    bool is_rmf;
    int16_t midi_id;
    // 0 = private, 1 = RMF structured, 2 = RMF linear, -1 = standard MIDI (SMS)
    int16_t midi_format;
    uint16_t tempo_bias; // base = 16667; linear
    uint16_t volume_bias; // base = 127; linear
    int16_t semitone_shift;
    int16_t percussion_instrument; // -1 = unspecified (RMF)
    int16_t note_decay; // In 1/60ths; -1 = unspecified
    bool allow_program_change;
    std::unordered_map<uint16_t, uint16_t> instrument_overrides;

    std::string copyright_text;
    std::string composer; // Called "author" in SMS-format songs

    // The following fields are only present in RMF-format songs, and all are
    // optional.
    std::vector<uint16_t> velocity_override_map;
    std::string title;
    std::string performer;
    std::string copyright_date;
    std::string license_contact;
    std::string license_uses;
    std::string license_domain;
    std::string license_term;
    std::string license_expiration;
    std::string note;
    std::string index_number;
    std::string genre;
    std::string subgenre;
  };

  struct DecodedPattern {
    ImageRGB888 pattern;
    ImageG1 monochrome_pattern;
    uint64_t raw_monochrome_pattern; // MSB is top-left, bits proceed in English reading order
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
    std::vector<JumpTableEntry> jump_table;
  };

  struct DecodedCodeResource {
    // if near model, this is >= 0 and the far model fields are uninitialized:
    int32_t first_jump_table_entry;
    uint16_t num_jump_table_entries;

    // if far model, entry_offset is < 0 and these will all be initialized:
    uint32_t near_entry_start_a5_offset; // offset from A5, so subtract 0x20
    uint32_t near_entry_count;
    uint32_t far_entry_start_a5_offset; // offset from A5, so subtract 0x20
    uint32_t far_entry_count;
    uint32_t a5_relocation_data_offset;
    uint32_t a5;
    uint32_t pc_relocation_data_offset;
    uint32_t load_address; // unintuitive; see docs

    std::vector<uint32_t> a5_relocation_addresses;
    std::vector<uint32_t> pc_relocation_addresses;

    std::string code;
  };

  struct DecodedDriverResource {
    enum Flag {
      ENABLE_READ = 0x0100,
      ENABLE_WRITE = 0x0200,
      ENABLE_CONTROL = 0x0400,
      ENABLE_STATUS = 0x0800,
      NEED_GOODBYE = 0x1000,
      NEED_TIME = 0x2000,
      NEED_LOCK = 0x4000,
    };
    uint16_t flags;
    uint16_t delay;
    uint16_t event_mask;
    int16_t menu_id;
    uint16_t open_label;
    uint16_t prime_label;
    uint16_t control_label;
    uint16_t status_label;
    uint16_t close_label;
    std::string name;
    size_t code_start_offset; // Used as start_address during disassembly
    std::string code;
  };

  struct DecodedRSSCResource {
    // Note: The start offset of the code is 0x16, so function_offsets will be
    // either 0 (no function) or >= 0x16
    uint16_t function_offsets[9];
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
    ImageRGBA8888 image;
    std::string embedded_image_format;
    std::string embedded_image_data;
  };

  struct DecodedFontResource {
    // See Inside Macintosh: Text page 4-9 for descriptions of these terms
    uint8_t source_bit_depth; // 1, 2, 4, or 8 (TODO: we only support 1 for now)
    std::vector<ColorTableEntry> color_table; // Unused (TODO)
    bool is_dynamic;
    bool has_non_black_colors; // Unused (TODO)
    bool fixed_width;
    uint16_t first_char; // Character code corresponding to glyphs[0]
    uint16_t last_char; // Character code corresponding to glyphs[glyphs.size() - 1]
    uint16_t max_width; // Maximum width of any glyph
    int16_t max_kerning;
    uint16_t rect_width;
    uint16_t rect_height;
    int16_t max_ascent;
    int16_t max_descent;
    int16_t leading; // Space between bottom of bitmap (descent) and top of next line

    ImageG1 full_bitmap;

    struct Glyph {
      int16_t ch;
      uint16_t bitmap_offset;
      uint16_t bitmap_width;
      int8_t offset;
      uint8_t width;
      ImageGA11 img;
    };
    Glyph missing_glyph;
    std::vector<Glyph> glyphs;

    const Glyph& glyph_for_char(uint16_t ch) const;
    Glyph& glyph_for_char(uint16_t ch);
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
    be_uint32_t type;
    be_int16_t id;
  } __attribute__((packed));

  struct DecodedROMOverridesResource {
    uint16_t rom_version;
    std::vector<DecodedROMOverride> overrides;
  };

  struct DecodedPEFDriver {
    std::string header;
    PEFFile pef;
  };

  struct DecodedDialogItem {
    enum class Type {
      BUTTON, // text valid
      CHECKBOX, // text valid
      RADIO_BUTTON, // text valid
      RESOURCE_CONTROL, // resource_id valid
      HELP_BALLOON,
      TEXT, // text valid
      EDIT_TEXT, // text valid
      ICON, // resource_id valid
      PICTURE, // resource_id valid
      CUSTOM, // neither resource_id nor text valid
      UNKNOWN, // text contains raw info string (may be binary data!)
    };

    Rect bounds;
    bool enabled;
    Type type;
    uint8_t raw_type;
    int16_t resource_id;
    std::string text;
  };

  struct DecodedDialog {
    Rect bounds;
    int16_t proc_id;
    bool visible;
    bool go_away;
    int32_t ref_con;
    int16_t items_id;
    std::string title;
    uint16_t auto_position; // See AUTO_POSITION_NAMES
  };

  struct DecodedWindow {
    Rect bounds;
    int16_t proc_id;
    bool visible;
    bool go_away;
    int32_t ref_con;
    std::string title;
    uint16_t auto_position; // See AUTO_POSITION_NAMES
  };

  struct DecodedUIControl {
    Rect bounds;
    int16_t value;
    bool visible;
    int16_t max;
    int16_t min;
    int16_t proc_id;
    int32_t ref_con;
    std::string title;
  };

  struct DecodedMenu {
    int16_t menu_id;
    int16_t proc_id;
    std::string title;
    bool enabled;
    struct Item {
      std::string name;
      uint8_t icon_number;
      char key_equivalent;
      char mark_character; // In MacRoman; use decode_mac_roman if needed
      uint8_t style_flags; // See TextStyleFlag
      bool enabled;
    };
    std::vector<Item> items;
  };

  struct DecodedKeyCharMap {
    struct DeadKey {
      struct Completion {
        uint8_t completion_char;
        uint8_t substitute_char;
      };
      uint8_t table_index;
      uint8_t virtual_key_code;
      std::vector<Completion> completions;
      Completion no_match_completion;
    };
    std::array<uint8_t, 0x100> table_index_for_modifiers;
    std::vector<std::array<uint8_t, 0x80>> tables;
    std::vector<DeadKey> dead_keys;
  };

  struct TemplateEntry {
    enum class Type {
      VOID, // DVDR
      INTEGER, // DBYT, DWRD, DLNG, HBYT, HWRD, HLNG, CHAR, TNAM, RSID
      ALIGNMENT, // AWRD, ALNG
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
      OPT_EOF, // If there's still data left
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

    std::vector<std::unique_ptr<TemplateEntry>> list_entries;
    std::map<int64_t, std::string> case_names;

    TemplateEntry(std::string&& name,
        Type type,
        Format format = Format::DECIMAL,
        uint16_t width = 0,
        uint8_t end_alignment = 0,
        uint8_t align_offset = 0,
        bool is_signed = true,
        std::map<int64_t, std::string> case_names = {});
    TemplateEntry(std::string&& name,
        Type type,
        std::vector<std::unique_ptr<TemplateEntry>>&& list_entries);
  };
  using TemplateEntryList = std::vector<std::unique_ptr<ResourceFile::TemplateEntry>>;

  // Meta resources
  TemplateEntryList decode_TMPL(int16_t id, uint32_t type = RESOURCE_TYPE_TMPL) const;
  static TemplateEntryList decode_TMPL(std::shared_ptr<const Resource> res);
  static TemplateEntryList decode_TMPL(const void* data, size_t size);

  static std::string describe_template(const TemplateEntryList& tmpl);
  static std::string disassemble_from_template(const void* data, size_t size, const TemplateEntryList& tmpl);

  // Code metadata resources
  DecodedSizeResource decode_SIZE(int16_t id, uint32_t type = RESOURCE_TYPE_SIZE) const;
  static DecodedSizeResource decode_SIZE(std::shared_ptr<const Resource> res);
  static DecodedSizeResource decode_SIZE(const void* data, size_t size);
  DecodedVersionResource decode_vers(int16_t id, uint32_t type = RESOURCE_TYPE_vers) const;
  static DecodedVersionResource decode_vers(std::shared_ptr<const Resource> res);
  static DecodedVersionResource decode_vers(const void* data, size_t size);
  std::vector<DecodedCodeFragmentEntry> decode_cfrg(int16_t id, uint32_t type = RESOURCE_TYPE_cfrg) const;
  static std::vector<DecodedCodeFragmentEntry> decode_cfrg(std::shared_ptr<const Resource> res);
  static std::vector<DecodedCodeFragmentEntry> decode_cfrg(const void* vdata, size_t size);
  DecodedROMOverridesResource decode_ROvN(int16_t id, uint32_t type = RESOURCE_TYPE_ROvN) const;
  static DecodedROMOverridesResource decode_ROvN(std::shared_ptr<const Resource> res);
  static DecodedROMOverridesResource decode_ROvN(const void* data, size_t size);

  // 68K code resources
  DecodedCode0Resource decode_CODE_0(int16_t id = 0, uint32_t type = RESOURCE_TYPE_CODE) const;
  static DecodedCode0Resource decode_CODE_0(std::shared_ptr<const Resource> res);
  static DecodedCode0Resource decode_CODE_0(const void* vdata, size_t size);
  DecodedCodeResource decode_CODE(int16_t id, uint32_t type = RESOURCE_TYPE_CODE) const;
  static DecodedCodeResource decode_CODE(std::shared_ptr<const Resource> res);
  static DecodedCodeResource decode_CODE(const void* vdata, size_t size);
  DecodedDriverResource decode_DRVR(int16_t id, uint32_t type = RESOURCE_TYPE_DRVR) const;
  static DecodedDriverResource decode_DRVR(std::shared_ptr<const Resource> res);
  static DecodedDriverResource decode_DRVR(const void* vdata, size_t size);
  DecodedDecompressorResource decode_dcmp(int16_t id, uint32_t type = RESOURCE_TYPE_dcmp) const;
  static DecodedDecompressorResource decode_dcmp(std::shared_ptr<const Resource> res);
  static DecodedDecompressorResource decode_dcmp(const void* vdata, size_t size);
  DecodedRSSCResource decode_RSSC(int16_t id, uint32_t type = RESOURCE_TYPE_RSSC) const;
  static DecodedRSSCResource decode_RSSC(std::shared_ptr<const Resource> res);
  static DecodedRSSCResource decode_RSSC(const void* vdata, size_t size);

  // PowerPC code resources
  PEFFile decode_pef(int16_t id, uint32_t type) const;
  static PEFFile decode_pef(std::shared_ptr<const Resource> res);
  static PEFFile decode_pef(const void* data, size_t size);
  DecodedPEFDriver decode_expt(int16_t id, uint32_t type = RESOURCE_TYPE_expt) const;
  static DecodedPEFDriver decode_expt(std::shared_ptr<const Resource> res);
  static DecodedPEFDriver decode_expt(const void* data, size_t size);
  DecodedPEFDriver decode_nsrd(int16_t id, uint32_t type = RESOURCE_TYPE_nsrd) const;
  static DecodedPEFDriver decode_nsrd(std::shared_ptr<const Resource> res);
  static DecodedPEFDriver decode_nsrd(const void* data, size_t size);

  // Image resources
  DecodedColorIconResource decode_cicn(int16_t id, uint32_t type = RESOURCE_TYPE_cicn) const;
  static DecodedColorIconResource decode_cicn(std::shared_ptr<const Resource> res);
  static DecodedColorIconResource decode_cicn(const void* vdata, size_t size);
  DecodedCursorResource decode_CURS(int16_t id, uint32_t type = RESOURCE_TYPE_CURS) const;
  static DecodedCursorResource decode_CURS(std::shared_ptr<const Resource> res);
  static DecodedCursorResource decode_CURS(const void* data, size_t size);
  DecodedColorCursorResource decode_crsr(int16_t id, uint32_t type = RESOURCE_TYPE_crsr) const;
  static DecodedColorCursorResource decode_crsr(std::shared_ptr<const Resource> res);
  static DecodedColorCursorResource decode_crsr(const void* data, size_t size);
  DecodedPattern decode_ppat(int16_t id, uint32_t type = RESOURCE_TYPE_ppat) const;
  static DecodedPattern decode_ppat(std::shared_ptr<const Resource> res);
  static DecodedPattern decode_ppat(const void* data, size_t size);
  std::vector<DecodedPattern> decode_pptN(int16_t id, uint32_t type = RESOURCE_TYPE_pptN) const;
  static std::vector<DecodedPattern> decode_pptN(std::shared_ptr<const Resource> res);
  static std::vector<DecodedPattern> decode_pptN(const void* data, size_t size);
  ImageG1 decode_PAT(int16_t id, uint32_t type = RESOURCE_TYPE_PAT) const;
  static ImageG1 decode_PAT(std::shared_ptr<const Resource> res);
  static ImageG1 decode_PAT(const void* data, size_t size);
  std::vector<ImageG1> decode_PATN(int16_t id, uint32_t type = RESOURCE_TYPE_PATN) const;
  static std::vector<ImageG1> decode_PATN(std::shared_ptr<const Resource> res);
  static std::vector<ImageG1> decode_PATN(const void* data, size_t size);
  std::vector<ImageG1> decode_SICN(int16_t id, uint32_t type = RESOURCE_TYPE_SICN) const;
  static std::vector<ImageG1> decode_SICN(std::shared_ptr<const Resource> res);
  static std::vector<ImageG1> decode_SICN(const void* data, size_t size);
  ImageRGBA8888 decode_icl8(int16_t id, uint32_t type = RESOURCE_TYPE_icl8) const;
  ImageRGBA8888 decode_icl8(std::shared_ptr<const Resource> res) const;
  static ImageRGB888 decode_icl8_without_alpha(const void* data, size_t size);
  ImageRGBA8888 decode_icm8(int16_t id, uint32_t type = RESOURCE_TYPE_icm8) const;
  ImageRGBA8888 decode_icm8(std::shared_ptr<const Resource> res) const;
  static ImageRGB888 decode_icm8_without_alpha(const void* data, size_t size);
  ImageRGBA8888 decode_ics8(int16_t id, uint32_t type = RESOURCE_TYPE_ics8) const;
  ImageRGBA8888 decode_ics8(std::shared_ptr<const Resource> res) const;
  static ImageRGB888 decode_ics8_without_alpha(const void* data, size_t size);
  ImageRGBA8888 decode_kcs8(int16_t id, uint32_t type = RESOURCE_TYPE_kcs8) const;
  ImageRGBA8888 decode_kcs8(std::shared_ptr<const Resource> res) const;
  static ImageRGB888 decode_kcs8_without_alpha(const void* data, size_t size);
  ImageRGBA8888 decode_icl4(int16_t id, uint32_t type = RESOURCE_TYPE_icl4) const;
  ImageRGBA8888 decode_icl4(std::shared_ptr<const Resource> res) const;
  static ImageRGB888 decode_icl4_without_alpha(const void* data, size_t size);
  ImageRGBA8888 decode_icm4(int16_t id, uint32_t type = RESOURCE_TYPE_icm4) const;
  ImageRGBA8888 decode_icm4(std::shared_ptr<const Resource> res) const;
  static ImageRGB888 decode_icm4_without_alpha(const void* data, size_t size);
  ImageRGBA8888 decode_ics4(int16_t id, uint32_t type = RESOURCE_TYPE_ics4) const;
  ImageRGBA8888 decode_ics4(std::shared_ptr<const Resource> res) const;
  static ImageRGB888 decode_ics4_without_alpha(const void* data, size_t size);
  ImageRGBA8888 decode_kcs4(int16_t id, uint32_t type = RESOURCE_TYPE_kcs4) const;
  ImageRGBA8888 decode_kcs4(std::shared_ptr<const Resource> res) const;
  static ImageRGB888 decode_kcs4_without_alpha(const void* data, size_t size);
  ImageG1 decode_ICON(int16_t id, uint32_t type = RESOURCE_TYPE_ICON) const;
  static ImageG1 decode_ICON(std::shared_ptr<const Resource> res);
  static ImageG1 decode_ICON(const void* data, size_t size);
  DecodedIconListResource decode_ICNN(int16_t id, uint32_t type = RESOURCE_TYPE_ICNN) const;
  static DecodedIconListResource decode_ICNN(std::shared_ptr<const Resource> res);
  static DecodedIconListResource decode_ICNN(const void* data, size_t size);
  DecodedIconListResource decode_icmN(int16_t id, uint32_t type = RESOURCE_TYPE_icmN) const;
  static DecodedIconListResource decode_icmN(std::shared_ptr<const Resource> res);
  static DecodedIconListResource decode_icmN(const void* data, size_t size);
  DecodedIconListResource decode_icsN(int16_t id, uint32_t type = RESOURCE_TYPE_icsN) const;
  static DecodedIconListResource decode_icsN(std::shared_ptr<const Resource> res);
  static DecodedIconListResource decode_icsN(const void* data, size_t size);
  DecodedIconListResource decode_kcsN(int16_t id, uint32_t type = RESOURCE_TYPE_kcsN) const;
  static DecodedIconListResource decode_kcsN(std::shared_ptr<const Resource> res);
  static DecodedIconListResource decode_kcsN(const void* data, size_t size);
  DecodedIconImagesResource decode_icns(int16_t id, uint32_t type = RESOURCE_TYPE_icns) const;
  static DecodedIconImagesResource decode_icns(std::shared_ptr<const Resource> res);
  static DecodedIconImagesResource decode_icns(const void* data, size_t size);
  DecodedPictResource decode_PICT(int16_t id, uint32_t type = RESOURCE_TYPE_PICT, bool allow_external = true) const;
  DecodedPictResource decode_PICT(std::shared_ptr<const Resource> res, bool allow_external = true) const;
  DecodedPictResource decode_PICT(const void* data, size_t size, bool allow_external = true) const;
  static DecodedPictResource decode_PICT_only(std::shared_ptr<const Resource> res, bool allow_external = true);
  static DecodedPictResource decode_PICT_only(const void* data, size_t size, bool allow_external = true);
  std::vector<Color> decode_pltt(int16_t id, uint32_t type = RESOURCE_TYPE_pltt) const;
  static std::vector<Color> decode_pltt(std::shared_ptr<const Resource> res);
  static std::vector<Color> decode_pltt(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_clut(int16_t id, uint32_t type = RESOURCE_TYPE_clut) const;
  static std::vector<ColorTableEntry> decode_clut(std::shared_ptr<const Resource> res);
  static std::vector<ColorTableEntry> decode_clut(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_actb(int16_t id, uint32_t type = RESOURCE_TYPE_actb) const;
  static std::vector<ColorTableEntry> decode_actb(std::shared_ptr<const Resource> res);
  static std::vector<ColorTableEntry> decode_actb(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_cctb(int16_t id, uint32_t type = RESOURCE_TYPE_cctb) const;
  static std::vector<ColorTableEntry> decode_cctb(std::shared_ptr<const Resource> res);
  static std::vector<ColorTableEntry> decode_cctb(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_dctb(int16_t id, uint32_t type = RESOURCE_TYPE_dctb) const;
  static std::vector<ColorTableEntry> decode_dctb(std::shared_ptr<const Resource> res);
  static std::vector<ColorTableEntry> decode_dctb(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_fctb(int16_t id, uint32_t type = RESOURCE_TYPE_fctb) const;
  static std::vector<ColorTableEntry> decode_fctb(std::shared_ptr<const Resource> res);
  static std::vector<ColorTableEntry> decode_fctb(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_wctb(int16_t id, uint32_t type = RESOURCE_TYPE_wctb) const;
  static std::vector<ColorTableEntry> decode_wctb(std::shared_ptr<const Resource> res);
  static std::vector<ColorTableEntry> decode_wctb(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_CTBL(int16_t id, uint32_t type = RESOURCE_TYPE_CTBL) const;
  static std::vector<ColorTableEntry> decode_CTBL(std::shared_ptr<const Resource> res);
  static std::vector<ColorTableEntry> decode_CTBL(const void* data, size_t size);

  // Sound resources
  // Note: return types may change here in the future to improve structuring and
  // to make it easier for callers of the library to use the returned data in
  // any way other than just saving it to WAV/MIDI files
  DecodedInstrumentResource decode_INST(int16_t id, uint32_t type = RESOURCE_TYPE_INST) const;
  DecodedInstrumentResource decode_INST(std::shared_ptr<const Resource> res) const;
  // Note: The SONG format depends on the resource index format, so there are no
  // static versions of this function.
  DecodedSongResource decode_SONG(int16_t id, uint32_t type = RESOURCE_TYPE_SONG) const;
  DecodedSongResource decode_SONG(std::shared_ptr<const Resource> res) const;
  DecodedSongResource decode_SONG(const void* data, size_t size) const;
  // If metadata_only is true, the .data field in the returned struct will be
  // empty. This saves time when generating SONG JSONs, for example.
  static DecodedSoundResource decode_snd_data(
      const void* vdata, size_t size, bool metadata_only = false, bool hirf_semantics = false, bool decompress_ysnd = false);
  DecodedSoundResource decode_snd(int16_t id, uint32_t type = RESOURCE_TYPE_snd, bool metadata_only = false) const;
  DecodedSoundResource decode_snd(std::shared_ptr<const Resource> res, bool metadata_only = false) const;
  DecodedSoundResource decode_snd(const void* data, size_t size, bool metadata_only = false) const;
  DecodedSoundResource decode_csnd(int16_t id, uint32_t type = RESOURCE_TYPE_csnd, bool metadata_only = false) const;
  DecodedSoundResource decode_csnd(std::shared_ptr<const Resource> res, bool metadata_only = false) const;
  DecodedSoundResource decode_csnd(const void* data, size_t size, bool metadata_only = false) const;
  DecodedSoundResource decode_esnd(int16_t id, uint32_t type = RESOURCE_TYPE_esnd, bool metadata_only = false) const;
  DecodedSoundResource decode_esnd(std::shared_ptr<const Resource> res, bool metadata_only = false) const;
  DecodedSoundResource decode_esnd(const void* data, size_t size, bool metadata_only = false) const;
  DecodedSoundResource decode_ESnd(int16_t id, uint32_t type = RESOURCE_TYPE_ESnd, bool metadata_only = false) const;
  DecodedSoundResource decode_ESnd(std::shared_ptr<const Resource> res, bool metadata_only = false) const;
  DecodedSoundResource decode_ESnd(const void* data, size_t size, bool metadata_only = false) const;
  DecodedSoundResource decode_Ysnd(int16_t id, uint32_t type, bool metadata_only = false) const;
  DecodedSoundResource decode_Ysnd(std::shared_ptr<const Resource> res, bool metadata_only = false) const;
  DecodedSoundResource decode_Ysnd(const void* vdata, size_t size, bool metadata_only = false) const;
  // These function return a string containing a raw WAV file.
  std::string decode_SMSD(int16_t id, uint32_t type = RESOURCE_TYPE_SMSD) const;
  static std::string decode_SMSD(std::shared_ptr<const Resource> res);
  static std::string decode_SMSD(const void* data, size_t size);
  std::string decode_SOUN(int16_t id, uint32_t type = RESOURCE_TYPE_SOUN) const;
  static std::string decode_SOUN(std::shared_ptr<const Resource> res);
  static std::string decode_SOUN(const void* data, size_t size);
  // The strings returned by these functions contain raw MIDI files
  std::string decode_cmid(int16_t id, uint32_t type = RESOURCE_TYPE_cmid) const;
  static std::string decode_cmid(std::shared_ptr<const Resource> res);
  static std::string decode_cmid(const void* data, size_t size);
  std::string decode_emid(int16_t id, uint32_t type = RESOURCE_TYPE_emid) const;
  static std::string decode_emid(std::shared_ptr<const Resource> res);
  static std::string decode_emid(const void* data, size_t size);
  std::string decode_ecmi(int16_t id, uint32_t type = RESOURCE_TYPE_ecmi) const;
  static std::string decode_ecmi(std::shared_ptr<const Resource> res);
  static std::string decode_ecmi(const void* data, size_t size);
  std::string decode_Tune(int16_t id, uint32_t type = RESOURCE_TYPE_Tune) const;
  static std::string decode_Tune(std::shared_ptr<const Resource> res);
  static std::string decode_Tune(const void* data, size_t size);

  // Text resources
  DecodedString decode_STR(int16_t id, uint32_t type = RESOURCE_TYPE_STR) const;
  static DecodedString decode_STR(std::shared_ptr<const Resource> res);
  static DecodedString decode_STR(const void* data, size_t size);
  std::string decode_card(int16_t id, uint32_t type = RESOURCE_TYPE_STR) const;
  static std::string decode_card(std::shared_ptr<const Resource> res);
  static std::string decode_card(const void* data, size_t size);
  DecodedStringSequence decode_STRN(int16_t id, uint32_t type = RESOURCE_TYPE_STRN) const;
  static DecodedStringSequence decode_STRN(std::shared_ptr<const Resource> res);
  static DecodedStringSequence decode_STRN(const void* data, size_t size);
  std::string decode_STRN_entry(int16_t id, size_t index, uint32_t type = RESOURCE_TYPE_STRN) const;
  static std::string decode_STRN_entry(std::shared_ptr<const Resource> res, size_t index);
  static std::string decode_STRN_entry(const void* data, size_t size, size_t index);
  std::vector<std::string> decode_TwCS(int16_t id, uint32_t type = RESOURCE_TYPE_TwCS) const;
  static std::vector<std::string> decode_TwCS(std::shared_ptr<const Resource> res);
  static std::vector<std::string> decode_TwCS(const void* data, size_t size);
  std::string decode_TEXT(int16_t id, uint32_t type = RESOURCE_TYPE_TEXT) const;
  static std::string decode_TEXT(std::shared_ptr<const Resource> res);
  static std::string decode_TEXT(const void* data, size_t size);
  std::string decode_styl(int16_t id, uint32_t type = RESOURCE_TYPE_styl) const;
  std::string decode_styl(std::shared_ptr<const Resource> res) const;
  DecodedKeyCharMap decode_KCHR(int16_t id, uint32_t type = RESOURCE_TYPE_KCHR) const;
  static DecodedKeyCharMap decode_KCHR(std::shared_ptr<const Resource> res);
  static DecodedKeyCharMap decode_KCHR(const void* data, size_t size);

  // Font resources
  DecodedFontResource decode_FONT(int16_t id, uint32_t type = RESOURCE_TYPE_FONT) const;
  DecodedFontResource decode_FONT(std::shared_ptr<const Resource> res) const;
  DecodedFontResource decode_FONT(const void* data, size_t size, int16_t res_id = 0) const;
  static DecodedFontResource decode_FONT_only(std::shared_ptr<const Resource> res);
  static DecodedFontResource decode_FONT_only(const void* data, size_t size);
  DecodedFontResource decode_NFNT(int16_t id, uint32_t type = RESOURCE_TYPE_NFNT) const;
  DecodedFontResource decode_NFNT(std::shared_ptr<const Resource> res) const;
  DecodedFontResource decode_NFNT(const void* data, size_t size, int16_t res_id = 0) const;
  static DecodedFontResource decode_NFNT_only(std::shared_ptr<const Resource> res);
  static DecodedFontResource decode_NFNT_only(const void* data, size_t size);
  std::vector<DecodedFontInfo> decode_finf(int16_t id, uint32_t type = RESOURCE_TYPE_finf) const;
  static std::vector<DecodedFontInfo> decode_finf(std::shared_ptr<const Resource> res);
  static std::vector<DecodedFontInfo> decode_finf(const void* data, size_t size);

  // Dialog/layout resources
  DecodedUIControl decode_CNTL(int16_t id, uint32_t type = RESOURCE_TYPE_CNTL) const;
  static DecodedUIControl decode_CNTL(std::shared_ptr<const Resource> res);
  static DecodedUIControl decode_CNTL(const void* data, size_t size);
  DecodedDialog decode_DLOG(int16_t id, uint32_t type = RESOURCE_TYPE_DLOG) const;
  static DecodedDialog decode_DLOG(std::shared_ptr<const Resource> res);
  static DecodedDialog decode_DLOG(const void* data, size_t size);
  DecodedWindow decode_WIND(int16_t id, uint32_t type = RESOURCE_TYPE_WIND) const;
  static DecodedWindow decode_WIND(std::shared_ptr<const Resource> res);
  static DecodedWindow decode_WIND(const void* data, size_t size);
  std::vector<DecodedDialogItem> decode_DITL(int16_t id, uint32_t type = RESOURCE_TYPE_DITL) const;
  static std::vector<DecodedDialogItem> decode_DITL(std::shared_ptr<const Resource> res);
  static std::vector<DecodedDialogItem> decode_DITL(const void* data, size_t size);
  DecodedMenu decode_MENU(int16_t id, uint32_t type = RESOURCE_TYPE_MENU) const;
  static DecodedMenu decode_MENU(std::shared_ptr<const Resource> res);
  static DecodedMenu decode_MENU(const void* data, size_t size);

private:
  IndexFormat format;
  // Note: It's important that this is not an unordered_map because we expect
  // all_resources to always return resources of the same type contiguously
  // ordered by their ID
  std::map<uint64_t, std::shared_ptr<Resource>> key_to_resource;
  mutable std::map<uint64_t, std::shared_ptr<Resource>> key_to_decompressed_resource;
  std::multimap<std::string, std::shared_ptr<Resource>> name_to_resource;
  std::unordered_map<int16_t, std::shared_ptr<Resource>> system_dcmp_cache;

  std::shared_ptr<const Resource> decompress_if_requested(std::shared_ptr<Resource> res, uint64_t decompress_flags) const;

  DecodedInstrumentResource decode_INST_recursive(
      std::shared_ptr<const Resource> res,
      std::unordered_set<int16_t>& ids_in_progress) const;

  static DecodedFontResource decode_FONT_data(const void* data, size_t size, const ResourceFile* rf, int16_t res_id);
  static DecodedPictResource decode_PICT_data(const void* data, size_t size, const ResourceFile* rf, bool allow_external);

  void add_name_index_entry(std::shared_ptr<Resource> res);
  void delete_name_index_entry(std::shared_ptr<Resource> res);

  static uint64_t make_resource_key(uint32_t type, int16_t id);
  static uint32_t type_from_resource_key(uint64_t key);
  static int16_t id_from_resource_key(uint64_t key);

  static const std::vector<std::shared_ptr<TemplateEntry>>& get_system_template(
      uint32_t type);
};

} // namespace ResourceDASM
