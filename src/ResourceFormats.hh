#pragma once

#include <phosg/Encoding.hh>

namespace ResourceDASM {

////////////////////////////////////////////////////////////////////////////////
// Common structures

struct Point {
  phosg::be_int16_t y;
  phosg::be_int16_t x;

  Point() = default;
  Point(int16_t y, int16_t x);

  bool operator==(const Point& other) const;
  bool operator!=(const Point& other) const;

  std::string str() const;
} __attribute__((packed));

struct Rect {
  phosg::be_int16_t y1;
  phosg::be_int16_t x1;
  phosg::be_int16_t y2;
  phosg::be_int16_t x2;

  Rect() = default;
  Rect(int16_t y1, int16_t x1, int16_t y2, int16_t x2);

  bool operator==(const Rect& other) const;
  bool operator!=(const Rect& other) const;

  bool contains(ssize_t x, ssize_t y) const;
  bool contains(const Rect& other) const;
  ssize_t width() const;
  ssize_t height() const;

  bool is_empty() const;

  Rect anchor(int16_t x = 0, int16_t y = 0) const;

  std::string str() const;
} __attribute__((packed));

union Fixed {
  struct {
    phosg::be_int16_t whole;
    phosg::be_uint16_t decimal;
  } __attribute__((packed)) parts;
  phosg::be_int32_t value;

  Fixed();
  Fixed(int16_t whole, uint16_t decimal);

  double as_double() const;
} __attribute__((packed));

struct Polygon {
  phosg::be_uint16_t size;
  Rect bounds;
  Point points[0];
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// Bitmaps and pixmaps (used in multiple QuickDraw resources)

struct BitMapHeader {
  phosg::be_uint16_t flags_row_bytes;
  Rect bounds;

  inline size_t bytes() const {
    return (this->flags_row_bytes & 0x3FFF) * bounds.height();
  }
} __attribute__((packed));

struct PixelMapHeader {
  /* 00 */ phosg::be_uint16_t flags_row_bytes;
  /* 02 */ Rect bounds;
  /* 0A */ phosg::be_uint16_t version;
  /* 0C */ phosg::be_uint16_t pack_format;
  /* 0E */ phosg::be_uint32_t pack_size;
  /* 12 */ phosg::be_uint32_t h_res;
  /* 16 */ phosg::be_uint32_t v_res;
  /* 1A */ phosg::be_uint16_t pixel_type;
  /* 1C */ phosg::be_uint16_t pixel_size; // bits per pixel
  /* 1E */ phosg::be_uint16_t component_count;
  /* 20 */ phosg::be_uint16_t component_size;
  /* 22 */ phosg::be_uint32_t plane_offset;
  /* 26 */ phosg::be_uint32_t color_table_offset; // when in memory, handle to color table
  /* 2A */ phosg::be_uint32_t reserved;
  /* 2E */
} __attribute__((packed));

struct PixelMapData {
  uint8_t data[0];

  uint32_t lookup_entry(uint16_t pixel_size, size_t row_bytes, size_t x, size_t y) const;
  static size_t size(uint16_t row_bytes, size_t h);
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// clut, pltt

struct Color8 {
  uint8_t r;
  uint8_t g;
  uint8_t b;

  Color8() = default;
  Color8(uint32_t c);
  Color8(uint8_t r, uint8_t g, uint8_t b);

  constexpr uint32_t rgba8888(uint8_t alpha = 0xFF) const {
    return phosg::rgba8888(this->r, this->g, this->b, alpha);
  }
} __attribute__((packed));

struct Color {
  phosg::be_uint16_t r;
  phosg::be_uint16_t g;
  phosg::be_uint16_t b;

  Color() = default;
  Color(uint16_t r, uint16_t g, uint16_t b);

