#include "SystemTemplates.hh"

#include <stdint.h>
#include <sys/types.h>

#include <stdexcept>
#include <vector>
#include <string>

#include "ResourceFile.hh"

using namespace std;



using Entry = ResourceFile::TemplateEntry;
using EntryList = ResourceFile::TemplateEntryList;
using Type = Entry::Type;
using Format = Entry::Format;

static const map<int64_t, string> AUTO_POSITION_NAMES = {
  { 0x0000, "no auto-center" },
  { 0x280A, "center on main screen" },
  { 0x300A, "use alert position on main screen" },
  { 0x380A, "stagger on main screen" },
  { 0xA80A, "center on parent window" },
  { 0xB00A, "use alert position on parent window" },
  { 0xB80A, "stagger on parent window" },
  { 0x680A, "center on parent window's screen" },
  { 0x700A, "use alert position on parent window's screen"},
  { 0x780A, "stagger on parent window's screen" }
};


static shared_ptr<Entry> t_bool(const char* name) {
  return shared_ptr<Entry>(new Entry(name, Type::BOOL, Format::FLAG, 2));
}

static shared_ptr<Entry> t_byte(const char* name, bool is_signed = true, map<int64_t, string> case_names = {}) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::DECIMAL, 1, 0, 0, is_signed, std::move(case_names)));
}

static shared_ptr<Entry> t_byte_hex(const char* name, bool is_signed = false, map<int64_t, string> case_names = {}) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::HEX, 1, 0, 0, is_signed, std::move(case_names)));
}

static shared_ptr<Entry> t_char(const char* name) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::TEXT, 1));
}

static shared_ptr<Entry> t_word(const char* name, bool is_signed = true, map<int64_t, string> case_names = {}) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::DECIMAL, 2, 0, 0, is_signed, std::move(case_names)));
}

static shared_ptr<Entry> t_word_hex(const char* name, bool is_signed = true, map<int64_t, string> case_names = {}) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::HEX, 2, 0, 0, is_signed, std::move(case_names)));
}

static shared_ptr<Entry> t_long(const char* name, bool is_signed = true, map<int64_t, string> case_names = {}) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::DECIMAL, 4, 0, 0, is_signed, std::move(case_names)));
}

static shared_ptr<Entry> t_long_hex(const char* name, bool is_signed = false, map<int64_t, string> case_names = {}) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::HEX, 4, 0, 0, is_signed, std::move(case_names)));
}

static shared_ptr<Entry> t_date(const char* name) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::DATE, 4, 0, 0, false));
}

static shared_ptr<Entry> t_ostype(const char* name) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::TEXT, 4, 0, 0, false));
}

static shared_ptr<Entry> t_zero(const char* name, uint8_t width) {
  return shared_ptr<Entry>(new Entry(name, Type::ZERO_FILL, Format::HEX, width, 0, 0, false));
}

static shared_ptr<Entry> t_align(uint8_t end_alignment) {
  return shared_ptr<Entry>(new Entry("", Type::ALIGNMENT, Format::FLAG, 0, end_alignment, 0));
}

static shared_ptr<Entry> t_string(const char* name, uint8_t width, bool word_align = false, bool odd_offset = false) {
  return shared_ptr<Entry>(new Entry(name, Type::STRING, Format::TEXT, width, word_align ? 2 : 0, odd_offset ? 1 : 0));
}

static shared_ptr<Entry> t_pstring(const char* name, bool word_align = false, bool odd_offset = false) {
  return shared_ptr<Entry>(new Entry(name, Type::PSTRING, Format::TEXT, 1, word_align ? 2 : 0, odd_offset ? 1 : 0));
}

static shared_ptr<Entry> t_pstring_2(const char* name, bool word_align = false, bool odd_offset = false) {
  return shared_ptr<Entry>(new Entry(name, Type::PSTRING, Format::TEXT, 2, word_align ? 2 : 0, odd_offset ? 1 : 0));
}

static shared_ptr<Entry> t_pstring_fixed(const char* name, uint8_t width, bool word_align = false, bool odd_offset = false) {
  return shared_ptr<Entry>(new Entry(name, Type::FIXED_PSTRING, Format::TEXT, width, word_align ? 2 : 0, odd_offset ? 1 : 0));
}

static shared_ptr<Entry> t_rect(const char* name) {
  return shared_ptr<Entry>(new Entry(name, Type::RECT, Format::DECIMAL, 2));
}

static shared_ptr<Entry> t_bitfield(EntryList&& entries) {
  return shared_ptr<Entry>(new Entry("", Type::BITFIELD, move(entries)));
}

