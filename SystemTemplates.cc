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

static shared_ptr<Entry> t_bool(const char* name) {
  return shared_ptr<Entry>(new Entry(name, Type::BOOL, Format::FLAG, 2));
}

static shared_ptr<Entry> t_byte(const char* name, bool is_signed = true) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::DECIMAL, 1, 0, 0, is_signed));
}

static shared_ptr<Entry> t_byte_hex(const char* name, bool is_signed = false) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::HEX, 1, 0, 0, is_signed));
}

static shared_ptr<Entry> t_char(const char* name) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::TEXT, 1));
}

static shared_ptr<Entry> t_word(const char* name, bool is_signed = true) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::DECIMAL, 2, 0, 0, is_signed));
}

static shared_ptr<Entry> t_long(const char* name, bool is_signed = true) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::DECIMAL, 4, 0, 0, is_signed));
}

static shared_ptr<Entry> t_long_hex(const char* name, bool is_signed = false) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::HEX, 4, 0, 0, is_signed));
}

static shared_ptr<Entry> t_ostype(const char* name) {
  return shared_ptr<Entry>(new Entry(name, Type::INTEGER, Format::TEXT, 4, 0, 0, false));
}

static shared_ptr<Entry> t_zero(uint8_t width) {
  return shared_ptr<Entry>(new Entry("", Type::ZERO_FILL, Format::HEX, width, 0, 0, false));
}

static shared_ptr<Entry> t_pstring(const char* name, bool word_align = false, bool odd_offset = false) {
  return shared_ptr<Entry>(new Entry(name, Type::PSTRING, Format::TEXT, 1, word_align ? 2 : 0, odd_offset ? 1 : 0));
}

static shared_ptr<Entry> t_rect(const char* name) {
  return shared_ptr<Entry>(new Entry(name, Type::RECT, Format::DECIMAL, 2));
}

static shared_ptr<Entry> t_bitfield(EntryList&& entries) {
  return shared_ptr<Entry>(new Entry("", Type::BITFIELD, move(entries)));
}

static shared_ptr<Entry> t_list_eof(const char* name, EntryList&& entries) {
  return shared_ptr<Entry>(new Entry(name, Type::LIST_EOF, move(entries)));
}

static shared_ptr<Entry> t_list_zero_byte(const char* name, EntryList&& entries) {
  return shared_ptr<Entry>(new Entry(name, Type::LIST_ZERO_BYTE, move(entries)));
}

static shared_ptr<Entry> t_list_zero_count(const char* name, EntryList&& entries) {
  return shared_ptr<Entry>(new Entry(name, Type::LIST_ZERO_COUNT, move(entries)));
}

static shared_ptr<Entry> t_list_one_count(const char* name, EntryList&& entries) {
  return shared_ptr<Entry>(new Entry(name, Type::LIST_ONE_COUNT, move(entries)));
}

static const unordered_map<uint32_t, ResourceFile::TemplateEntryList> system_templates({
  {'acur', {
    t_word("Number of frames (cursors)", false),
    t_word("Used frame counter", false),
    t_list_eof("Frames", {
      t_word("CURS resource ID"),
      t_zero(2),
    }),
  }},
  {'ALRT', {
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
    // Seems to be present in only some resources:
    // t_word_hex("Auto position")
  }},
  {'APPL', {
    t_list_eof("Entries", {
      t_ostype("Creator"),
      t_long("Directory"),
      t_pstring("Application", true),
    }),
  }},
  {'BNDL', {
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
  {'CNTL', {
    t_rect("Bounds"),
    t_word("Value"),
    t_bool("Visible"),
    t_word("Max"),
    t_word("Min"),
    t_word("ProcID"),
    t_long("RefCon"),
    t_pstring("Title"),
  }},
  // TODO: Info is non-text for several item types; we should make a real
  // renderer or something
  {'DITL', {
    t_list_zero_count("Items", {
      t_zero(4),
      t_rect("Bounds"),
      t_byte("Type"),
      t_pstring("Info", true, true),
    }),
  }},
  {'DLOG', {
    t_rect("Bounds"),
    t_word("ProcID"),
    t_bool("Visible"),
    t_bool("GoAway"),
    t_long("RefCon"),
    t_word("ItemsID"),
    t_pstring("Title"),
    // Seems to be present in only some resources:
    // t_word_hex("Auto position")
  }},
  {'FREF', {
    t_ostype("File type"),
    t_word("LocalID"),
    t_pstring("File name"),
  }},
  {'MBAR', {
    t_list_one_count("Menus", {
      t_word("Resource ID"),
    }),
  }},
  {'MENU', {
    t_word("Menu ID"),
    t_zero(2), // width
    t_zero(2), // height
    t_word("ProcID"),
    t_zero(2),
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
  {'WIND', {
    t_rect("Bounds"),
    t_word("ProcID"),
    t_bool("Visible"),
    t_bool("GoAway"),
    t_long("RefCon"),
    t_pstring("Title", true),
    // Seems to be present in only some resources:
    // t_word_hex("Auto position")
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