  Color8 as8() const;
  uint64_t to_u64() const;
  constexpr uint32_t rgba8888(uint8_t a = 0xFF) const {
    return phosg::rgba8888(this->r / 0x0101, this->g / 0x0101, this->b / 0x0101, a);
  }
} __attribute__((packed));

struct ColorTableEntry {
  phosg::be_uint16_t color_num;
  Color c;
} __attribute__((packed));

struct ColorTable {
  phosg::be_uint32_t seed;
  phosg::be_uint16_t flags;
  phosg::be_int16_t num_entries; // actually num_entries - 1
  ColorTableEntry entries[0];

  static std::shared_ptr<ColorTable> from_entries(
      const std::vector<ColorTableEntry>& entries);

  size_t size() const;
  uint32_t get_num_entries() const;
  const ColorTableEntry* get_entry(int16_t id) const;
} __attribute__((packed));

struct PaletteEntry {
  Color c;
  phosg::be_uint16_t usage;
  phosg::be_uint16_t tolerance;
  phosg::be_uint16_t private_flags;
  phosg::be_uint32_t unused;
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// PAT#

struct Pattern {
  union {
    uint8_t rows[8];
    uint64_t pattern;
  };

  Pattern(uint64_t pattern);

  bool pixel_at(uint8_t x, uint8_t y) const;
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// SIZE

struct SizeResource { // SIZE
  phosg::be_uint16_t flags;
  phosg::be_uint32_t size;
  phosg::be_uint32_t min_size;
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// cfrg

struct CodeFragmentResourceEntry {
  phosg::be_uint32_t architecture;
  phosg::be_uint16_t reserved1;
  uint8_t reserved2;
  uint8_t update_level;
  phosg::be_uint32_t current_version;
  phosg::be_uint32_t old_def_version;
  phosg::be_uint32_t app_stack_size;
  union {
    phosg::be_int16_t app_subdir_id;
    phosg::be_uint16_t lib_flags;
  } __attribute__((packed)) flags;

  // Values for usage:
  // kImportLibraryCFrag   = 0 // Standard CFM import library
  // kApplicationCFrag     = 1 // MacOS application
  // kDropInAdditionCFrag  = 2 // Application or library private extension/plug-in
  // kStubLibraryCFrag     = 3 // Import library used for linking only
  // kWeakStubLibraryCFrag = 4 // Import library used for linking only and will be automatically weak linked
  uint8_t usage;

  // Values for where:
  // kMemoryCFragLocator        = 0 // Container is already addressable
  // kDataForkCFragLocator      = 1 // Container is in a file's data fork
  // kResourceCFragLocator      = 2 // Container is in a file's resource fork
  // kByteStreamCFragLocator    = 3 // Reserved
  // kNamedFragmentCFragLocator = 4 // Reserved
  uint8_t where;

  phosg::be_uint32_t offset;
  phosg::be_uint32_t length; // If zero, fragment fills the entire space (e.g. entire data fork)
  union {
    phosg::be_uint32_t space_id;
    phosg::be_uint32_t fork_kind;
  } __attribute__((packed)) space;
  phosg::be_uint16_t fork_instance;
  phosg::be_uint16_t extension_count;
  phosg::be_uint16_t entry_size; // Total size of this entry (incl. name) in bytes
  char name[0]; // p-string (first byte is length)
} __attribute__((packed));

struct CodeFragmentResourceHeader { // cfrg
  phosg::be_uint32_t reserved1;
  phosg::be_uint32_t reserved2;
  phosg::be_uint16_t reserved3;
  phosg::be_uint16_t version;
  phosg::be_uint32_t reserved4;
  phosg::be_uint32_t reserved5;
  phosg::be_uint32_t reserved6;
  phosg::be_uint32_t reserved7;
  phosg::be_uint16_t reserved8;
  phosg::be_uint16_t entry_count;
  // Entries immediately follow this field
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// CODE

struct Code0ResourceHeader {
  phosg::be_uint32_t above_a5_size;
  phosg::be_uint32_t below_a5_size;
  phosg::be_uint32_t jump_table_size; // Should be == resource_size - 0x10
  phosg::be_uint32_t jump_table_offset;