static shared_ptr<Entry> t_data_hex(const char* name, size_t size) {
  return shared_ptr<Entry>(new Entry(name, Type::STRING, Format::HEX, size, 0, 0, false));
}

static shared_ptr<Entry> t_data_eof_hex(const char* name) {
  return shared_ptr<Entry>(new Entry(name, Type::EOF_STRING, Format::HEX, 0, 0, 0, false));
}

static shared_ptr<Entry> t_list_eof(const char* name, EntryList&& entries) {
  return shared_ptr<Entry>(new Entry(name, Type::LIST_EOF, move(entries)));
}

static shared_ptr<Entry> t_list_zero_byte(const char* name, EntryList&& entries) {
  return shared_ptr<Entry>(new Entry(name, Type::LIST_ZERO_BYTE, move(entries)));
}

static shared_ptr<Entry> t_list_zero_count(const char* name, EntryList&& entries) {
  // list count is a word
  return shared_ptr<Entry>(new Entry(name, Type::LIST_ZERO_COUNT, move(entries)));
}

static shared_ptr<Entry> t_list_one_count(const char* name, EntryList&& entries) {
  // list count is a word
  return shared_ptr<Entry>(new Entry(name, Type::LIST_ONE_COUNT, move(entries)));
}

static shared_ptr<Entry> t_opt_eof(EntryList&& entries) {
  return shared_ptr<Entry>(new Entry("", Type::OPT_EOF, move(entries)));
}

static shared_ptr<Entry> t_dvdr(const char* comment) {
  return shared_ptr<Entry>(new Entry(comment, Type::VOID, Format::DECIMAL, 0, 0, 0));
}