  struct MethodEntry {
    phosg::be_uint16_t offset; // Need to add 4 to this apparently
    phosg::be_uint16_t push_opcode;
    phosg::be_int16_t resource_id; // id of target CODE resource
    phosg::be_uint16_t trap_opcode; // Disassembles as `trap _LoadSeg`
  } __attribute__((packed));

  MethodEntry entries[0];
} __attribute__((packed));

struct CodeResourceHeader {
  phosg::be_uint16_t first_jump_table_entry;
  phosg::be_uint16_t num_jump_table_entries;
} __attribute__((packed));

struct CodeResourceFarHeader {
  phosg::be_uint16_t entry_offset; // 0xFFFF
  phosg::be_uint16_t unused; // 0x0000
  phosg::be_uint32_t near_entry_start_a5_offset;
  phosg::be_uint32_t near_entry_count;
  phosg::be_uint32_t far_entry_start_a5_offset;
  phosg::be_uint32_t far_entry_count;
  phosg::be_uint32_t a5_relocation_data_offset;
  phosg::be_uint32_t a5;
  phosg::be_uint32_t pc_relocation_data_offset;
  phosg::be_uint32_t load_address;
  phosg::be_uint32_t reserved; // 0x00000000
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// DRVR

struct DriverResourceHeader {
  phosg::be_uint16_t flags;
  phosg::be_uint16_t delay;
  phosg::be_uint16_t event_mask;
  phosg::be_int16_t menu_id;
  phosg::be_uint16_t open_label;
  phosg::be_uint16_t prime_label;
  phosg::be_uint16_t control_label;
  phosg::be_uint16_t status_label;
  phosg::be_uint16_t close_label;
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// RSSC

struct RSSCResourceHeader {
  phosg::be_uint32_t type_signature; // == RESOURCE_TYPE_RSSC
  // TODO: Figure out what these functions actually do and name them. 6-8 appear
  // to always be unused, so they may not actually be function offsets.
  phosg::be_uint16_t functions[9];
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// cicn

struct ColorIconResourceHeader {
  // pixMap fields
  phosg::be_uint32_t pix_map_unused;
  PixelMapHeader pix_map;

  // mask bitmap fields
  phosg::be_uint32_t mask_unused;
  BitMapHeader mask_header;

  // 1-bit icon bitmap fields
  phosg::be_uint32_t bitmap_unused;
  BitMapHeader bitmap_header;

  // icon data fields
  phosg::be_uint32_t icon_data; // ignored
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// crsr

struct ColorCursorResourceHeader {
  phosg::be_uint16_t type; // 0x8000 (monochrome) or 0x8001 (color)
  phosg::be_uint32_t pixel_map_offset; // offset from beginning of resource data
  phosg::be_uint32_t pixel_data_offset; // offset from beginning of resource data
  phosg::be_uint32_t expanded_data; // ignore this (Color QuickDraw stuff)
  phosg::be_uint16_t expanded_depth;
  phosg::be_uint32_t unused;
  uint8_t bitmap[0x20];
  uint8_t mask[0x20];
  phosg::be_uint16_t hotspot_y;
  phosg::be_uint16_t hotspot_x;
  phosg::be_uint32_t color_table_offset; // offset from beginning of resource
  phosg::be_uint32_t cursor_id; // ignore this (resource id)
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// ppat

struct PixelPatternResourceHeader {
  phosg::be_uint16_t type;
  phosg::be_uint32_t pixel_map_offset;
  phosg::be_uint32_t pixel_data_offset;
  phosg::be_uint32_t unused1; // TMPL: "Expanded pixel image" (probably ptr to decompressed data when used by QuickDraw)
  phosg::be_uint16_t unused2; // TMPL: "Pattern valid flag" (unused in stored resource)
  phosg::be_uint32_t reserved; // TMPL: "Expanded pattern"
  phosg::be_uint64_t monochrome_pattern;
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// PICT

struct PictHeader {
  phosg::be_uint16_t size; // unused
  Rect bounds;
} __attribute__((packed));

struct PictSubheaderV2 {
  /* 00 */ phosg::be_int32_t version; // == -1
  /* 04 */ Fixed bounds_x1;
  /* 08 */ Fixed bounds_y1;
  /* 0C */ Fixed bounds_x2;
  /* 10 */ Fixed bounds_y2;
  /* 14 */ phosg::be_uint32_t reserved2;
  /* 18 */
} __attribute__((packed));

struct PictSubheaderV2Extended {
  /* 00 */ phosg::be_int16_t version; // == -2
  /* 02 */ phosg::be_uint16_t reserved1;
  /* 04 */ Fixed horizontal_resolution_dpi;
  /* 08 */ Fixed vertical_resolution_dpi;
  /* 0C */ Rect source_rect;
  /* 14 */ phosg::be_uint32_t reserved2;
  /* 18 */
} __attribute__((packed));

union PictSubheader {
  PictSubheaderV2 v2;
  PictSubheaderV2Extended v2e;
} __attribute__((packed));

struct PictCopyBitsMonochromeArgs {
  BitMapHeader header;
  Rect source_rect;
  Rect dest_rect;
  phosg::be_uint16_t mode;
};

/* There's no struct PictPackedCopyBitsIndexedColorArgs because the color table
 * is a variable size and comes early in the format. If there were such a struct
 * it would look like this:
 * struct PictPackedCopyBitsIndexedColorArgs {
 *   PixelMapHeader header;
 *   ColorTable ctable; // variable size
 *   Rect source_rect;
 *   Rect dest_rect;
 *   uint16_t mode;
 * };
 */

struct PictPackedCopyBitsDirectColorArgs {
  phosg::be_uint32_t base_address; // unused
  PixelMapHeader header;
  Rect source_rect;
  Rect dest_rect;
  phosg::be_uint16_t mode;
};

struct PictQuickTimeImageDescription {
  phosg::be_uint32_t size; // includes variable-length fields
  phosg::be_uint32_t codec;
  phosg::be_uint32_t reserved1;
  phosg::be_uint16_t reserved2;
  phosg::be_uint16_t data_ref_index; // also reserved
  phosg::be_uint16_t algorithm_version;
  phosg::be_uint16_t revision_level; // version of compression software, essentially
  phosg::be_uint32_t vendor;
  phosg::be_uint32_t temporal_quality;
  phosg::be_uint32_t spatial_quality;
  phosg::be_uint16_t width;
  phosg::be_uint16_t height;
  Fixed h_res;
  Fixed v_res;
  phosg::be_uint32_t data_size;
  phosg::be_uint16_t frame_count;
  char name[32];
  phosg::be_uint16_t bit_depth;
  phosg::be_uint16_t clut_id;
} __attribute__((packed));

struct PictCompressedQuickTimeArgs {
  phosg::be_uint32_t size;
  phosg::be_uint16_t version;
  phosg::be_uint32_t matrix[9];
  phosg::be_uint32_t matte_size;
  Rect matte_rect;
  phosg::be_uint16_t mode;
  Rect src_rect;
  phosg::be_uint32_t accuracy;
  phosg::be_uint32_t mask_region_size;
  // variable-length fields:
  // - matte_image_description (determined by matte_size)
  // - matte_data (determined by matte_size)
  // - mask_region (determined by mask_region_size)
  // - image_description (always included; size is self-determined)
  // - data (specified in image_description's data_size field)
} __attribute__((packed));

struct PictUncompressedQuickTimeArgs {
  phosg::be_uint32_t size;
  phosg::be_uint16_t version;
  phosg::be_uint32_t matrix[9];
  phosg::be_uint32_t matte_size;
  Rect matte_rect;
  // variable-length fields:
  // - matte_image_description (determined by matte_size)
  // - matte_data (determined by matte_size)
  // - subopcode describing the image and mask (98, 99, 9A, or 9B)
  // - image data
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// snd

struct SoundResourceHeaderFormat2 {
  phosg::be_uint16_t format_code; // = 2
  phosg::be_uint16_t reference_count;
  phosg::be_uint16_t num_commands;
} __attribute__((packed));

struct SoundResourceHeaderFormat1 {
  phosg::be_uint16_t format_code; // = 1
  phosg::be_uint16_t data_format_count; // we only support 0 or 1 here
} __attribute__((packed));

// 3 is not a standard header format; it's used by Beatnik for MPEG-encoded
// samples. This format is only parsed when the ResourceFile's index format is
// HIRF.
struct SoundResourceHeaderFormat3 {
  phosg::be_uint16_t format_code;
  phosg::be_uint32_t type; // 'none', 'ima4', 'imaW', 'mac3', 'mac6', 'ulaw', 'alaw', or 'mpga'-'mpgn'
  phosg::be_uint32_t sample_rate; // actually a Fixed16
  phosg::be_uint32_t decoded_bytes;
  phosg::be_uint32_t frame_count; // If MPEG, the number of blocks
  phosg::be_uint32_t encoded_bytes;
  phosg::be_uint32_t unused;
  phosg::be_uint32_t start_frame; // If MPEG, the number of uint16_ts to skip
  phosg::be_uint32_t channel_loop_start_frame[6];
  phosg::be_uint32_t channel_loop_end_frame[6];
  phosg::be_uint32_t name_resource_type;
  phosg::be_uint32_t name_resource_id;
  uint8_t base_note;
  uint8_t channel_count; // up to 6
  uint8_t bits_per_sample; // 8 or 16
  uint8_t is_embedded;
  uint8_t is_encrypted;
  uint8_t is_little_endian;
  uint8_t reserved1[2];
  phosg::be_uint32_t reserved2[8];
} __attribute__((packed));

struct SoundResourceHeaderMohawkChunkHeader {
  phosg::be_uint32_t type;
  phosg::be_uint32_t size; // not including this header
} __attribute__((packed));

struct SoundResourceHeaderMohawkFormat {
  // Used when header.type = 'Data' or 'Cue#'
  phosg::be_uint16_t sample_rate;
  phosg::be_uint32_t num_samples; // could be sample bytes, could also be uint16_t
  uint8_t sample_bits;
  uint8_t num_channels;
  phosg::be_uint32_t unknown[3];
  // Sample data immediately follows
} __attribute__((packed));

struct SoundResourceDataFormatHeader {
  phosg::be_uint16_t data_format_id; // we only support 5 here (sampled sound)
  phosg::be_uint32_t flags; // 0x40 = stereo
} __attribute__((packed));

struct SoundResourceCommand {
  phosg::be_uint16_t command;
  phosg::be_uint16_t param1;
  phosg::be_uint32_t param2;
} __attribute__((packed));

struct SoundResourceSampleBuffer {
  phosg::be_uint32_t data_offset; // From end of this struct
  phosg::be_uint32_t data_bytes;
  phosg::be_uint32_t sample_rate; // Probably actually a Fixed
  phosg::be_uint32_t loop_start;
  phosg::be_uint32_t loop_end;
  uint8_t encoding;
  uint8_t base_note;
  uint8_t data[0];
} __attribute__((packed));

struct SoundResourceCompressedBuffer {
  phosg::be_uint32_t num_frames;
  uint8_t sample_rate[10]; // TODO: This could be a long double
  phosg::be_uint32_t marker_chunk;
  phosg::be_uint32_t format;
  phosg::be_uint32_t reserved1;
  phosg::be_uint32_t state_vars; // High word appears to be sample size
  phosg::be_uint32_t left_over_block_ptr;
  phosg::be_uint16_t compression_id;
  phosg::be_uint16_t packet_size;
  phosg::be_uint16_t synth_id;
  phosg::be_uint16_t bits_per_sample;
  uint8_t data[0];
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// INST

struct InstrumentResourceHeader {
  enum Flags1 {
    ENABLE_INTERPOLATE = 0x80,
    ENABLE_AMP_SCALE = 0x40,
    DISABLE_SOUND_LOOPS = 0x20,
    USE_SAMPLE_RATE = 0x08,
    SAMPLE_AND_HOLD = 0x04,
    EXTENDED_FORMAT = 0x02,
    DISABLE_REVERB = 0x01,
  };
  enum Flags2 {
    NEVER_INTERPOLATE = 0x80,
    PLAY_AT_SAMPLED_FREQ = 0x40,
    FIT_KEY_SPLITS = 0x20,
    ENABLE_SOUND_MODIFIER = 0x10,
    USE_SOUND_MODIFIER_AS_BASE_NOTE = 0x08,
    NOT_POLYPHONIC = 0x04,
    ENABLE_PITCH_RANDOMNESS = 0x02,
    PLAY_FROM_SPLIT = 0x01,
  };