static const unordered_map<uint32_t, ResourceFile::TemplateEntryList> system_templates({
  {RESOURCE_TYPE_acur, {
    t_word("Number of frames (cursors)", false),
    t_word("Used frame counter", false),
    t_list_eof("Frames", {
      t_word("CURS resource ID"),
      t_zero("", 2),
    }),
  }},
  {RESOURCE_TYPE_ALIS, { // Beatnik ALIS; not the same as Mac OS alis
    t_long("Version", false),
    // TODO: The list count appears to actually be a long here; support this
    // natively instead of assuming the upper 2 bytes are zeroes
    t_zero("", 2),
    t_list_one_count("Aliases", {
      t_long("Alias from", false),
      t_long("Alias to", false),
    }),
  }},
  {RESOURCE_TYPE_ALRT, {
    t_rect("Bounds"),
    t_word("Items ID"),
    t_bitfield({
      t_bool("(4) bold #"),
      t_bool("(4) drawn"),
      t_bool("(4) snd high"),
      t_bool("(4) snd low"),
      t_bool("(3) bold #"),
      t_bool("(3) drawn"),
      t_bool("(3) snd high"),
      t_bool("(3) snd low"),
    }),
    t_bitfield({
      t_bool("(2) bold #"),
      t_bool("(2) drawn"),
      t_bool("(2) snd high"),
      t_bool("(2) snd low"),
      t_bool("(1) bold #"),
      t_bool("(1) drawn"),
      t_bool("(1) snd high"),
      t_bool("(1) snd low"),
    }),
    t_opt_eof({
      // Can exist in System 7.0 and later
      t_align(2),
      t_word_hex("Auto position", false, AUTO_POSITION_NAMES),
    })
  }},
  {RESOURCE_TYPE_APPL, {
    t_list_eof("Entries", {
      t_ostype("Creator"),
      t_long("Directory"),
      t_pstring("Application", true),
    }),
  }},
  {RESOURCE_TYPE_audt, {
    t_list_eof("Entries", {
      t_ostype("Macintosh model"),
      t_long("Installation status", false, {
        { 0, "not installed" },
        { 1, "minimal installation" },
        { 2, "full installation" },
      }),
    }),
  }},
  {RESOURCE_TYPE_BNDL, {
    t_ostype("Owner name"),
    t_word("Owner ID"),
    t_list_zero_count("Types", {
      t_ostype("Type"),
      t_list_zero_count("IDs", {
        t_word("Local ID"),
        t_word("Resource ID"),
      }),
    }),
  }},
  {RESOURCE_TYPE_CMDK, {
    t_pstring("Command keys"),
  }},
  {RESOURCE_TYPE_cmnu, {
    t_word("Menu ID"),
    t_zero("Width", 2),
    t_zero("Height", 2),
    t_word("ProcID"),
    t_zero("", 2),
    t_long_hex("Enabled flags"),
    t_pstring("Title"),
    t_list_zero_byte("Items", {
      t_pstring("Name"),
      t_byte("Icon number"),
      t_char("Key equivalent"),
      t_char("Mark character"),
      t_byte_hex("Style"),
      t_align(2),
      t_word("Command number"), // Note: this is t_long in CMNU
    }),
  }},
  {RESOURCE_TYPE_CMNU, {
    t_word("Menu ID"),
    t_zero("Width", 2),
    t_zero("Height", 2),
    t_word("ProcID"),
    t_zero("", 2),
    t_long_hex("Enabled flags"),
    t_pstring("Title"),
    t_list_zero_byte("Items", {
      t_pstring("Name"),
      t_byte("Icon number"),
      t_char("Key equivalent"),
      t_char("Mark character"),
      t_byte_hex("Style"),
      t_align(2),
      t_long("Command number"), // Note: this is t_word in cmnu
    }),
  }},
  {RESOURCE_TYPE_CNTL, {
    t_rect("Bounds"),
    t_word("Value"),
    t_bool("Visible"),
    t_word("Max"),
    t_word("Min"),
    t_word("ProcID"),
    t_long("RefCon"),
    t_pstring("Title"),
  }},
  {RESOURCE_TYPE_CTYN, {
    t_list_zero_count("Cities", {
      t_word("Num chars", false),
      t_long_hex("Latitude"),
      t_long_hex("Longitude"),
      t_long("GMT difference"),
      t_long("abc"), // TODO: What is this? Name it appropriately.
      t_pstring("Name"),
      t_align(2),
    }),
  }},
  {RESOURCE_TYPE_dbex, {
    t_dvdr("If and only when this resource exists in the System file, holding the"),
    t_dvdr("shift key during boot turns off extensions. That way the user can't "),
    t_dvdr("prevent e.g. security INITs from loading"),
    t_word("Dummy"),
  }},
  // TODO: Info is non-text for several item types; we should make a real
  // renderer or something
  {RESOURCE_TYPE_DITL, {
    t_list_zero_count("Items", {
      t_zero("", 4),
      t_rect("Bounds"),
      t_byte("Type"),
      t_pstring("Info", true, true),
    }),
  }},
  {RESOURCE_TYPE_DLOG, {
    t_rect("Bounds"),
    t_word("ProcID"),
    t_bool("Visible"),
    t_bool("GoAway"),
    t_long("RefCon"),
    t_word("ItemsID"),
    t_pstring("Title", false),
    t_opt_eof({
      // Can exist in System 7.0 and later
      t_align(2),
      t_word_hex("Auto position", false, AUTO_POSITION_NAMES),
    })
  }},
  {RESOURCE_TYPE_errs, {
    t_list_eof("Entries", {
      t_word("Minimum ID"),
      t_word("Maximum ID"),
      t_word("String ID"),
    }),
  }},
  {RESOURCE_TYPE_FBTN, {
    t_list_one_count("Buttons", {
      // TODO: The presence of an icon here merits an actual decoder (e.g.
      // decode_FBTN); unfortunately, I don't have any example resources to test
      // such a decoder on, so I haven't written it yet.
      t_data_hex("Icon", 128),
      t_ostype("Type"),
      t_pstring("Application", true),
      t_pstring("Document", true),
    }),
  }},
  {RESOURCE_TYPE_FDIR, {
    t_list_eof("", {
      t_long_hex("Button DirID"),
    }),
  }},
  {RESOURCE_TYPE_fldN, {
    t_list_eof("Folders", {
      t_ostype("Folder type"),
      t_zero("Version", 2),
      // TODO: This kind of implies that the pstring should be a wstring
      // instead, but the TMPL explicitly has a t_zero before it. Fix this?
      t_zero("Length (high byte)", 1),
      t_pstring("Folder name", true, true),
    }),
  }},
  {RESOURCE_TYPE_flst, {
    t_list_one_count("Fonts", {
      t_pstring("Font name", true),
      // Always plain(?)
      t_word_hex("Font style"),
      t_word_hex("Font size"),
      // Quickdraw transfer mode
      t_word_hex("Font mode")
    }),
  }},
  {RESOURCE_TYPE_fmap, {
    t_list_eof("File mappings", {
      t_ostype("File type"),
      t_word("Standard File icon ID"),
      t_word("Finder icon ID"),
    }),
  }},
  {RESOURCE_TYPE_FREF, {
    t_ostype("File type"),
    t_word("LocalID"),
    t_pstring("File name"),
  }},
  {RESOURCE_TYPE_FRSV, {
    t_list_one_count("Font IDs", {
      t_word("Font ID"),
    }),
  }},
  {RESOURCE_TYPE_FWID, {
    t_word_hex("Font type"),
    t_word("First char"),
    t_word("Last char"),
    t_word("Maximum width"),
    t_word("Maximum kern"),
    t_word("Negated descent"),
    t_word("Rect width"),
    t_word("Char height"),
    t_word("Offset/width table location"),
    t_word("Ascent"),
    t_word("Descent"),
    t_word("Leading"),
    t_list_eof("Chars", {
      t_byte("Offset"),
      t_byte("Width"),
    }),
  }},
  {RESOURCE_TYPE_gbly, {
    t_word("Version"),
    t_date("Timestamp"),
    t_list_one_count("Box flags", {
      t_word_hex("Box flag of supported CPU"),
    }),
  }},
  {RESOURCE_TYPE_GNRL, {
    t_word("ShowSysWarn"),
    t_word("OpenAtStart"),
    t_word("PickWidth"),
    t_word("PickHeight"),
    t_word("TypesWidth"),
    t_word("TypesHeight"),
    t_byte("UseIconView"),
    t_byte("ShowSize"),
    t_word("PrefsVersion"),
    t_word("WindWarnLim"),
    t_word("VerifyOnOpen"),
    t_byte("AutoSize"),
    t_byte("StackAllWind"),
    t_byte("NoStakCanCol"),
    t_byte("NoShowSplash"),
    t_byte("NoZoomRects"),
    t_byte("(unused)"),
    t_byte("(unused)"),
    t_byte("(unused)"),
    t_word("(unused)"),
    t_word("(unused)"),
    t_word("(unused)"),
    t_word("(unused)"),
  }},
  {RESOURCE_TYPE_hwin, {
    t_word("Help version"),
    t_long_hex("Options"),
    t_list_one_count("Items", {
      t_word("Resource ID"),
      t_ostype("Resource type"),
      t_word("String length", false),
      t_pstring("Window title"),
      t_align(2),
    }),
  }},
  {RESOURCE_TYPE_icmt, {
    t_long_hex("Version release date"),
    t_long_hex("Version"),
    t_word("Icon ID"),
    t_pstring("Comment"),
  }},
  {RESOURCE_TYPE_inbb, {
    t_word_hex("Format version"),
    t_zero("Flags (high)", 1),
    t_bitfield({
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("Change on install"),
      t_bool("Change on remove"),
    }),
    t_word_hex("Value key"),
    t_data_eof_hex("Value"),
  }},
  {RESOURCE_TYPE_indm, {
    t_word_hex("Format version"),
    t_zero("Flags", 2),
    t_list_one_count("Machines", {
      t_word("Machine type"),
    }),
    t_list_one_count("Processors", {
      t_word("Processor type"),
    }),
    t_list_one_count("MMUs", {
      t_word("MMU type"),
    }),
    t_list_one_count("Keyboards", {
      t_word("Keyboard type"),
    }),
    t_byte("Requires FPU"),
    t_byte("Requires Color QuickDraw"),
    t_word("Minimal memory (MB)", false),
    t_list_one_count("System resources", {
      t_ostype("Type"),
      t_word("ID"),
    }),
    t_long_hex("System revision"),
    t_word("Country code"),
    t_word("AppleTalk driver version"),
    t_long("Minimum target size (KB)", false),
    t_long("Maximum target size (KB)", false),
    t_word("User function ID"),
    t_pstring("User description", true),
    t_list_one_count("Packages", {
      t_word("Package ID"),
    }),
  }},
  // Note: There is a TMPL for 'infa' in ResEdit, but it appears to be incorrect
  // or outdated because it doesn't match any example resources I could find.
  {RESOURCE_TYPE_infs, {
    t_ostype("File type"),
    t_ostype("File creator"),
    t_date("Creation date"),
    t_bitfield({
      t_bool("Search for file"),
      t_bool("Type and creator must match"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
    }),
    t_zero("Flags (low)", 1),
    t_pstring("File name"),
  }},
  {RESOURCE_TYPE_inpk, {
    t_word_hex("Format version"),
    t_bitfield({
      t_bool("Shows on custom"),
      t_bool("Removable"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
    }),
    t_zero("Flags (low)", 1),
    t_word("icmt ID"),
    t_long("Package size", false),
    t_pstring("Package name", true),
    t_list_one_count("Parts", {
      t_ostype("Type"),
      t_word("ID"),
    }),
  }},
  {RESOURCE_TYPE_inra, {
    t_word_hex("Format version"),
    t_bitfield({
      t_bool("Delete on remove"),
      t_bool("Delete on install"),
      t_bool("Copy"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
    }),
    t_bitfield({
      t_bool("(unused)"),
      t_bool("Target required"),
      t_bool("Keep existing"),
      t_bool("Update only"),
      t_bool("Even if protected"),
      t_bool("Need not exist"),
      t_bool("Find by ID"),
      t_bool("Name must match"),
    }),
    t_word("Target file spec"),
    t_word("Source file spec"),
    t_ostype("Resource type"),
    t_word("Source ID"),
    t_word("Target ID"),
    t_word("Resource size", false),
    t_pstring("Atom description", true),
    t_pstring("Resource name"),
  }},
  {RESOURCE_TYPE_insc, {
    t_word_hex("Format version"),
    t_word_hex("Flags"),
    t_pstring("Script name", true),
    t_pstring_2("Help string"),
    t_align(2),
    t_list_one_count("Files", {
      t_word_hex("File spec"),
      t_ostype("Type"),
      t_ostype("Creator"),
      t_date("Creation date"),
      t_zero("Handle", 4),
      t_zero("Del size", 4),
      t_zero("Add size", 4),
      t_pstring("File name", true),
    }),
    t_list_one_count("Resource files", {
      t_word_hex("File spec"),
      t_ostype("Type"),
      t_ostype("Creator"),
      t_date("Creation date"),
      t_zero("Handle", 4),
      t_zero("Del size", 4),
      t_zero("Add size", 4),
      t_pstring("To file name", true),
      t_list_one_count("From files", {
        t_word_hex("File spec"),
        t_ostype("Type"),
        t_ostype("Creator"),
        t_date("Creation date"),
        t_zero("Handle", 4),
        t_zero("Del size", 4),
        t_zero("Add size", 4),
        t_pstring("From file name", true),
        t_list_one_count("Resources", {
          t_word_hex("Resource spec"),
          t_ostype("Type"),
          t_word("Source ID"),
          t_word("Target ID"),
          t_word_hex("CRC/version"),
          t_zero("", 6),
          t_zero("Del size", 4),
          t_zero("Add size", 4),
          t_pstring("Resource name", true),
          t_word_hex("Previous CRCs"),
        }),
      }),
    }),
    t_data_eof_hex("Data"),
  }},
  {RESOURCE_TYPE_itl0, {
    t_char("Decimal point separator"),
    t_char("Thousands separator"),
    t_char("List separator"),
    t_string("Currency symbol", 3),
    t_bitfield({
      t_bool("Leading unit zero"),
      t_bool("Trailing unit zero"),
      // 0 = parenthesis, 1 = minus sign
      t_bool("Negative representation"),
      t_bool("Currency symbol leads number"),
      // 4 unused bits
    }),
    t_byte("Short date format order", false, {
      { 0, "month/day/year" },
      { 1, "day/month/year" },
      { 2, "year/month/day" },
      { 3, "month/year/day" },
      { 4, "day/year/month" },
      { 5, "year/day/month" },
    }),
    t_bitfield({
      t_bool("Short date has century"),
      t_bool("Short date's month has leading 0"),
      t_bool("Short date's day has leading 0"),
      // 5 unused bits
    }),
    t_char("Short date separator"),
    t_byte("Time cycle short date", false, {
      { 0, "24h" },
      { 1, "24h zero cycle" },
      { 255, "12h" },
    }),
    t_bitfield({
      t_bool("Leading 0 in hours"),
      t_bool("Leading 0 in minutes"),
      t_bool("Leading 0 in seconds"),
      // 5 unused bits
    }),
    t_string("Morning string", 4),
    t_string("Evening string", 4),
    t_char("Time separator"),
    t_string("AM suffix if 24h cycle", 4),
    t_string("PM suffix if 24h cycle", 4),
    t_byte("Measurement system", false, {
      { 0, "inches" },
      { 255, "metric" },
    }),
    t_byte("Region"),
    t_byte("Version"),
  }},  
  {RESOURCE_TYPE_ITL1, {
    t_word("Use short dates before system"),
  }},
  {RESOURCE_TYPE_itlb, {
    t_word("itl0 ID"),
    t_word("itl1 ID"),
    t_word("itl2 ID"),
    t_word_hex("Flags"),
    t_word("itl4 ID"),
    t_zero("Reserved", 2),
    t_word("Script language code"),
    t_byte("Number representation code"),
    t_byte("Date representation code"),
    t_word("KCHR ID"),
    t_word("SICN ID"),
    t_long("Script record size", false),
    t_word("Default monochrome FOND ID"),
    t_word("Default monochrome font size", false),
    t_word("Preferred FOND ID"),
    t_word("Preferred font size", false),
    t_word("Small FOND ID"),
    t_word("Small font size", false),
    t_word("System FOND ID"),
    t_word("System font size", false),
    t_word("Application FOND ID"),
    t_word("Application font size", false),
    t_word("Help Manager FOND ID"),
    t_word("Help Manager font size", false),
    t_byte_hex("Valid styles"),
    t_byte_hex("Alias styles"),
  }},
  {RESOURCE_TYPE_itlc, {
    t_word("System script code"),
    t_word("Keyboard cache size"),
    t_byte_hex("Font force (00=off, FF=on)"),
    t_byte_hex("Intl force (00=off, FF=on)"),
    t_byte_hex("Old keyboard"),
    t_bitfield({
      t_bool("Always show keyboard icon"),
      t_bool("Use dual caret for mixed-direction text"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
    }),
    t_word("Script icon offset"),
    t_byte("Script icon side (00=right, FF=left)"),
    t_byte_hex("Reserved for icon info"),
    t_word("System region code"),
    t_zero("Reserved", 34),
  }},
  {RESOURCE_TYPE_itlk, {
    t_list_one_count("Entries", {
      t_word("Keyboard type"),
      t_byte_hex("Old mods"),
      t_byte("Old code"),
      t_byte_hex("Mask mods"),
      t_byte("Mask code"),
      t_byte_hex("New mods"),
      t_byte("New code"),
    }),
  }},
  {RESOURCE_TYPE_KBDN, {
    t_pstring("Keyboard name"),
  }},
  {RESOURCE_TYPE_LAYO, {
    t_word("Font ID"),
    t_word("Font size", false),
    t_word("Screen header height", false),
    t_word("Top line break"),
    t_word("Bottom line break"),
    t_word("Printing header height"),
    t_word("Printing footer height"),
    t_rect("Window rect"),
    t_word("Line spacing"),
    t_word("Tab stop 1"),
    t_word("Tab stop 2"),
    t_word("Tab stop 3"),
    t_word("Tab stop 4"),
    t_word("Tab stop 5"),
    t_word("Tab stop 6"),
    t_word("Tab stop 7"),
    t_byte_hex("Column justification"),
    t_byte_hex("Reserved"),
    t_word("Icon horizontal spacing"),
    t_word("Icon vertical spacing"),
    t_word("Icon vertical phase"),
    t_word("Small icon horizontal"),
    t_word("Small icon vertical"),
    t_byte("Default view"),
    t_zero("", 1),
    t_word_hex("Text view date"),
    t_bitfield({
      t_bool("Use zoom rects"),
      t_bool("Skip trash warnings"),
      t_bool("Always grid drags"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
    }),
    t_byte("Icon-text gap"),
    t_word("Sort style"),
    t_long("Watch threshold"),
    t_bitfield({
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("Use physical icon"),
      t_bool("Title click"),
      t_bool("Copy inherit"),
      t_bool("New fold inherit"),
    }),
    t_byte("Color style"),
    t_word("Maximum number of windows"),
  }},
  {RESOURCE_TYPE_lstr, {
    t_pstring_fixed("", 31),
  }},
  {RESOURCE_TYPE_mach, {
    t_long_hex("Capabilities", false, {
      { 0xFFFF0000, "runs on all systems" },
      { 0x0000FFFF, "control panel decides" },
    })
  }},
  {RESOURCE_TYPE_MBAR, {
    t_list_one_count("Menus", {
      t_word("Resource ID"),
    }),
  }},
  {RESOURCE_TYPE_mcky, {
    t_byte("Threshold 1"),
    t_byte("Threshold 2"),
    t_byte("Threshold 3"),
    t_byte("Threshold 4"),
    t_byte("Threshold 5"),
    t_byte("Threshold 6"),
    t_byte("Threshold 7"),
    t_byte("Threshold 8"),
  }},
  {RESOURCE_TYPE_MENU, {
    t_word("Menu ID"),
    t_zero("Width", 2),
    t_zero("Height", 2),
    t_word("ProcID"),
    t_zero("", 2),
    t_long_hex("Enabled flags"),
    t_pstring("Title"),
    t_list_zero_byte("Items", {
      t_pstring("Name"),
      t_byte("Icon number"),
      t_char("Key equivalent"),
      t_char("Mark character"),
      t_byte_hex("Style"),
    }),
  }},
  {RESOURCE_TYPE_mitq, {
    t_long("Queue size for 3 bit inverse table", false),
    t_long("Queue size for 4 bit inverse table", false),
    t_long("Queue size for 5 bit inverse table", false),
  }},
  {RESOURCE_TYPE_nrct, {
    t_list_one_count("Rectangles", {
      t_rect("Rectangle"),
    }),
  }},
  {RESOURCE_TYPE_PAPA, {
    t_pstring("Name"),
    t_pstring("Type"),
    t_pstring("Zone"),
    t_long_hex("Address block"),
    t_data_eof_hex("Data"),
  }},
  {RESOURCE_TYPE_PICK, {
    t_ostype("Type"),
    t_byte("Use color"),
    t_byte("Picker type"),
    t_byte("View by"),
    t_zero("(unused)", 1),
    t_word("Vertical cell size"),
    t_word("Horizontal cell size"),
    t_ostype("LDEF type"),
    t_pstring("Option string"),
  }},
  // Note: There is a TMPL for 'POST' in ResEdit, but it appears to be incorrect
  // or outdated because it doesn't match any example resources I could find.
  {RESOURCE_TYPE_ppcc, {
    t_dvdr("(PPC = program-to-program communication, not PowerPC)"),
    t_byte("NBP lookup interval"),
    t_byte("NBP lookup count"),
    t_word("NBP maximum lives"),
    t_word("NBP maximum entities"),
    t_word("NBP idle time"),
    t_word("PPC maximum ports"),
    t_word("PPC idle time"),
  }},
  {RESOURCE_TYPE_ppci, {
    t_dvdr("(PPC = program-to-program communication, not PowerPC)"),
    t_byte("Min. PPC port"),
    t_byte("Max. PPC port"),
    t_byte("Min. no. of sessions (local use)"),
    t_byte("Max. no. of sessions (local use)"),
    t_byte("Min. no. of sessions (remote use)"),
    t_byte("Max. no. of sessions (remote use)"),
    t_byte("Min. no. of sessions (IPM use)"),
    t_byte("Max. no. of sessions (IPM use)"),
    t_byte("ADSP time-out in 1/6th of a second"),
    t_byte("ADSP retries"),
    t_byte("NBP time-out interval in 8-ticks"),
    t_byte("NBP retries"),
    t_pstring("NBP type of PPC toolbox"),
  }},
  {RESOURCE_TYPE_PRC0, {
    t_word("iPrVersion"),
    t_word_hex("prInfo.iDev"),
    t_word("prInfo.iVRes"),
    t_word("prInfo.iHRes"),
    t_rect("prInfo.rPage"),
    t_rect("rPaper"),
    t_word_hex("prStl.wDev"),
    t_word("prStl.iPageV"),
    t_word("prStl.iPageH"),
    t_byte("prStl.bPort"),
    t_byte("prStl.feed"),
    t_word("prIPT.iDev"),
    t_word("prIPT.iVRes"),
    t_word("prIPT.iHRes"),
    t_rect("prIPT.rPage"),
    t_word("prXI.iRowBytes"),
    t_word("prXI.iBandV"),
    t_word("prXI.iBandH"),
    t_word("prXI.iDevBytes"),
    t_word("prXI.iBands"),
    t_byte("prXI.bPatScale"),
    t_byte("prXI.bUlThick"),
    t_byte("prXI.UlOffset"),
    t_byte("prXI.UlShadow"),
    t_byte("prXI.scan"),
    t_byte("prXI.bXInfoX"),
    t_word("prJob.iFstPage"),
    t_word("prJob.iLstPage"),
    t_word("prJob.iCopies"),
    t_byte("prJob.bJDocLoop"),
    t_byte("prJob.fFromUsr"),
    t_long_hex("prJob.pIdleProc"),
    t_long_hex("prJob.pFileName"),
    t_word("prJob.iFileVol"),
    t_byte("prJob.bFileVers"),
    t_byte("prJob.bJobX"),
    t_data_hex("printX", 38),
  }},
  {RESOURCE_TYPE_PRC3, {
    t_word("Number of buttons"),
    t_word("Button 1 height"),
    t_word("Button 1 width"),
    t_word("Button 2 height"),
    t_word("Button 2 width"),
    t_word("Button 3 height"),
    t_word("Button 3 width"),
    t_word("Button 4 height"),
    t_word("Button 4 width"),
    t_word("Button 5 height"),
    t_word("Button 5 width"),
    t_word("Button 6 height"),
    t_word("Button 6 width"),
    t_pstring("Button 1 name"),
    t_pstring("Button 2 name"),
    t_pstring("Button 3 name"),
    t_pstring("Button 4 name"),
    t_pstring("Button 5 name"),
    t_pstring("Button 6 name"),
    t_data_eof_hex("Data"),
  }},
  {RESOURCE_TYPE_PSAP, {
    t_pstring("String"),
  }},
  {RESOURCE_TYPE_pslt, {
    t_word("Number of Nubus pseudo-slots"),
    t_word("Nubus orientation", false, {
      { 0, "Horizontal form factor, ascending slot order" },
      { 1, "Horizontal form factor, descending slot order" },
      { 2, "Vertical form factor, ascending slot order" },
      { 3, "Vertical form factor, descending slot order" },
    }),
    t_list_eof("Slots", {
      t_word("Nubus slot"),
      t_word("Pseudo slot"),
    }),
  }},
  {RESOURCE_TYPE_ptbl, {
    t_word("Patch table version"),
    t_list_zero_count("Ranges", {
      t_word("Start", false),
      t_word("End (inclusive)", false),
    }),
  }},
  {RESOURCE_TYPE_qrsc, {
    t_word("Version"),
    t_word("qdef ID"),
    t_word("Host etc. STR#"),
    t_word("Current query"),
    t_list_one_count("Queries", {
      t_word("wstr ID"),
    }),
    t_list_one_count("Resources", {
      t_ostype("Type"),
      t_word("ID"),
    }),
  }},
  {RESOURCE_TYPE_RECT, {
    t_rect(""),
  }},
  {RESOURCE_TYPE_resf, {
    t_list_one_count("Families", {
      t_pstring("Family name"),
      t_align(2),
      t_list_one_count("Fonts", {
        t_word("Point size", false),
        t_word_hex("Style flags"),
      }),
    }),
  }},
  {RESOURCE_TYPE_RMAP, {
    t_ostype("Map to type"),
    t_byte("Editor only"),
    t_align(2),
    t_list_one_count("Exceptions", {
      t_word("ID"),
      t_ostype("Map to type"),
      t_byte("Editor only"),
      t_align(2),
    }),
  }},
  {RESOURCE_TYPE_rttN, {
    t_list_one_count("Handlers", {
      t_word("'proc' resource ID"),
      t_list_one_count("DB types of handler", {
        t_ostype("DB type"),
      })
    })
  }},
  {RESOURCE_TYPE_RVEW, {
    t_byte("View by"),
    t_byte("Show attributes"),
  }},
  {RESOURCE_TYPE_scrn, {
    t_list_one_count("Devices", {
      t_word_hex("SRsrc type"),
      t_word_hex("NuBus slot (card slot + 8)"),
      t_long_hex("DCtlDevBase"),
      t_word("Mode sRsrcID"),
      t_word_hex("Flags (0x77FE)"),
    }),
    t_bitfield({
      t_bool("Is active"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("Is main screen"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
    }),
    t_bitfield({
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("(unused)"),
      t_bool("Is color"),
    }),
    t_word("clut resource ID"),
    t_word("Gamma correction resource ID"),
    t_rect("Global rectangle"),
    t_list_one_count("Control calls", {
      t_word("CsCode"),
      t_word("Length"),
      t_long("Data"),
    }),
  }},
  {RESOURCE_TYPE_sect, {
    t_byte("Version"),
    t_byte("Kind"),
    t_byte("Mode"),
    t_date("Modification date"),
    t_long("Section ID"),
    t_long("Reference count"),
    t_long("Alias handle"),
    t_long("Sub part"),
    t_long("Next section"),
    t_long("Control block"),
    t_long("Reference number"),
  }},
  {RESOURCE_TYPE_slut, { // ahem ("sound lookup table"?)
    t_list_eof("Entries", {
      t_ostype("OS type"),
      t_word("Resource ID"),
    }),
  }},
  {RESOURCE_TYPE_SIGN, {
    t_long("Key word"),
    t_word("BNDL ID"),
  }},
  {RESOURCE_TYPE_thnN, {
    t_list_eof("Entries", {
      t_ostype("OS type"),
      t_word("Resource ID"),
    }),
  }},
  {RESOURCE_TYPE_TOOL, {
    t_word("Tools per row"),
    t_word("Number of rows"),
    t_list_eof("Tools", {
      t_word("Cursor ID"),
    }),
  }},
  {RESOURCE_TYPE_WIND, {
    t_rect("Bounds"),
    t_word("ProcID"),
    t_bool("Visible"),
    t_bool("GoAway"),
    t_long("RefCon"),
    t_pstring("Title", false),
    t_opt_eof({
      // Can exist in System 7.0 and later
      t_align(2),
      t_word_hex("Auto position", false),
    })
  }},
  {RESOURCE_TYPE_wstr, {
    t_pstring_2("String"),
  }},
});

static const ResourceFile::TemplateEntryList empty_template({});

const ResourceFile::TemplateEntryList& get_system_template(uint32_t type) {
  try {
    return system_templates.at(type);
  } catch (const out_of_range&) {
    return empty_template;
  }
}