  phosg::be_int16_t snd_id; // or csnd or esnd
  phosg::be_uint16_t base_note; // if zero, use the snd's base_note
  uint8_t panning;
  uint8_t flags1;
  uint8_t flags2;
  int8_t smod_id;
  phosg::be_int16_t smod_params[2];
  phosg::be_uint16_t num_key_regions;
} __attribute__((packed));

struct InstrumentResourceKeyRegion {
  // low/high are inclusive
  uint8_t key_low;
  uint8_t key_high;

  phosg::be_int16_t snd_id;
  phosg::be_int16_t smod_params[2];
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// SONG

struct SMSSongResourceHeader {
  struct InstrumentOverride {
    phosg::be_uint16_t midi_channel_id;
    phosg::be_uint16_t inst_resource_id;
  } __attribute__((packed));

  enum Flags1 {
    TERMINATE_DECAY_NOTES_EARLY = 0x40,
    NOTE_INTERPOLATE_ENTIRE_SONG = 0x20,
    NOTE_INTERPOLATE_LEAD_INSTRUMENT = 0x10,
    DEFAULT_PROGRAMS_PER_TRACK = 0x08, // If true, track 1 is inst 1, etc.; otherwise channel 1 is inst 1, etc. (currently unimplemented here)
    ENABLE_MIDI_PROGRAM_CHANGE = 0x04, // Ignored; we always allow program change
    DISABLE_CLICK_REMOVAL = 0x02,
    USE_LEAD_INSTRUMENT_FOR_ALL_VOICES = 0x01,
  };
  enum Flags2 {
    INTERPOLATE_11KHZ_BUFFER = 0x20,
    ENABLE_PITCH_RANDOMNESS = 0x10,
    AMPLITUDE_SCALE_LEAD_INSTRUMENT = 0x08,
    AMPLITUDE_SCALE_ALL_INSTRUMENTS = 0x04,
    ENABLE_AMPLITUDE_SCALING = 0x02,
  };

  phosg::be_int16_t midi_id;
  // RMF docs call this field unused (and indeed, resource_dasm doesn't use it)
  uint8_t lead_inst_id;
  // Reverb types from RMF documentation (these are the names they used):
  // 0 = default/current (don't override from environment)
  // 1 = no reverb
  // 2 = closet
  // 3 = garage
  // 4 = lab
  // 5 = cavern
  // 6 = dungeon
  // 7 = small reflections
  // 8 = early reflections
  // 9 = basement
  // 10 = banquet hall
  // 11 = catacombs
  uint8_t reverb_type;
  phosg::be_uint16_t tempo_bias; // 0 = default = 16667; linear, so 8333 = half-speed
  // Note: Some older TMPLs show the following two fields as a single be_int16_t
  // semitone_shift field; it looks like the filter_type field was added later
  // in development. I haven't yet seen any SONGs that have nonzero filter_type.
  // Similarly, RMF docs combine these two bytes into one field (as it was in
  // earlier SoundMusicSys versions). When exactly did RMF branch from SMS?
  uint8_t filter_type; // 0 = sms, 1 = rmf, 2 = mod (we only support 0 here)
  int8_t semitone_shift;
  // Similarly, RMF docs combine these two bytes into a single field ("Maximum
  // number of simultaneous digital audio files and digital audio streams"). We
  // ignore this difference because resource_dasm doesn't use these fields.
  uint8_t max_effects; // TMPL: "Extra channels for sound effects"
  uint8_t max_notes;
  phosg::be_uint16_t mix_level;
  uint8_t flags1;
  uint8_t note_decay; // In 1/60ths apparently
  uint8_t percussion_instrument; // Channel 10; 0 = none, 0xFF = GM percussion
  uint8_t flags2;

  phosg::be_uint16_t instrument_override_count;

  // Variable-length fields follow:
  // InstrumentOverride instrument_overrides[instrument_override_count];
  // pstring copyright;
  // pstring author;
} __attribute__((packed));

struct RMFSongResourceHeader {
  // Many of these fields are the same as those in SMSSongResourceHeader; see
  // that structure for comments.
  phosg::be_int16_t midi_id;
  uint8_t reserved1;
  uint8_t reverb_type;
  phosg::be_uint16_t tempo_bias;
  uint8_t midi_format; // (RMF) 0 = private, 1 = RMF structure, 2 = RMF linear
  uint8_t encrypted;
  phosg::be_int16_t semitone_shift;
  phosg::be_uint16_t max_concurrent_streams;
  phosg::be_uint16_t max_voices;
  phosg::be_uint16_t max_signals;
  phosg::be_uint16_t volume_bias; // 0 = normal = 007F; linear, so 00FE = double volume
  uint8_t is_in_instrument_bank;
  uint8_t reserved2;
  phosg::be_uint32_t reserved3[7];
  phosg::be_uint16_t num_subresources;
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// Tune

struct TuneResourceHeader {
  phosg::be_uint32_t header_size; // Includes the sample description commands in the MIDI stream
  phosg::be_uint32_t magic; // 'musi'
  phosg::be_uint32_t reserved1;
  phosg::be_uint16_t reserved2;
  phosg::be_uint16_t index;
  phosg::be_uint32_t flags;
  // MIDI track data immediately follows
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// styl

struct StyleResourceCommand {
  phosg::be_uint32_t offset;
  // These two fields seem to scale with size; they might be line/char spacing
  phosg::be_uint16_t unknown1;
  phosg::be_uint16_t unknown2;
  phosg::be_uint16_t font_id;
  phosg::be_uint16_t style_flags;
  phosg::be_uint16_t size;
  Color color;
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// FONT, NFNT

struct FontResourceHeader {
  enum TypeFlags {
    CONTAINS_IMAGE_HEIGHT_TABLE = 0x0001,
    CONTAINS_GLYPH_WIDTH_TABLE = 0x0002,
    BIT_DEPTH_MASK = 0x000C,
    MONOCHROME = 0x0000,
    BIT_DEPTH_2 = 0x0004,
    BIT_DEPTH_4 = 0x0008,
    BIT_DEPTH_8 = 0x000C,
    HAS_COLOR_TABLE = 0x0080,
    IS_DYNAMIC = 0x0010,
    HAS_NON_BLACK_COLORS = 0x0020,
    FIXED_WIDTH = 0x2000,
    CANNOT_EXPAND = 0x4000,
  };
  phosg::be_uint16_t type_flags;
  phosg::be_uint16_t first_char;
  phosg::be_uint16_t last_char;
  phosg::be_uint16_t max_width;
  phosg::be_int16_t max_kerning;
  phosg::be_int16_t descent; // if positive, this is the high word of the width offset table offset
  phosg::be_uint16_t rect_width;
  phosg::be_uint16_t rect_height; // also bitmap height
  phosg::be_uint16_t width_offset_table_offset;
  phosg::be_int16_t max_ascent;
  phosg::be_int16_t max_descent;
  phosg::be_int16_t leading;
  phosg::be_uint16_t bitmap_row_width;
  // Variable-length fields follow:
  // - bitmap image table (each aligned to 16-bit boundary)
  // - bitmap location table
  // - width offset table
  // - glyph-width table
  // - image height table
} __attribute__((packed));

} // namespace ResourceDASM
