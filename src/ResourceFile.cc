#include "ResourceFile.hh"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <deque>
#include <exception>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <phosg/Process.hh>
#include <phosg/Time.hh>
#include <stdexcept>
#include <vector>
#include <string>

#include "AudioCodecs.hh"
#include "TextCodecs.hh"
#include "Lookups.hh"
#include "ResourceCompression.hh"
#include "QuickDrawFormats.hh"
#include "QuickDrawEngine.hh"
#include "Emulators/M68KEmulator.hh"
#include "Emulators/PPC32Emulator.hh"
#include "Decompressors/System.hh"

using namespace std;



string string_for_resource_type(uint32_t type) {
  string result;
  for (ssize_t s = 24; s >= 0; s -= 8) {
    uint8_t ch = (type >> s) & 0xFF;
    if (ch == '\\') {
      result += "\\\\";
    } else if (unsigned(ch) < 0x20) {
      result += string_printf("\\x%02hhX", ch);
    } else {
      result += decode_mac_roman(ch);
    }
  }
  return result;
}

string raw_string_for_resource_type(uint32_t type) {
  string result;
  for (ssize_t s = 24; s >= 0; s -= 8) {
    result += static_cast<char>((type >> s) & 0xFF);
  }
  return result;
}


void ResourceFile::add_name_index_entry(shared_ptr<Resource> res) {
  if (!res->name.empty()) {
    this->name_to_resource.emplace(res->name, res);
  }
}

void ResourceFile::delete_name_index_entry(shared_ptr<Resource> res) {
  if (!res->name.empty()) {
    for (auto its = this->name_to_resource.equal_range(res->name);
         its.first != its.second;) {
      if (its.first->second == res) {
        its.first = this->name_to_resource.erase(its.first);
      } else {
        its.first++;
      }
    }
  }
}

uint64_t ResourceFile::make_resource_key(uint32_t type, int16_t id) {
  return (static_cast<uint64_t>(type) << 16) | (static_cast<uint64_t>(id) & 0xFFFF);
}

uint32_t ResourceFile::type_from_resource_key(uint64_t key) {
  return (key >> 16) & 0xFFFFFFFF;
}

int16_t ResourceFile::id_from_resource_key(uint64_t key) {
  return key & 0xFFFF;
}

ResourceFile::Resource::Resource() : type(0), id(0), flags(0) { }

ResourceFile::Resource::Resource(uint32_t type, int16_t id, const string& data)
  : type(type), id(id), flags(0), data(data) { }

ResourceFile::Resource::Resource(uint32_t type, int16_t id, string&& data)
  : type(type), id(id), flags(0), data(move(data)) { }

ResourceFile::Resource::Resource(uint32_t type, int16_t id, uint16_t flags, const string& name, const string& data)
  : type(type), id(id), flags(flags), name(name), data(data) { }

ResourceFile::Resource::Resource(uint32_t type, int16_t id, uint16_t flags, string&& name, string&& data)
  : type(type), id(id), flags(flags), name(move(name)), data(move(data)) { }

ResourceFile::ResourceFile() : ResourceFile(IndexFormat::NONE) { }

ResourceFile::ResourceFile(IndexFormat format) : format(format) { }

bool ResourceFile::add(const Resource& res_obj) {
  shared_ptr<Resource> res(new Resource(res_obj));
  return this->add(res);
}

bool ResourceFile::add(Resource&& res_obj) {
  shared_ptr<Resource> res(new Resource(move(res_obj)));
  return this->add(res);
}

bool ResourceFile::add(shared_ptr<Resource> res) {
  uint64_t key = this->make_resource_key(res->type, res->id);
  auto emplace_ret = this->key_to_resource.emplace(key, res);
  if (emplace_ret.second) {
    this->add_name_index_entry(res);
  }
  return emplace_ret.second;
}

bool ResourceFile::change_id(uint32_t type, int16_t current_id, int16_t new_id) {
  uint64_t current_key = this->make_resource_key(type, current_id);
  uint64_t new_key = this->make_resource_key(type, new_id);
  auto it = this->key_to_resource.find(current_key);
  if (it != this->key_to_resource.end()) {
    if (current_id != new_id) {
      auto res = it->second;
      this->key_to_resource.erase(it);
      res->id = new_id;
      this->key_to_resource.emplace(new_key, res);
    }
    return true;
  }
  return false;
}

bool ResourceFile::rename(uint32_t type, int16_t id, const string& new_name) {
  if (new_name.size() > 0xFF) {
    throw invalid_argument("name must be 255 bytes or shorter");
  }
  uint64_t key = this->make_resource_key(type, id);
  auto it = this->key_to_resource.find(key);
  if (it != this->key_to_resource.end()) {
    auto& res = it->second;
    this->delete_name_index_entry(res);
    res->name = new_name;
    this->add_name_index_entry(res);
    return true;
  }
  return false;
}

bool ResourceFile::remove(uint32_t type, int16_t id) {
  uint64_t key = this->make_resource_key(type, id);
  auto it = this->key_to_resource.find(key);
  if (it != this->key_to_resource.end()) {
    this->delete_name_index_entry(it->second);
    this->key_to_resource.erase(it);
    return true;
  }
  return false;
}

IndexFormat ResourceFile::index_format() const {
  return this->format;
}

bool ResourceFile::resource_exists(uint32_t type, int16_t id) const {
  return this->key_to_resource.count(this->make_resource_key(type, id));
}

bool ResourceFile::resource_exists(uint32_t type, const char* name) const {
  auto its = this->name_to_resource.equal_range(name);
  for (; its.first != its.second; its.first++) {
    if (its.first->second->type == type) {
      return true;
    }
  }
  return false;
}

shared_ptr<ResourceFile::Resource> ResourceFile::get_resource(
    uint32_t type, int16_t id, uint64_t decompress_flags) {
  auto res = this->key_to_resource.at(this->make_resource_key(type, id));
  decompress_resource(res, decompress_flags, this);
  return res;
}

shared_ptr<ResourceFile::Resource> ResourceFile::get_resource(
    uint32_t type, const char* name, uint64_t decompress_flags) {
  auto its = this->name_to_resource.equal_range(name);
  for (; its.first != its.second; its.first++) {
    auto res = its.first->second;
    if (res->type == type) {
      decompress_resource(res, decompress_flags, this);
      return res;
    }
  }
  throw out_of_range("no such resource");
}

shared_ptr<const ResourceFile::Resource> ResourceFile::get_resource(
    uint32_t type, int16_t id) const {
  return this->key_to_resource.at(this->make_resource_key(type, id));
}

shared_ptr<const ResourceFile::Resource> ResourceFile::get_resource(
    uint32_t type, const char* name) const {
  auto its = this->name_to_resource.equal_range(name);
  for (; its.first != its.second; its.first++) {
    auto res = its.first->second;
    if (res->type == type) {
      return res;
    }
  }
  throw out_of_range("no such resource");
}

vector<int16_t> ResourceFile::all_resources_of_type(uint32_t type) const {
  vector<int16_t> ret;
  for (auto it = this->key_to_resource.lower_bound(this->make_resource_key(type, 0));
       it != this->key_to_resource.end(); it++) {
    if (this->type_from_resource_key(it->first) != type) {
      break;
    }
    ret.emplace_back(this->id_from_resource_key(it->first));
  }
  return ret;
}

vector<uint32_t> ResourceFile::all_resource_types() const {
  vector<uint32_t> ret;
  for (auto it : this->key_to_resource) {
    uint32_t type = this->type_from_resource_key(it.first);
    if (ret.empty() || ret.back() != type) {
      ret.emplace_back(type);
    }
  }
  return ret;
}

vector<pair<uint32_t, int16_t>> ResourceFile::all_resources() const {
  vector<pair<uint32_t, int16_t>> ret;
  for (const auto& it : this->key_to_resource) {
    ret.emplace_back(make_pair(
        this->type_from_resource_key(it.first), this->id_from_resource_key(it.first)));
  }
  return ret;
}

uint32_t ResourceFile::find_resource_by_id(int16_t id,
    const vector<uint32_t>& types) {
  for (uint32_t type : types) {
    if (this->resource_exists(type, id)) {
      return type;
    }
  }
  throw runtime_error(string_printf("referenced resource %hd not found", id));
}



////////////////////////////////////////////////////////////////////////////////
// Meta resources

ResourceFile::TemplateEntry::TemplateEntry(
    string&& name,
    Type type,
    Format format,
    uint16_t width,
    uint8_t end_alignment,
    uint8_t align_offset,
    bool is_signed,
    std::map<int64_t, std::string> case_names)
  : name(move(name)),
    type(type),
    format(format),
    width(width),
    end_alignment(end_alignment),
    align_offset(align_offset),
    is_signed(is_signed),
    case_names(std::move(case_names)) { }

ResourceFile::TemplateEntry::TemplateEntry(
    string&& name,
    Type type,
    TemplateEntryList&& list_entries)
  : name(move(name)),
    type(type),
    format(Format::FLAG),
    width(2),
    end_alignment(0),
    align_offset(0),
    is_signed(true),
    list_entries(move(list_entries)) { }

ResourceFile::TemplateEntryList ResourceFile::decode_TMPL(int16_t id, uint32_t type) {
  return this->decode_TMPL(this->get_resource(type, id));
}

ResourceFile::TemplateEntryList ResourceFile::decode_TMPL(shared_ptr<const Resource> res) {
  return ResourceFile::decode_TMPL(res->data.data(), res->data.size());
}

ResourceFile::TemplateEntryList ResourceFile::decode_TMPL(const void* data, size_t size) {
  StringReader r(data, size);

  using Entry = TemplateEntry;
  using Type = Entry::Type;
  using Format = Entry::Format;

  vector<shared_ptr<Entry>> ret;
  vector<vector<shared_ptr<Entry>>*> write_stack;
  write_stack.emplace_back(&ret);
  bool in_bbit_array = false;
  while (!r.eof()) {
    string name = decode_mac_roman(r.readx(r.get_u8()));
    uint32_t type = r.get_u32b();

    if (write_stack.empty()) {
      throw runtime_error("TMPL ended list with no list open");
    }
    auto* entries = write_stack.back();

    if (in_bbit_array && type != 0x42424954) {
      throw runtime_error("BBIT array length is not a multiple of 8");
    }

    // For templates supported by Resorcerer, see http://www.mathemaesthetics.com/ResTemplates.html
    switch (type) {
      case 0x44564452: // DVDR; Resorcerer-only. Divider line with comment (0 bytes)
        entries->emplace_back(new Entry(move(name), Type::VOID, Format::DECIMAL, 0, 0, 0));
        break;
      case 0x43415345: { // CASE; Resorcerer-only. Symbolic and/or default value (0 bytes)
        // These appear to be of the format <name>=<value>. <value> is an
        // integer in decimal format or hex (preceded by a $).
        auto tokens = split(name, '=');
        if (tokens.size() != 2) {
          throw runtime_error("CASE entry does not contain exactly one '=' character");
        }
        if (tokens[1].empty()) {
          throw runtime_error("CASE value token is empty");
        }
        name = tokens[0];
        int32_t value = (tokens[1][0] == '$')
            ? strtol(&tokens[1][1], nullptr, 16)
            : stol(tokens[1], nullptr, 10);
        entries->back()->case_names.emplace(value, move(tokens[0]));
        break;
      }
      case 0x55425954: // UBYT; Resorcerer-only. "unsigned decimal byte"
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::DECIMAL, 1, 0, 0, false));
        break;
      case 0x55575244: // UWRD; Resorcerer-only. "unsigned decimal word"
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::DECIMAL, 2, 0, 0, false));
        break;
      case 0x554C4E47: // ULNG; Resorcerer-only. "unsigned decimal long"
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::DECIMAL, 4, 0, 0, false));
        break;
      case 0x44415445: // DATE; Resorcerer-only. Macintosh System Date/Time (seconds)
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::DATE, 4, 0, 0, false));
        break;
      case 0x44425954: // DBYT; Resorcerer-only. "signed decimal byte"
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::DECIMAL, 1, 0, 0));
        break;
      case 0x44575244: // DWRD; Resorcerer-only. "signed decimal word"
      case 0x52534944: // RSID; Resorcerer-only. "resource ID"
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::DECIMAL, 2, 0, 0));
        break;
      case 0x444C4E47: // DLNG; Resorcerer-only. "signed decimal long"
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::DECIMAL, 4, 0, 0));
        break;
      case 0x48425954: // HBYT; Resorcerer-only. "hex byte"
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::HEX, 1, 0, 0));
        break;
      case 0x48575244: // HWRD; Resorcerer-only. "hex word"
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::HEX, 2, 0, 0));
        break;
      case 0x484C4E47: // HLNG; Resorcerer-only. "hex long"
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::HEX, 4, 0, 0));
        break;
      case 0x46495844: // FIXD; Resorcerer-only. 16:16 Fixed Point Number.
        // .width specifies the width per component (16+16=32)
        entries->emplace_back(new Entry(move(name), Type::FIXED_POINT, Format::DECIMAL, 2, 0, 0));
        break;
      case 0x504E5420: // PNT ; Resorcerer-only. QuickDraw Point.
        // .width specifies the width per component (16+16=32)
        entries->emplace_back(new Entry(move(name), Type::POINT_2D, Format::DECIMAL, 2, 0, 0));
        break;
      case 0x41575244: // AWRD; align to 2-byte boundary
        entries->emplace_back(new Entry(move(name), Type::ALIGNMENT, Format::HEX, 0, 2, 0));
        break;
      case 0x414C4E47: // ALNG; align to 2-byte boundary
        entries->emplace_back(new Entry(move(name), Type::ALIGNMENT, Format::HEX, 0, 4, 0));
        break;
      case 0x46425954: // FBYT; fill byte
        entries->emplace_back(new Entry(move(name), Type::ZERO_FILL, Format::HEX, 1, 0, 0));
        break;
      case 0x46575244: // FWRD; fill word
        entries->emplace_back(new Entry(move(name), Type::ZERO_FILL, Format::HEX, 2, 0, 0));
        break;
      case 0x464C4E47: // FLNG; fill long
        entries->emplace_back(new Entry(move(name), Type::ZERO_FILL, Format::HEX, 4, 0, 0));
        break;
      case 0x48455844: // HEXD; hex dump
        entries->emplace_back(new Entry(move(name), Type::EOF_STRING, Format::HEX, 0, 0, 0));
        break;
      case 0x50535452: // PSTR; Pascal string
        entries->emplace_back(new Entry(move(name), Type::PSTRING, Format::TEXT, 1, 0, 0));
        break;
      case 0x57535452: // WSTR; Pascal string with word-sized length
        entries->emplace_back(new Entry(move(name), Type::PSTRING, Format::TEXT, 2, 0, 0));
        break;
      case 0x4C535452: // LSTR; Pascal string with long-sized length
        entries->emplace_back(new Entry(move(name), Type::PSTRING, Format::TEXT, 4, 0, 0));
        break;
      case 0x45535452: // ESTR; even-padded Pascal string
        entries->emplace_back(new Entry(move(name), Type::PSTRING, Format::TEXT, 1, 2, 0));
        break;
      case 0x4F535452: // OSTR; odd-padded Pascal string
        entries->emplace_back(new Entry(move(name), Type::PSTRING, Format::TEXT, 1, 2, 1));
        break;
      case 0x43535452: // CSTR; C string (null-terminated)
        entries->emplace_back(new Entry(move(name), Type::CSTRING, Format::TEXT, 1, 0, 0));
        break;
      case 0x45435354: // ECST; even-padded C string
        entries->emplace_back(new Entry(move(name), Type::CSTRING, Format::TEXT, 1, 2, 0));
        break;
      case 0x4F435354: // OCST; odd-padded C string
        entries->emplace_back(new Entry(move(name), Type::CSTRING, Format::TEXT, 1, 2, 1));
        break;
      case 0x424F4F4C: // BOOL; boolean word
        entries->emplace_back(new Entry(move(name), Type::BOOL, Format::FLAG, 2, 0, 0));
        break;
      case 0x42424954: // BBIT; bit within a word
        if (in_bbit_array) {
          entries->emplace_back(new Entry(move(name), Type::BOOL, Format::FLAG, 2, 0, 0));
          if (entries->size() == 8) {
            write_stack.pop_back();
            in_bbit_array = false;
          }
        } else {
          entries->emplace_back(new Entry("", Type::BITFIELD, Format::FLAG, 1, 0, 0));
          entries = write_stack.emplace_back(&entries->back()->list_entries);
          entries->emplace_back(new Entry(move(name), Type::BOOL, Format::FLAG, 2, 0, 0));
          in_bbit_array = true;
        }
        break;
      case 0x43484152: // CHAR; ASCII character
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::TEXT, 1, 0, 0));
        break;
      case 0x544E414D: // TNAM; type name
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::TEXT, 4, 0, 0));
        break;
      case 0x52454354: // RECT; QuickDraw Rectangle
        entries->emplace_back(new Entry(move(name), Type::RECT, Format::DECIMAL, 2, 0, 0));
        break;
      case 0x434F4C52: // COLR; QuickDraw Color RGB Triplet
        entries->emplace_back(new Entry(move(name), Type::COLOR, Format::HEX, 2, 0, 0, false));
        break;
      case 0x4C53545A: // LSTZ
        entries->emplace_back(new Entry(move(name), Type::LIST_ZERO_BYTE, Format::FLAG, 0, 0, 0));
        write_stack.emplace_back(&entries->back()->list_entries);
        break;
      case 0x4C535442: // LSTB
        entries->emplace_back(new Entry(move(name), Type::LIST_EOF, Format::FLAG, 0, 0, 0));
        write_stack.emplace_back(&entries->back()->list_entries);
        break;
      case 0x5A434E54: // ZCNT
        entries->emplace_back(new Entry(move(name), Type::LIST_ZERO_COUNT, Format::HEX, 2, 0, 0));
        write_stack.emplace_back(&entries->back()->list_entries);
        break;
      case 0x4F434E54: // OCNT
        entries->emplace_back(new Entry(move(name), Type::LIST_ONE_COUNT, Format::HEX, 2, 0, 0));
        write_stack.emplace_back(&entries->back()->list_entries);
        break;
      case 0x4C434E54: // LCNT; Resorcerer-only. One-based long count of list items
        entries->emplace_back(new Entry(move(name), Type::LIST_ONE_COUNT, Format::HEX, 4, 0, 0));
        write_stack.emplace_back(&entries->back()->list_entries);
        break;
      case 0x4C535443: { // LSTC
        // ZCNT or OCNT should have already opened the list; make sure that it
        // was the previous command.
        if (write_stack.size() < 2) {
          throw runtime_error("LSTC without preceding ZCNT/OCNT/LCNT in TMPL");
        }
        if (!write_stack.back()->empty()) {
          throw runtime_error("commands between ZCNT/OCNT/LCNT and LSTC");
        }
        auto list_type = write_stack[write_stack.size() - 2]->back()->type;
        if ((list_type != Type::LIST_ZERO_COUNT) && (list_type != Type::LIST_ONE_COUNT)) {
          throw runtime_error("LSTC used for non-ZCNT/OCNT list");
        }
        break;
      }
      case 0x4C535445: // LSTE
        write_stack.pop_back();
        break;
      case 0x50313030: // P100; Pnnn = Pascal string with size 0xnnn / fixed-size Pascal string with NUL-byte padding
        //
        // According to an alert in Resorcerer when editing a resource that uses a template with an odd Pnnn in it,
        // this can either be:
        //  - a Pascal string of "0xnnn" characters, prefixed with the length byte (ResEdit)
        //  - a Pascal string of "0xnnn - 1" characters, prefixed with the length byte (Resorcerer)
        //
        // As a Pascal string can be at most 255 (0xFF) characters long, if "nnn" is > 0xFF, the remaining bytes are
        // filled with NUL-bytes.
        //
        // This appears to be a bug. Stuffit Expander has a TMPL with a P100
        // field in it, but P100 isn't valid - a 1-byte pstring can only be 0xFF
        // bytes long. It looks like whoever wrote the TMPL mistakely included
        // the length byte here... reading some of the resources that would
        // match the relevant TMPL makes it look like we should treat P100 as if
        // it were P0FF, so that's what we'll do.
        entries->emplace_back(new Entry(move(name), Type::FIXED_PSTRING, Format::TEXT, 0xFF, 0, 0));
        break;
      default:
        try {
          if ((type & 0xFF000000) == 0x48000000) {
            // Hnnn (fixed-length hex dump)
            uint16_t width = 
                value_for_hex_char(type & 0xFF) |
                (value_for_hex_char((type >> 8) & 0xFF) << 4) |
                (value_for_hex_char((type >> 16) & 0xFF) << 8);
            entries->emplace_back(new Entry(move(name), Type::STRING, Format::HEX, width, 0, 0));
          } else if ((type & 0xFF000000) == 0x43000000) {
            // Cnnn (C string with fixed NUL-byte padding)
            uint16_t width = 
                value_for_hex_char(type & 0xFF) |
                (value_for_hex_char((type >> 8) & 0xFF) << 4) |
                (value_for_hex_char((type >> 16) & 0xFF) << 8);
            entries->emplace_back(new Entry(move(name), Type::FIXED_CSTRING, Format::TEXT, width, 0, 0));
          } else if ((type & 0xFFFF0000) == 0x50300000) {
            // P0nn (Pascal string with fixed NUL-byte padding)
            uint16_t width = 
                value_for_hex_char(type & 0xFF) |
                (value_for_hex_char((type >> 8) & 0xFF) << 4);
            entries->emplace_back(new Entry(move(name), Type::FIXED_PSTRING, Format::TEXT, width, 0, 0));
          } else {
            throw runtime_error("unknown field type: " + string_for_resource_type(type));
          }
        } catch (const out_of_range&) {
          throw runtime_error("unknown field type: " + string_for_resource_type(type));
        }
    }
  }

  if (write_stack.size() != 1) {
    throw runtime_error("unterminated list in TMPL");
  }
  return ret;
}

static string format_template_string(ResourceFile::TemplateEntry::Format format, const string& str, bool has_name) {
  using Entry = ResourceFile::TemplateEntry;
  using Format = Entry::Format;
  
  if (format == Format::HEX) {
    return format_data_string(str);
  } else if (format == Format::TEXT) {
    if (has_name) {
      return "'" + str + "'";
    } else {
      return str;
    }
  } else {
    throw logic_error("invalid string display format");
  }
};

static string format_template_integer(shared_ptr<const ResourceFile::TemplateEntry> entry, int64_t value) {
  using Entry = ResourceFile::TemplateEntry;
  using Format = Entry::Format;
  
  string case_name_suffix;
  try {
    case_name_suffix = string_printf(" (%s)", entry->case_names.at(value).c_str());
  } catch (const out_of_range&) { }

  switch (entry->format) {
    case Format::DECIMAL:
      return string_printf("%" PRId64 "%s", value, case_name_suffix.c_str());
    case Format::HEX:
    case Format::FLAG:
      if (entry->width == 1) {
        if (entry->is_signed && (value & 0x80)) {
          return string_printf("-0x%02hhX%s", static_cast<uint8_t>(-value), case_name_suffix.c_str());
        } else {
          return string_printf("0x%02hhX%s", static_cast<uint8_t>(value), case_name_suffix.c_str());
        }
      } else if (entry->width == 2) {
        if (entry->is_signed && (value & 0x8000)) {
          return string_printf("-0x%04hX%s", static_cast<uint16_t>(-value), case_name_suffix.c_str());
        } else {
          return string_printf("0x%04hX%s", static_cast<uint16_t>(value), case_name_suffix.c_str());
        }
      } else if (entry->width == 4) {
        if (entry->is_signed && (value & 0x80000000)) {
          return string_printf("-0x%08X%s", static_cast<uint32_t>(-value), case_name_suffix.c_str());
        } else {
          return string_printf("0x%08X%s", static_cast<uint32_t>(value), case_name_suffix.c_str());
        }
      } else {
        throw logic_error("invalid integer width");
      }
    case Format::TEXT:
      if (entry->width == 1) {
        if (value < 0x20 || value > 0x7E) {
          return string_printf("0x%0" PRIX64 "%s", value, case_name_suffix.c_str());
        } else {
          return string_printf("\'%c\' (0x%02" PRIX64 ")%s",
              static_cast<char>(value), value, case_name_suffix.c_str());
        }
      } else if (entry->width == 2) {
        char ch1 = static_cast<char>((value >> 8) & 0xFF);
        char ch2 = static_cast<char>(value & 0xFF);
        if (ch1 < 0x20 || ch1 > 0x7E || ch2 < 0x20 || ch2 > 0x7E) {
          return string_printf("0x%04" PRIX64 "%s", value, case_name_suffix.c_str());
        } else {
          return string_printf("\'%c%c\' (0x%04" PRIX64 ")%s", ch1, ch2, value, case_name_suffix.c_str());
        }
      } else if (entry->width == 4) {
        char ch[] = {
          static_cast<char>((value >> 24) & 0xFF),
          static_cast<char>((value >> 16) & 0xFF),
          static_cast<char>((value >> 8) & 0xFF),
          static_cast<char>(value & 0xFF)
        };
        if ((unsigned(ch[0]) < 0x20) || (unsigned(ch[1]) < 0x20) ||
            (unsigned(ch[2]) < 0x20) || (unsigned(ch[3]) < 0x20)) {
          return string_printf("0x%08" PRIX64 "%s", value, case_name_suffix.c_str());
        } else {
          return string_printf("\'%s\' (0x%08" PRIX64 ")%s", decode_mac_roman(ch, 4).c_str(), value, case_name_suffix.c_str());
        }
      } else {
        throw logic_error("invalid integer width");
      }
    case Format::DATE: {
      // Classic Mac timestamps are based on 1904-01-01 instead of 1970-01-01
      int64_t ts = value - 2082826800;
      if (ts < 0) {
        // TODO: Handle this case properly. Probably it's quite rare
        return string_printf("%" PRId64 " seconds before 1970-01-01 00:00:00 (classic: 0x%08" PRIX64 ")%s",
            -ts, value, case_name_suffix.c_str());
      } else {
        return format_time(ts * 1000000) + string_printf(" (classic: 0x%" PRIX64 ")%s", value, case_name_suffix.c_str());
      }
    }
    default:
      throw logic_error("invalid integer display format");
  }
};

static string format_template_bool(shared_ptr<const ResourceFile::TemplateEntry> entry, bool value) {
  string case_name_suffix;
  try {
    case_name_suffix = string_printf(" (%s)", entry->case_names.at(value).c_str());
  } catch (const out_of_range&) { }
  
  return (value ? "true" : "false") + case_name_suffix;
}

static void disassemble_from_template_inner(
    deque<string>& lines,
    StringReader& r,
    const ResourceFile::TemplateEntryList& entries,
    size_t indent_level) {

  using Entry = ResourceFile::TemplateEntry;
  using Type = Entry::Type;
  using Format = Entry::Format;

  for (const auto& entry : entries) {
    string prefix(indent_level * 2, ' ');
    if (entry->type == Type::VOID) {
      if (entry->name.empty()) {
        lines.emplace_back(prefix + "# (empty comment)");
      } else {
        lines.emplace_back(prefix + "# " + entry->name);
      }
      continue;
    }

    if (!entry->name.empty()) {
      prefix += entry->name;
      prefix += ": ";
    }

    switch (entry->type) {
      // Note: Type::VOID is already handled above
      case Type::ZERO_FILL:
        if ((entry->width != 1) && (entry->width != 2) && (entry->width != 4)) {
          string data = r.readx(entry->width);
          if (data.find_first_not_of('\0') != string::npos) {
            lines.emplace_back(prefix + format_data_string(data) + " (type = zero fill in template)");
          }
          continue;
        }

        // Handle ZERO_FILL like INTEGER if its size matches an integer
        [[fallthrough]];
      case Type::INTEGER: {
        int64_t value;
        if (entry->is_signed) {
          if (entry->width == 1) {
            value = r.get_s8();
          } else if (entry->width == 2) {
            value = r.get_s16b();
          } else if (entry->width == 4) {
            value = r.get_s32b();
          } else {
            throw logic_error("invalid width in disassemble_from_template");
          }
        } else {
          if (entry->width == 1) {
            value = r.get_u8();
          } else if (entry->width == 2) {
            value = r.get_u16b();
          } else if (entry->width == 4) {
            value = r.get_u32b();
          } else {
            throw logic_error("invalid width in disassemble_from_template");
          }
        }
        if (entry->end_alignment) {
          throw logic_error("integer has nonzero end_alignment");
        }

        if (entry->type == Type::INTEGER) {
          lines.emplace_back(prefix + format_template_integer(entry, value));
        } else if (entry->type == Type::ZERO_FILL && value != 0) {
          lines.emplace_back(prefix + format_template_integer(entry, value) + " (type = zero fill in template)");
        }
        break;
      }
      case Type::ALIGNMENT: {
        uint8_t boundary = entry->end_alignment;
        uint8_t offset = entry->align_offset;
        
        if (boundary != 0) {
          // We currently only support offset == 0 or (offset == 1 and boundary == 2)
          // since those seem to be the only cases supported by the TMPL format.
          if (offset == 1) {
            if (boundary != 2) {
              throw logic_error("boundary must be 2 when offset is 1");
            }
            if (!(r.where() & 1)) {
              r.skip(1);
            }
          } else if (offset == 0) {
            r.go((r.where() + (boundary - 1)) & (~(boundary - 1)));
          } else {
            throw logic_error("offset is not 1 or 0");
          }
        }
        break;
      }
      case Type::FIXED_POINT: {
        int16_t integer_part = r.get_s16b();
        uint16_t fractional_part = r.get_u16b();
        if (entry->format == Format::DECIMAL) {
          double value = (integer_part >= 0)
              ? (integer_part + static_cast<double>(fractional_part) / 65536)
              : (integer_part - static_cast<double>(fractional_part) / 65536);
          lines.emplace_back(string_printf("%s%lg\n", prefix.c_str(), value));
        } else if (entry->format == Format::HEX) {
          lines.emplace_back(string_printf("%s%s0x%d.0x%hu\n", prefix.c_str(),
              integer_part < 0 ? "-" : "",
              (integer_part < 0) ? -integer_part : integer_part,
              fractional_part));
        } else {
          throw logic_error("invalid fixed-point display format");
        }
        break;
      }
      case Type::EOF_STRING:
        lines.emplace_back(prefix + format_template_string(entry->format, r.read(r.remaining()), !entry->name.empty()));
        break;
      case Type::STRING:
        lines.emplace_back(prefix + format_template_string(entry->format, r.readx(entry->width), !entry->name.empty()));
        break;
      case Type::PSTRING:
      case Type::CSTRING: {
        string data;
        if (entry->type == Type::PSTRING) {
          data = r.readx(r.get_u8());
        } else {
          data = r.get_cstr();
        }
        lines.emplace_back(prefix + format_template_string(entry->format, data, !entry->name.empty()));

        if (entry->end_alignment == 2) {
          if (((data.size() + 1) & 1) != (entry->align_offset)) {
            r.skip(1);
          }
        } else if (entry->end_alignment) {
          throw logic_error("unsupported pstring end alignment");
        }
        break;
      }
      case Type::FIXED_PSTRING: {
        size_t size = r.get_u8();
        if (size > entry->width) {
          throw runtime_error("p-string too long for field");
        }
        lines.emplace_back(prefix + format_template_string(entry->format, r.readx(size), !entry->name.empty()));
        r.skip(entry->width - size);
        break;
      }
      case Type::FIXED_CSTRING: {
        string data = r.get_cstr();
        if (data.size() > static_cast<size_t>(entry->width + 1)) {
          throw runtime_error("c-string too long for field");
        }
        lines.emplace_back(prefix + format_template_string(entry->format, data, !entry->name.empty()));
        r.skip(entry->width - data.size() - 1);
        break;
      }
      case Type::BOOL:
        // Note: Yes, Type::BOOL apparently is actually 2 bytes.
        lines.emplace_back(prefix + format_template_bool(entry, r.get_u16b()));
        break;
      case Type::POINT_2D: {
        Point pt = r.get<Point>();
        string x_str = format_template_integer(entry, pt.x);
        string y_str = format_template_integer(entry, pt.y);
        lines.emplace_back(prefix + "x=" + x_str + ", y=" + y_str);
        break;
      }
      case Type::RECT: {
        Rect rect = r.get<Rect>();
        string x1_str = format_template_integer(entry, rect.x1);
        string y1_str = format_template_integer(entry, rect.y1);
        string x2_str = format_template_integer(entry, rect.x2);
        string y2_str = format_template_integer(entry, rect.y2);
        lines.emplace_back(prefix + "x1=" + x1_str + ", y1=" + y1_str + ", x2=" + x2_str + ", y2=" + y2_str);
        break;
      }
      case Type::COLOR: {
        Color c = r.get<Color>();
        string r_str = format_template_integer(entry, c.r);
        string g_str = format_template_integer(entry, c.g);
        string b_str = format_template_integer(entry, c.b);
        lines.emplace_back(prefix + "r=" + r_str + ", g=" + g_str + ", b=" + b_str);
        break;
      }
      case Type::BITFIELD: {
        uint8_t flags = r.get_u8();
        for (const auto& bit_entry : entry->list_entries) {
          lines.emplace_back(prefix + bit_entry->name + ": " + format_template_bool(bit_entry, flags & 0x80));
          flags <<= 1;
        }
        break;
      }
      case Type::LIST_ZERO_BYTE:
        lines.emplace_back(prefix + "(zero-terminated list)");
        for (size_t z = 0; r.get_u8(false); z++) {
          string item_prefix(indent_level * 2, ' ');
          item_prefix += entry->name;
          lines.emplace_back(item_prefix + string_printf("[%zu]", z));
          disassemble_from_template_inner(lines, r, entry->list_entries, indent_level + 1);
        }
        break;
      case Type::LIST_EOF:
        lines.emplace_back(prefix + "(EOF-terminated list)");
        for (size_t z = 0; !r.eof(); z++) {
          string item_prefix(indent_level * 2, ' ');
          item_prefix += entry->name;
          lines.emplace_back(item_prefix + string_printf("[%zu]", z));
          disassemble_from_template_inner(lines, r, entry->list_entries, indent_level + 1);
        }
        break;
      case Type::LIST_ZERO_COUNT:
      case Type::LIST_ONE_COUNT: {
        size_t num_items;
        if (entry->width == 2) {
          num_items = r.get_u16b() + (entry->type == Type::LIST_ZERO_COUNT);
          // 0xFFFF actually means zero in LIST_ZERO_COUNT
          if (num_items == 0x10000) {
            num_items = 0;
          }
        } else if (entry->width == 4) {
          // It's (currently) not possible to get a LIST_ZERO_COUNT with a
          // 4-byte width field
          if (entry->type == Type::LIST_ZERO_COUNT) {
            throw logic_error("4-byte width LIST_ZERO_COUNT");
          }
          num_items = r.get_u32b();
        } else {
          throw logic_error("invalid list length width");
        }
        lines.emplace_back(prefix + string_printf("(%zu entries)", num_items));
        for (size_t z = 0; z < num_items; z++) {
          string item_prefix(indent_level * 2, ' ');
          item_prefix += entry->name;
          lines.emplace_back(item_prefix + string_printf("[%zu]", z));
          disassemble_from_template_inner(lines, r, entry->list_entries, indent_level + 1);
        }
        break;
      }
      
      case Type::OPT_EOF:
        if (!r.eof()) {
          disassemble_from_template_inner(lines, r, entry->list_entries, indent_level);
        }
        break;
      
      default:
        throw logic_error("unknown field type in disassemble_from_template");
    }
  }
}

string ResourceFile::disassemble_from_template(
    const void* data,
    size_t size,
    const ResourceFile::TemplateEntryList& tmpl) {
  StringReader r(data, size);
  deque<string> lines;
  disassemble_from_template_inner(lines, r, tmpl, 0);
  if (!r.eof()) {
    string extra_data = r.read(r.remaining());
    lines.emplace_back("\nNote: template did not parse all data in resource; remaining data: "
        + format_data_string(extra_data));
  }
  return join(lines, "\n");
}



////////////////////////////////////////////////////////////////////////////////
// CODE helpers

struct SizeResource {
  be_uint16_t flags;
  be_uint32_t size;
  be_uint32_t min_size;
} __attribute__((packed));

ResourceFile::DecodedSizeResource ResourceFile::decode_SIZE(int16_t id, uint32_t type) {
  return this->decode_SIZE(this->get_resource(type, id));
}

ResourceFile::DecodedSizeResource ResourceFile::decode_SIZE(shared_ptr<const Resource> res) {
  return ResourceFile::decode_SIZE(res->data.data(), res->data.size());
}

ResourceFile::DecodedSizeResource ResourceFile::decode_SIZE(const void* vdata, size_t size) {
  StringReader r(vdata, size);
  const auto& size_res = r.get<SizeResource>();

  DecodedSizeResource decoded;
  decoded.save_screen = !!(size_res.flags & 0x8000);
  decoded.accept_suspend_events = !!(size_res.flags & 0x4000);
  decoded.disable_option = !!(size_res.flags & 0x2000);
  decoded.can_background = !!(size_res.flags & 0x1000);
  decoded.activate_on_fg_switch = !!(size_res.flags & 0x0800);
  decoded.only_background = !!(size_res.flags & 0x0400);
  decoded.get_front_clicks = !!(size_res.flags & 0x0200);
  decoded.accept_died_events = !!(size_res.flags & 0x0100);
  decoded.clean_addressing = !!(size_res.flags & 0x0080);
  decoded.high_level_event_aware = !!(size_res.flags & 0x0040);
  decoded.local_and_remote_high_level_events = !!(size_res.flags & 0x0020);
  decoded.stationery_aware = !!(size_res.flags & 0x0010);
  decoded.use_text_edit_services = !!(size_res.flags & 0x0008);
  // Low 3 bits in size_res.flags are unused
  decoded.size = size_res.size;
  decoded.min_size = size_res.min_size;
  return decoded;
}

ResourceFile::DecodedVersionResource ResourceFile::decode_vers(int16_t id, uint32_t type) {
  return this->decode_vers(this->get_resource(type, id));
}

ResourceFile::DecodedVersionResource ResourceFile::decode_vers(shared_ptr<const Resource> res) {
  return ResourceFile::decode_vers(res->data.data(), res->data.size());
}

ResourceFile::DecodedVersionResource ResourceFile::decode_vers(const void* vdata, size_t size) {
  StringReader r(vdata, size);
  DecodedVersionResource decoded;
  decoded.major_version = r.get_u8();
  decoded.minor_version = r.get_u8();
  decoded.development_stage = r.get_u8();
  decoded.prerelease_version_level = r.get_u8();
  decoded.region_code = r.get_u16b();
  decoded.version_number = r.readx(r.get_u8());
  decoded.version_message = decode_mac_roman(r.readx(r.get_u8()));
  return decoded;
}

struct CodeFragmentResourceEntry {
  be_uint32_t architecture;
  be_uint16_t reserved1;
  uint8_t reserved2;
  uint8_t update_level;
  be_uint32_t current_version;
  be_uint32_t old_def_version;
  be_uint32_t app_stack_size;
  union {
    be_int16_t app_subdir_id;
    be_uint16_t lib_flags;
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

  be_uint32_t offset;
  be_uint32_t length; // If zero, fragment fills the entire space (e.g. entire data fork)
  union {
    be_uint32_t space_id;
    be_uint32_t fork_kind;
  } __attribute__((packed)) space;
  be_uint16_t fork_instance;
  be_uint16_t extension_count;
  be_uint16_t entry_size; // Total size of this entry (incl. name) in bytes
  char name[0]; // p-string (first byte is length)
} __attribute__((packed));

struct CodeFragmentResourceHeader {
  be_uint32_t reserved1;
  be_uint32_t reserved2;
  be_uint16_t reserved3;
  be_uint16_t version;
  be_uint32_t reserved4;
  be_uint32_t reserved5;
  be_uint32_t reserved6;
  be_uint32_t reserved7;
  be_uint16_t reserved8;
  be_uint16_t entry_count;
  // Entries immediately follow this field
} __attribute__((packed));

vector<ResourceFile::DecodedCodeFragmentEntry> ResourceFile::decode_cfrg(
    int16_t id, uint32_t type) {
  return this->decode_cfrg(this->get_resource(type, id));
}

vector<ResourceFile::DecodedCodeFragmentEntry> ResourceFile::decode_cfrg(
    shared_ptr<const Resource> res) {
  return ResourceFile::decode_cfrg(res->data.data(), res->data.size());
}

vector<ResourceFile::DecodedCodeFragmentEntry> ResourceFile::decode_cfrg(
    const void* vdata, size_t size) {
  StringReader r(vdata, size);

  const auto& header = r.get<CodeFragmentResourceHeader>();
  if (header.version != 1) {
    throw runtime_error("cfrg is not version 1");
  }

  vector<DecodedCodeFragmentEntry> ret;
  while (ret.size() < header.entry_count) {
    size_t entry_start_offset = r.where();

    const auto& src_entry = r.get<CodeFragmentResourceEntry>();
    string name = r.readx(r.get_u8());

    ret.emplace_back();
    auto& ret_entry = ret.back();

    ret_entry.architecture = src_entry.architecture;
    ret_entry.update_level = src_entry.update_level;
    ret_entry.current_version = src_entry.current_version;
    ret_entry.old_def_version = src_entry.old_def_version;
    ret_entry.app_stack_size = src_entry.app_stack_size;
    ret_entry.app_subdir_id = src_entry.flags.app_subdir_id; // Also lib_flags

    if (src_entry.usage > 4) {
      throw runtime_error("code fragment entry usage is invalid");
    }
    ret_entry.usage = static_cast<DecodedCodeFragmentEntry::Usage>(src_entry.usage);

    if (src_entry.usage > 4) {
      throw runtime_error("code fragment entry location (where) is invalid");
    }
    ret_entry.where = static_cast<DecodedCodeFragmentEntry::Where>(src_entry.where);

    ret_entry.offset = src_entry.offset;
    ret_entry.length = src_entry.length;
    ret_entry.space_id = src_entry.space.space_id; // Also fork_kind
    ret_entry.fork_instance = src_entry.fork_instance;
    ret_entry.name = string(&src_entry.name[1], static_cast<size_t>(src_entry.name[0]));
    ret_entry.extension_count = src_entry.extension_count;

    // TODO: it looks like there is probably some alignment logic that we should
    // implement here (see System cfrg 0, for example)
    size_t extension_data_end_offset = entry_start_offset + src_entry.entry_size;
    if (r.where() > extension_data_end_offset) {
      throw runtime_error("code fragment entry size is smaller than header + name");
    }
    if (ret_entry.extension_count) {
      ret_entry.extension_data = r.readx(extension_data_end_offset - r.where());
    } else {
      r.skip(extension_data_end_offset - r.where());
    }
  }

  return ret;
}

struct Code0ResourceHeader {
  be_uint32_t above_a5_size;
  be_uint32_t below_a5_size;
  be_uint32_t jump_table_size; // Should be == resource_size - 0x10
  be_uint32_t jump_table_offset;

  struct MethodEntry {
    be_uint16_t offset; // Need to add 4 to this apparently
    be_uint16_t push_opcode;
    be_int16_t resource_id; // id of target CODE resource
    be_uint16_t trap_opcode; // Disassembles as `trap _LoadSeg`
  } __attribute__((packed));

  MethodEntry entries[0];
} __attribute__((packed));

struct CodeResourceHeader {
  be_uint16_t first_jump_table_entry;
  be_uint16_t num_jump_table_entries;
} __attribute__((packed));

struct CodeResourceFarHeader {
  be_uint16_t entry_offset; // 0xFFFF
  be_uint16_t unused; // 0x0000
  be_uint32_t near_entry_start_a5_offset;
  be_uint32_t near_entry_count;
  be_uint32_t far_entry_start_a5_offset;
  be_uint32_t far_entry_count;
  be_uint32_t a5_relocation_data_offset;
  be_uint32_t a5;
  be_uint32_t pc_relocation_data_offset;
  be_uint32_t load_address;
  be_uint32_t reserved; // 0x00000000
} __attribute__((packed));

vector<uint32_t> parse_relocation_data(StringReader& r) {
  // Note: we intentionally do not check r.eof here, since the format has an
  // explicit end command
  vector<uint32_t> ret;
  uint32_t offset = 0;
  for (;;) {
    uint8_t a = r.get_u8();
    if (a == 0) {
      a = r.get_u8();
      if (a == 0) {
        return ret;
      } else if (a & 0x80) {
        offset += (static_cast<uint32_t>(a & 0x7F) << 25) | (r.get_u24b() << 1);
      } else {
        throw runtime_error("invalid relocation command (0001-007F)");
      }
    } else if (a & 0x80) {
      offset += (static_cast<uint16_t>(a & 0x7F) << 9) | (r.get_u8() << 1);
    } else {
      offset += (a << 1);
    }
    ret.push_back(offset);
  }
}

ResourceFile::DecodedCode0Resource ResourceFile::decode_CODE_0(int16_t id, uint32_t type) {
  return this->decode_CODE_0(this->get_resource(type, id));
}

ResourceFile::DecodedCode0Resource ResourceFile::decode_CODE_0(shared_ptr<const Resource> res) {
  return ResourceFile::decode_CODE_0(res->data.data(), res->data.size());
}

ResourceFile::DecodedCode0Resource ResourceFile::decode_CODE_0(
    const void* vdata, size_t size) {
  StringReader r(vdata, size);
  const auto& header = r.get<Code0ResourceHeader>();

  DecodedCode0Resource ret;
  ret.above_a5_size = header.above_a5_size;
  ret.below_a5_size = header.below_a5_size;

  // Some apps have what looks like a compressed jump table - it has a single
  // entry (usually pointing to the beginning of the last CODE resource),
  // followed by obviously non-jump-table data. Since this data often has a size
  // that isn't a multiple of 8, we can't just check r.eof() here.
  while (r.remaining() >= sizeof(Code0ResourceHeader::MethodEntry)) {
    const auto& e = r.get<Code0ResourceHeader::MethodEntry>();
    if (e.push_opcode != 0x3F3C || e.trap_opcode != 0xA9F0) {
      ret.jump_table.push_back({0, 0});
    } else {
      ret.jump_table.push_back({e.resource_id, e.offset});
    }
  }

  return ret;
}

ResourceFile::DecodedCodeResource ResourceFile::decode_CODE(int16_t id, uint32_t type) {
  return this->decode_CODE(this->get_resource(type, id));
}

ResourceFile::DecodedCodeResource ResourceFile::decode_CODE(shared_ptr<const Resource> res) {
  return ResourceFile::decode_CODE(res->data.data(), res->data.size());
}

ResourceFile::DecodedCodeResource ResourceFile::decode_CODE(
    const void* vdata, size_t size) {
  StringReader r(vdata, size);

  const auto& header = r.get<CodeResourceHeader>(false);

  DecodedCodeResource ret;
  if ((header.first_jump_table_entry == 0xFFFF) &&
      (header.num_jump_table_entries == 0x0000)) {
    const auto& far_header = r.get<CodeResourceFarHeader>();

    ret.first_jump_table_entry = -1;
    ret.num_jump_table_entries = 0;
    ret.near_entry_start_a5_offset = far_header.near_entry_start_a5_offset;
    ret.near_entry_count = far_header.near_entry_count;
    ret.far_entry_start_a5_offset = far_header.far_entry_start_a5_offset;
    ret.far_entry_count = far_header.far_entry_count;
    ret.a5_relocation_data_offset = far_header.a5_relocation_data_offset;
    ret.a5 = far_header.a5;
    ret.pc_relocation_data_offset = far_header.pc_relocation_data_offset;
    ret.load_address = far_header.load_address;

    {
      auto sub_r = r.sub(ret.a5_relocation_data_offset);
      ret.a5_relocation_addresses = parse_relocation_data(sub_r);
    }
    {
      auto sub_r = r.sub(ret.pc_relocation_data_offset);
      ret.pc_relocation_addresses = parse_relocation_data(sub_r);
    }

  } else {
    r.skip(sizeof(CodeResourceHeader));
    ret.first_jump_table_entry = header.first_jump_table_entry;
    ret.num_jump_table_entries = header.num_jump_table_entries;
  }

  ret.code = r.read(r.remaining());
  return ret;
}

struct DriverResourceHeader {
  be_uint16_t flags;
  be_uint16_t delay;
  be_uint16_t event_mask;
  be_int16_t menu_id;
  be_uint16_t open_label;
  be_uint16_t prime_label;
  be_uint16_t control_label;
  be_uint16_t status_label;
  be_uint16_t close_label;
} __attribute__((packed));

ResourceFile::DecodedDriverResource ResourceFile::decode_DRVR(int16_t id, uint32_t type) {
  return this->decode_DRVR(this->get_resource(type, id));
}

ResourceFile::DecodedDriverResource ResourceFile::decode_DRVR(shared_ptr<const Resource> res) {
  return ResourceFile::decode_DRVR(res->data.data(), res->data.size());
}

ResourceFile::DecodedDriverResource ResourceFile::decode_DRVR(
    const void* data, size_t size) {
  StringReader r(data, size);

  const auto& header = r.get<DriverResourceHeader>();
  string name = r.readx(r.get_u8());

  // Code starts at the next word-aligned boundary after the name
  if (r.where() & 1) {
    r.skip(1);
  }
  size_t code_start_offset = r.where();

  auto handle_label = +[](int32_t* dest, uint16_t src, size_t code_start_offset, const char* name) {
    if (src == 0) {
      *dest = -1;
    } else if (src < code_start_offset) {
      throw runtime_error(string(name) + " label is before code start");
    } else {
      *dest = src - code_start_offset;
    }
  };

  DecodedDriverResource ret;
  ret.flags = header.flags;
  ret.delay = header.delay;
  ret.event_mask = header.event_mask;
  ret.menu_id = header.menu_id;
  handle_label(&ret.open_label, header.open_label, code_start_offset, "open");
  handle_label(&ret.prime_label, header.prime_label, code_start_offset, "prime");
  handle_label(&ret.control_label, header.control_label, code_start_offset, "control");
  handle_label(&ret.status_label, header.status_label, code_start_offset, "status");
  handle_label(&ret.close_label, header.close_label, code_start_offset, "close");
  ret.name = move(name);
  ret.code = r.read(r.remaining());
  return ret;
}

ResourceFile::DecodedDecompressorResource ResourceFile::decode_dcmp(int16_t id, uint32_t type) {
  return this->decode_dcmp(this->get_resource(type, id));
}

ResourceFile::DecodedDecompressorResource ResourceFile::decode_dcmp(shared_ptr<const Resource> res) {
  return ResourceFile::decode_dcmp(res->data.data(), res->data.size());
}

ResourceFile::DecodedDecompressorResource ResourceFile::decode_dcmp(const void* vdata, size_t size) {
  StringReader r(vdata, size);

  DecodedDecompressorResource ret;
  if ((r.pget_u8(0) == 0x60) && (r.pget_u32b(4) == RESOURCE_TYPE_dcmp)) {
    ret.init_label = -1;
    ret.decompress_label = 0;
    ret.exit_label = -1;
    ret.pc_offset = 0;
  } else {
    ret.init_label = r.get_u16b();
    ret.decompress_label = r.get_u16b();
    ret.exit_label = r.get_u16b();
    ret.pc_offset = 6;
  }
  ret.code = r.read(r.remaining());
  return ret;
}

PEFFile ResourceFile::decode_pef(int16_t id, uint32_t type) {
  return this->decode_pef(this->get_resource(type, id));
}

PEFFile ResourceFile::decode_pef(shared_ptr<const Resource> res) {
  return ResourceFile::decode_pef(res->data.data(), res->data.size());
}

PEFFile ResourceFile::decode_pef(const void* data, size_t size) {
  return PEFFile("__unnamed__", data, size);
}

ResourceFile::DecodedPEFDriver ResourceFile::decode_expt(int16_t id, uint32_t type) {
  return this->decode_expt(this->get_resource(type, id));
}

ResourceFile::DecodedPEFDriver ResourceFile::decode_expt(shared_ptr<const Resource> res) {
  return ResourceFile::decode_expt(res->data.data(), res->data.size());
}

ResourceFile::DecodedPEFDriver ResourceFile::decode_expt(const void* data, size_t size) {
  StringReader r(data, size);
  // TODO: Figure out the format (and actual size) of this header and parse it
  string header_contents = r.read(0x20);
  size_t pef_size = r.remaining();
  return {move(header_contents),
          PEFFile("__unnamed__", &r.get<char>(true, pef_size), pef_size)};
}

ResourceFile::DecodedPEFDriver ResourceFile::decode_nsrd(int16_t id, uint32_t type) {
  return this->decode_nsrd(this->get_resource(type, id));
}

ResourceFile::DecodedPEFDriver ResourceFile::decode_nsrd(shared_ptr<const Resource> res) {
  return ResourceFile::decode_nsrd(res->data.data(), res->data.size());
}

ResourceFile::DecodedPEFDriver ResourceFile::decode_nsrd(const void* data, size_t size) {
  StringReader r(data, size);
  // TODO: Figure out the format (and actual size) of this header and parse it
  string header_contents = r.read(0x20);
  size_t pef_size = r.remaining();
  return {move(header_contents),
          PEFFile("__unnamed__", &r.get<char>(true, pef_size), pef_size)};
}



////////////////////////////////////////////////////////////////////////////////
// Image resource decoding

struct ColorIconResourceHeader {
  // pixMap fields
  be_uint32_t pix_map_unused;
  PixelMapHeader pix_map;

  // mask bitmap fields
  be_uint32_t mask_unused;
  BitMapHeader mask_header;

  // 1-bit icon bitmap fields
  be_uint32_t bitmap_unused;
  BitMapHeader bitmap_header;

  // icon data fields
  be_uint32_t icon_data; // ignored
} __attribute__((packed));

ResourceFile::DecodedColorIconResource ResourceFile::decode_cicn(int16_t id, uint32_t type) {
  return this->decode_cicn(this->get_resource(type, id));
}

ResourceFile::DecodedColorIconResource ResourceFile::decode_cicn(shared_ptr<const Resource> res) {
  return ResourceFile::decode_cicn(res->data.data(), res->data.size());
}

ResourceFile::DecodedColorIconResource ResourceFile::decode_cicn(const void* vdata, size_t size) {
  StringReader r(vdata, size);
  const auto& header = r.get<ColorIconResourceHeader>();

  // The mask is required, but the bitmap may be missing
  if ((header.pix_map.bounds.width() != header.mask_header.bounds.width()) ||
      (header.pix_map.bounds.height() != header.mask_header.bounds.height())) {
    throw runtime_error("mask dimensions don\'t match icon dimensions");
  }
  if (header.bitmap_header.flags_row_bytes &&
      ((header.pix_map.bounds.width() != header.mask_header.bounds.width()) ||
       (header.pix_map.bounds.height() != header.mask_header.bounds.height()))) {
    throw runtime_error("bitmap dimensions don\'t match icon dimensions");
  }
  if ((header.pix_map.pixel_size != 8) && (header.pix_map.pixel_size != 4) &&
      (header.pix_map.pixel_size != 2) && (header.pix_map.pixel_size != 1)) {
    throw runtime_error("pixel bit depth is not 1, 2, 4, or 8");
  }

  size_t mask_map_size = PixelMapData::size(
      header.mask_header.flags_row_bytes, header.mask_header.bounds.height());
  const auto& mask_map = r.get<PixelMapData>(true, mask_map_size);

  size_t bitmap_size = PixelMapData::size(
      header.bitmap_header.flags_row_bytes, header.bitmap_header.bounds.height());
  const auto& bitmap = r.get<PixelMapData>(true, bitmap_size);

  // We can't know the color table's size until we've read the header, hence
  // this non-advancing get() followed by a size-verridden get()
  const auto& ctable = r.get<ColorTable>(false);
  if (ctable.num_entries < 0) {
    throw runtime_error("color table has negative size");
  }
  r.get<ColorTable>(true, ctable.size());

  // Decode the image data
  size_t pixel_map_size = PixelMapData::size(
      header.pix_map.flags_row_bytes & 0x3FFF, header.pix_map.bounds.height());
  const auto& pixel_map = r.get<PixelMapData>(true, pixel_map_size);

  Image img = decode_color_image(header.pix_map, pixel_map, &ctable, &mask_map,
      header.mask_header.flags_row_bytes);

  // Decode the mask and bitmap
  Image bitmap_img(
      header.bitmap_header.flags_row_bytes ? header.bitmap_header.bounds.width() : 0,
      header.bitmap_header.flags_row_bytes ? header.bitmap_header.bounds.height() : 0,
      true);
  for (ssize_t y = 0; y < header.pix_map.bounds.height(); y++) {
    for (ssize_t x = 0; x < header.pix_map.bounds.width(); x++) {
      uint8_t alpha = mask_map.lookup_entry(1, header.mask_header.flags_row_bytes, x, y) ? 0xFF : 0x00;

      if (header.bitmap_header.flags_row_bytes) {
        if (bitmap.lookup_entry(1, header.bitmap_header.flags_row_bytes, x, y)) {
          bitmap_img.write_pixel(x, y, 0x00, 0x00, 0x00, alpha);
        } else {
          bitmap_img.write_pixel(x, y, 0xFF, 0xFF, 0xFF, alpha);
        }
      }
    }
  }

  return {.image = move(img), .bitmap = move(bitmap_img)};
}



struct ColorCursorResourceHeader {
  be_uint16_t type; // 0x8000 (monochrome) or 0x8001 (color)
  be_uint32_t pixel_map_offset; // offset from beginning of resource data
  be_uint32_t pixel_data_offset; // offset from beginning of resource data
  be_uint32_t expanded_data; // ignore this (Color QuickDraw stuff)
  be_uint16_t expanded_depth;
  be_uint32_t unused;
  uint8_t bitmap[0x20];
  uint8_t mask[0x20];
  be_uint16_t hotspot_x;
  be_uint16_t hotspot_y;
  be_uint32_t color_table_offset; // offset from beginning of resource
  be_uint32_t cursor_id; // ignore this (resource id)
} __attribute__((packed));

ResourceFile::DecodedColorCursorResource ResourceFile::decode_crsr(int16_t id, uint32_t type) {
  return this->decode_crsr(this->get_resource(type, id));
}

ResourceFile::DecodedColorCursorResource ResourceFile::decode_crsr(shared_ptr<const Resource> res) {
  return ResourceFile::decode_crsr(res->data.data(), res->data.size());
}

ResourceFile::DecodedColorCursorResource ResourceFile::decode_crsr(const void* vdata, size_t size) {
  StringReader r(vdata, size);

  const auto& header = r.get<ColorCursorResourceHeader>();
  if ((header.type & 0xFFFE) != 0x8000) {
    throw runtime_error("unknown crsr type");
  }

  Image bitmap = decode_monochrome_image_masked(&header.bitmap, 0x40, 16, 16);

  const auto& pixmap_header = r.pget<PixelMapHeader>(header.pixel_map_offset + 4);

  size_t pixel_map_size = PixelMapData::size(
      pixmap_header.flags_row_bytes & 0x3FFF, pixmap_header.bounds.height());
  const auto& pixmap_data = r.pget<PixelMapData>(
      header.pixel_data_offset, pixel_map_size);

  const auto& ctable = r.pget<ColorTable>(pixmap_header.color_table_offset);
  if (ctable.num_entries & 0x8000) {
    throw runtime_error("color table has negative size");
  }
  r.pget<ColorTable>(pixmap_header.color_table_offset, ctable.size());

  // Decode the color image
  Image img = replace_image_channel(
      decode_color_image(pixmap_header, pixmap_data, &ctable), 3, bitmap, 3);

  return {
      .image = move(img),
      .bitmap = move(bitmap),
      .hotspot_x = header.hotspot_x,
      .hotspot_y = header.hotspot_y};
}



struct PixelPatternResourceHeader {
  be_uint16_t type;
  be_uint32_t pixel_map_offset;
  be_uint32_t pixel_data_offset;
  be_uint32_t unused1; // TMPL: "Expanded pixel image" (probably ptr to decompressed data when used by QuickDraw)
  be_uint16_t unused2; // TMPL: "Pattern valid flag" (unused in stored resource)
  be_uint32_t reserved; // TMPL: "Expanded pattern"
  uint8_t monochrome_pattern[8];
} __attribute__((packed));

static ResourceFile::DecodedPattern decode_ppat_data(StringReader& r) {
  const auto& header = r.get<PixelPatternResourceHeader>();

  Image monochrome_pattern = decode_monochrome_image(header.monochrome_pattern,
      8, 8, 8);

  // Type 0 is monochrome; type 1 is indexed color; type 2 is apparently RGB
  // color, but it's not clear if these are ever stored in resources or only
  // used when loaded in memory
  if ((header.type == 0) || (header.type == 2)) {
    return {monochrome_pattern, monochrome_pattern};
  }
  if ((header.type != 1) && (header.type != 3)) {
    throw runtime_error("unknown ppat type");
  }

  // Get the pixel map header
  const auto& pixmap_header = r.pget<PixelMapHeader>(
      header.pixel_map_offset + 4);

  // Get the pixel map data
  size_t pixel_map_size = PixelMapData::size(
      pixmap_header.flags_row_bytes & 0x3FFF, pixmap_header.bounds.height());
  const auto& pixmap_data = r.pget<PixelMapData>(header.pixel_data_offset,
      pixel_map_size);

  // Get the color table
  const auto& ctable = r.pget<ColorTable>(pixmap_header.color_table_offset);
  if (ctable.num_entries < 0) {
    throw runtime_error("color table has negative size");
  }
  // We can't know the color table size until we have the struct, so pget it
  // again with the correct size to check that it's all within bounds
  r.pget<ColorTable>(pixmap_header.color_table_offset, ctable.size());

  // Decode the color image
  Image pattern = decode_color_image(pixmap_header, pixmap_data, &ctable);

  return {move(pattern), move(monochrome_pattern)};
}

ResourceFile::DecodedPattern ResourceFile::decode_ppat(int16_t id, uint32_t type) {
  return this->decode_ppat(this->get_resource(type, id));
}

ResourceFile::DecodedPattern ResourceFile::decode_ppat(shared_ptr<const Resource> res) {
  return ResourceFile::decode_ppat(res->data.data(), res->data.size());
}

ResourceFile::DecodedPattern ResourceFile::decode_ppat(const void* data, size_t size) {
  StringReader r(data, size);
  return decode_ppat_data(r);
}

vector<ResourceFile::DecodedPattern> ResourceFile::decode_pptN(int16_t id, uint32_t type) {
  return this->decode_pptN(this->get_resource(type, id));
}

vector<ResourceFile::DecodedPattern> ResourceFile::decode_pptN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_pptN(res->data.data(), res->data.size());
}

vector<ResourceFile::DecodedPattern> ResourceFile::decode_pptN(const void* vdata, size_t size) {
  // These resources are composed of a 2-byte count field, then N 4-byte
  // offsets, then the ppat data

  StringReader r(vdata, size);

  uint16_t count = r.get_u16b();
  vector<uint32_t> offsets;
  while (offsets.size() < count) {
    offsets.emplace_back(r.get_u32b());
  }
  offsets.emplace_back(size);

  vector<DecodedPattern> ret;
  for (size_t x = 0; x < count; x++) {
    StringReader sub_r = r.subx(offsets[x], offsets[x + 1] - offsets[x]);
    ret.emplace_back(decode_ppat_data(sub_r));
  }
  return ret;
}

Image ResourceFile::decode_PAT(int16_t id, uint32_t type) {
  return this->decode_PAT(this->get_resource(type, id));
}

Image ResourceFile::decode_PAT(shared_ptr<const Resource> res) {
  return ResourceFile::decode_PAT(res->data.data(), res->data.size());
}

Image ResourceFile::decode_PAT(const void* data, size_t size) {
  if (size != 8) {
    throw runtime_error("PAT not exactly 8 bytes in size");
  }
  return decode_monochrome_image(data, size, 8, 8);
}

vector<Image> ResourceFile::decode_PATN(int16_t id, uint32_t type) {
  return this->decode_PATN(this->get_resource(type, id));
}

vector<Image> ResourceFile::decode_PATN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_PATN(res->data.data(), res->data.size());
}

vector<Image> ResourceFile::decode_PATN(const void* vdata, size_t size) {
  StringReader r(vdata, size);
  uint16_t num_patterns = r.get_u16b();

  vector<Image> ret;
  while (ret.size() < num_patterns) {
    ret.emplace_back(decode_monochrome_image(&r.get<uint64_t>(), 8, 8, 8));
  }
  return ret;
}

vector<Image> ResourceFile::decode_SICN(int16_t id, uint32_t type) {
  return this->decode_SICN(this->get_resource(type, id));
}

vector<Image> ResourceFile::decode_SICN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_SICN(res->data.data(), res->data.size());
}

vector<Image> ResourceFile::decode_SICN(const void* vdata, size_t size) {
  // So simple, there isn't even a header struct! SICN resources are just
  // several 0x20-byte monochrome images concatenated together.

  if (size & 0x1F) {
    throw runtime_error("SICN size not a multiple of 32");
  }

  StringReader r(vdata, size);
  vector<Image> ret;
  while (!r.eof()) {
    ret.emplace_back(decode_monochrome_image(
        &r.get<uint8_t>(true, 0x20), 0x20, 16, 16));
  }
  return ret;
}

static Image apply_mask_from_parallel_icon_list(
  std::function<ResourceFile::DecodedIconListResource()> decode_list,
  Image color_image
) {
  try {
    auto decoded_mask = decode_list();
    if (decoded_mask.composite.empty()) {
      throw runtime_error("corresponding mask resource is not a 2-icon list");
    }
    return replace_image_channel(color_image, 3, decoded_mask.composite, 3);
  } catch (const exception&) {
    return color_image;
  }
}

Image ResourceFile::decode_ics8(int16_t id, uint32_t type) {
  return this->decode_ics8(this->get_resource(type, id));
}

Image ResourceFile::decode_ics8(shared_ptr<const Resource> res) {
  Image decoded = this->decode_ics8_without_alpha(res->data.data(), res->data.size());
  uint32_t mask_type = (res->type & 0xFFFFFF00) | '#';
  return apply_mask_from_parallel_icon_list(
      [&]() { return this->decode_icsN(res->id, mask_type); }, decoded);
}

Image ResourceFile::decode_ics8_without_alpha(const void* data, size_t size) {
  return decode_8bit_image(data, size, 16, 16);
}

Image ResourceFile::decode_kcs8(int16_t id, uint32_t type) {
  return this->decode_kcs8(this->get_resource(type, id));
}

Image ResourceFile::decode_kcs8(shared_ptr<const Resource> res) {
  return this->decode_ics8(res);
}

Image ResourceFile::decode_kcs8_without_alpha(const void* data, size_t size) {
  return ResourceFile::decode_ics8_without_alpha(data, size);
}

Image ResourceFile::decode_icl8(int16_t id, uint32_t type) {
  return this->decode_icl8(this->get_resource(type, id));
}

Image ResourceFile::decode_icl8(shared_ptr<const Resource> res) {
  Image decoded = this->decode_icl8_without_alpha(res->data.data(), res->data.size());
  return apply_mask_from_parallel_icon_list(
      [&]() { return this->decode_ICNN(res->id, RESOURCE_TYPE_ICNN); },
      decoded);
}

Image ResourceFile::decode_icl8_without_alpha(const void* data, size_t size) {
  return decode_8bit_image(data, size, 32, 32);
}

Image ResourceFile::decode_icm8(int16_t id, uint32_t type) {
  return this->decode_icm8(this->get_resource(type, id));
}

Image ResourceFile::decode_icm8(shared_ptr<const Resource> res) {
  Image decoded = this->decode_icm8_without_alpha(res->data.data(), res->data.size());
  return apply_mask_from_parallel_icon_list(
      [&]() { return this->decode_icmN(res->id, RESOURCE_TYPE_icmN); },
      decoded);
}

Image ResourceFile::decode_icm8_without_alpha(const void* data, size_t size) {
  return decode_8bit_image(data, size, 16, 12);
}

Image ResourceFile::decode_ics4(int16_t id, uint32_t type) {
  return this->decode_ics4(this->get_resource(type, id));
}

Image ResourceFile::decode_ics4(shared_ptr<const Resource> res) {
  Image decoded = this->decode_ics4_without_alpha(res->data.data(), res->data.size());
  uint32_t mask_type = (res->type & 0xFFFFFF00) | '#';
  return apply_mask_from_parallel_icon_list(
      [&]() { return this->decode_icsN(res->id, mask_type); },
      decoded);
}

Image ResourceFile::decode_ics4_without_alpha(const void* data, size_t size) {
  return decode_4bit_image(data, size, 16, 16);
}

Image ResourceFile::decode_kcs4(int16_t id, uint32_t type) {
  return this->decode_kcs4(this->get_resource(type, id));
}

Image ResourceFile::decode_kcs4(shared_ptr<const Resource> res) {
  return this->decode_ics4(res);
}

Image ResourceFile::decode_kcs4_without_alpha(const void* data, size_t size) {
  return ResourceFile::decode_ics4_without_alpha(data, size);
}

Image ResourceFile::decode_icl4(int16_t id, uint32_t type) {
  return this->decode_icl4(this->get_resource(type, id));
}

Image ResourceFile::decode_icl4(shared_ptr<const Resource> res) {
  Image decoded = this->decode_icl4_without_alpha(res->data.data(), res->data.size());
  return apply_mask_from_parallel_icon_list(
      [&]() { return this->decode_ICNN(res->id, RESOURCE_TYPE_ICNN); },
      decoded);
}

Image ResourceFile::decode_icl4_without_alpha(const void* data, size_t size) {
  return decode_4bit_image(data, size, 32, 32);
}

Image ResourceFile::decode_icm4(int16_t id, uint32_t type) {
  return this->decode_icm4(this->get_resource(type, id));
}

Image ResourceFile::decode_icm4(shared_ptr<const Resource> res) {
  Image decoded = this->decode_icm4_without_alpha(res->data.data(), res->data.size());
  return apply_mask_from_parallel_icon_list(
      [&]() { return this->decode_icmN(res->id, RESOURCE_TYPE_icmN); },
      decoded);
}

Image ResourceFile::decode_icm4_without_alpha(const void* data, size_t size) {
  return decode_4bit_image(data, size, 16, 12);
}

Image ResourceFile::decode_ICON(int16_t id, uint32_t type) {
  return this->decode_ICON(this->get_resource(type, id));
}

Image ResourceFile::decode_ICON(shared_ptr<const Resource> res) {
  return ResourceFile::decode_ICON(res->data.data(), res->data.size());
}

Image ResourceFile::decode_ICON(const void* data, size_t size) {
  return decode_monochrome_image(data, size, 32, 32);
}

ResourceFile::DecodedCursorResource ResourceFile::decode_CURS(int16_t id, uint32_t type) {
  return this->decode_CURS(this->get_resource(type, id));
}

ResourceFile::DecodedCursorResource ResourceFile::decode_CURS(shared_ptr<const Resource> res) {
  return ResourceFile::decode_CURS(res->data.data(), res->data.size());
}

ResourceFile::DecodedCursorResource ResourceFile::decode_CURS(const void* vdata, size_t size) {
  StringReader r(vdata, size);
  const uint8_t* bitmap_and_mask = &r.get<uint8_t>(true, 0x40);
  uint16_t hotspot_x = r.get_u16b();
  uint16_t hotspot_y = r.get_u16b();

  Image img = decode_monochrome_image_masked(bitmap_and_mask, 0x40, 16, 16);
  return {.bitmap = move(img), .hotspot_x = hotspot_x, .hotspot_y = hotspot_y};
}

static ResourceFile::DecodedIconListResource decode_monochrome_image_list(
    const void* data, size_t size, size_t w, size_t h) {
  if (w & 7) {
    throw logic_error("monochrome icons must be a multiple of 8 pixels wide");
  }

  size_t image_bytes = (w >> 3) * h;
  if (size % image_bytes) {
    throw runtime_error("data size is not a multiple of image_bytes");
  }
  size_t num_icons = size / image_bytes;

  if (num_icons == 2) {
    return {
        .composite = decode_monochrome_image_masked(data, size, w, h),
        .images = vector<Image>()};
  } else {
    vector<Image> ret;
    while (ret.size() < num_icons) {
      ret.emplace_back(decode_monochrome_image(data, image_bytes, w, h));
      data = reinterpret_cast<const uint8_t*>(data) + image_bytes;
    }
    return {.composite = Image(), .images = move(ret)};
  }
}

ResourceFile::DecodedIconListResource ResourceFile::decode_ICNN(
    int16_t id, uint32_t type) {
  return this->decode_ICNN(this->get_resource(type, id));
}

ResourceFile::DecodedIconListResource ResourceFile::decode_ICNN(
    shared_ptr<const Resource> res) {
  return ResourceFile::decode_ICNN(res->data.data(), res->data.size());
}

ResourceFile::DecodedIconListResource ResourceFile::decode_ICNN(
    const void* data, size_t size) {
  return decode_monochrome_image_list(data, size, 32, 32);
}

ResourceFile::DecodedIconListResource ResourceFile::decode_icsN(int16_t id, uint32_t type) {
  return this->decode_icsN(this->get_resource(type, id));
}

ResourceFile::DecodedIconListResource ResourceFile::decode_icsN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_icsN(res->data.data(), res->data.size());
}

ResourceFile::DecodedIconListResource ResourceFile::decode_icsN(const void* data, size_t size) {
  return decode_monochrome_image_list(data, size, 16, 16);
}

ResourceFile::DecodedIconListResource ResourceFile::decode_kcsN(int16_t id, uint32_t type) {
  return this->decode_kcsN(this->get_resource(type, id));
}

ResourceFile::DecodedIconListResource ResourceFile::decode_kcsN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_kcsN(res->data.data(), res->data.size());
}

ResourceFile::DecodedIconListResource ResourceFile::decode_kcsN(const void* data, size_t size) {
  return ResourceFile::decode_icsN(data, size);
}

ResourceFile::DecodedIconListResource ResourceFile::decode_icmN(int16_t id, uint32_t type) {
  return this->decode_icmN(this->get_resource(type, id));
}

ResourceFile::DecodedIconListResource ResourceFile::decode_icmN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_icmN(res->data.data(), res->data.size());
}

ResourceFile::DecodedIconListResource ResourceFile::decode_icmN(const void* data, size_t size) {
  return decode_monochrome_image_list(data, size, 16, 12);
}

ResourceFile::DecodedIconImagesResource::DecodedIconImagesResource()
  : icon_composer_version(0.0) { }

ResourceFile::DecodedIconImagesResource ResourceFile::decode_icns(int16_t id, uint32_t type) {
  return this->decode_icns(this->get_resource(type, id));
}

ResourceFile::DecodedIconImagesResource ResourceFile::decode_icns(shared_ptr<const Resource> res) {
  return ResourceFile::decode_icns(res->data.data(), res->data.size());
}

static std::string decompress_packed_icns_data(const void* data, size_t size) {
  StringWriter w;
  StringReader r(data, size);
  while (!r.eof()) {
    uint16_t cmd = r.get_u8();
    if (cmd < 0x80) {
      w.write(r.getv(cmd + 1), cmd + 1);
    } else {
      size_t target_size = w.size() + (cmd - 0x80 + 3);
      uint8_t v = r.get_u8();
      while (w.size() < target_size) {
        w.put_u8(v);
      }
    }
  }
  return move(w.str());
}

static Image decode_icns_packed_rgb_argb(
    const void* data, size_t size, size_t w, size_t h, bool has_alpha) {
  Image ret(w, h, has_alpha);

  // It appears that some applications support uncompressed data in these
  // formats. It's not clear how the decoder should determine whether the input
  // is compressed or not... the logical heuristic (which we implement here) is
  // that if the data is smaller than it should be, then it's compressed.
  // However, it appears that even in the has_alpha=false case, the alpha field
  // is still present in the uncompressed data, but is unused... so we compare
  // the size as if the image was ARGB even if it's not.
  size_t pixel_count = w * h;
  if (size >= (pixel_count * 4)) {
    // In the uncompressed case, the channels aren't tightly packed; instead,
    // each pixel's values are stored next to each other in (A|X)RGB order.
    StringReader r(data, size);
    for (size_t y = 0; y < h; y++) {
      for (size_t x = 0; x < w; x++) {
        uint8_t pixel_a = r.get_u8();
        uint8_t pixel_r = r.get_u8();
        uint8_t pixel_g = r.get_u8();
        uint8_t pixel_b = r.get_u8();
        ret.write_pixel(x, y, pixel_r, pixel_g, pixel_b, has_alpha ? pixel_a : 0xFF);
      }
    }

  } else {
    string decompressed_data = decompress_packed_icns_data(data, size);
    if (decompressed_data.size() < pixel_count * (has_alpha ? 4 : 3)) {
      throw runtime_error("not enough decompressed data");
    }

    StringReader ar(decompressed_data.data(), pixel_count);
    StringReader rr(decompressed_data.data() + pixel_count * (has_alpha ? 1 : 0), pixel_count);
    StringReader gr(decompressed_data.data() + pixel_count * (has_alpha ? 2 : 1), pixel_count);
    StringReader br(decompressed_data.data() + pixel_count * (has_alpha ? 3 : 2), pixel_count);
    for (size_t y = 0; y < h; y++) {
      for (size_t x = 0; x < w; x++) {
        ret.write_pixel(x, y, rr.get_u8(), gr.get_u8(), br.get_u8(), has_alpha ? ar.get_u8() : 0xFF);
      }
    }
  }

  return ret;
}

static void add_jpeg2000_png_or_direct_color(
    ResourceFile::DecodedIconImagesResource& ret,
    uint32_t sec_type,
    const void* sec_data,
    size_t sec_size,
    size_t direct_w = 0,
    size_t direct_h = 0,
    bool direct_has_alpha = false) {
  if (sec_size < 8) {
    throw runtime_error("JPEG2000/PNG/direct color section is too small");
  }
  // TODO: What is the "right" way to detect ARGB format? Here we just
  // assume JPEG2000 or PNG if the respective image formats' header bytes
  // appear, which should work in essentially all cases.
  if (!memcmp(sec_data, "\0\0\0\x0CjP  ", 8) || !memcmp(sec_data, "\0\0\0\x0CjP2 ", 8)) {
    string data(reinterpret_cast<const char*>(sec_data), sec_size);
    ret.type_to_jpeg2000_data.emplace(sec_type, move(data));
  } else if (!memcmp(sec_data, "\x89PNG\r\n\x1A\n", 8)) {
    string data(reinterpret_cast<const char*>(sec_data), sec_size);
    ret.type_to_png_data.emplace(sec_type, move(data));
  } else if (direct_w && direct_h) {
    ret.type_to_image.emplace(sec_type, decode_icns_packed_rgb_argb(
        sec_data, sec_size, direct_w, direct_h, direct_has_alpha));
  } else {
    print_data(stderr, sec_data, sec_size);
    throw runtime_error("icns subfield is not PNG, JPEG2000, or packed direct color: " + string_for_resource_type(sec_type));
  }
}

ResourceFile::DecodedIconImagesResource ResourceFile::decode_icns(const void* data, size_t size) {
  DecodedIconImagesResource ret;
  StringReader r(data, size);
  if (r.get_u32b() != RESOURCE_TYPE_icns) {
    throw runtime_error("resource does not begin with icns tag");
  }
  if (r.get_u32b() > size) {
    throw runtime_error("resource size field is incorrect");
  }



  while (r.where() < size) {
    uint32_t sec_type = r.get_u32b();
    uint32_t sec_size = r.get_u32b() - 8;
    const void* sec_data = r.getv(sec_size);
    try {
      switch (sec_type) {
        // Fixed-size monochrome images
        case RESOURCE_TYPE_ICON: // 32x32 no alpha
          ret.type_to_image.emplace(sec_type, ResourceFile::decode_ICON(sec_data, sec_size));
          break;
        case RESOURCE_TYPE_icmN: { // 16x12 with mask
          auto decoded = ResourceFile::decode_icmN(sec_data, sec_size);
          if (decoded.composite.empty()) {
            throw runtime_error("icm# subfield is not a 2-icon list");
          }
          ret.type_to_image.emplace(sec_type, decoded.composite);
          break;
        }
        case RESOURCE_TYPE_icsN: { // 16x16 with mask
          auto decoded = ResourceFile::decode_icsN(sec_data, sec_size);
          if (decoded.composite.empty()) {
            throw runtime_error("ics# subfield is not a 2-icon list");
          }
          ret.type_to_image.emplace(sec_type, decoded.composite);
          break;
        }
        case RESOURCE_TYPE_ICNN: { // 32x32 with mask
          auto decoded = ResourceFile::decode_ICNN(sec_data, sec_size);
          if (decoded.composite.empty()) {
            throw runtime_error("ICN# subfield is not a 2-icon list");
          }
          ret.type_to_image.emplace(sec_type, decoded.composite);
          break;
        }
        case RESOURCE_TYPE_ichN: { // 48x48 with mask
          auto decoded = decode_monochrome_image_list(sec_data, sec_size, 48, 48);
          if (decoded.composite.empty()) {
            throw runtime_error("ICN# subfield is not a 2-icon list");
          }
          ret.type_to_image.emplace(sec_type, decoded.composite);
          break;
        }

        // Fixed-size 4-bit images
        case RESOURCE_TYPE_icm4: // 16x12
          ret.type_to_image.emplace(sec_type, ResourceFile::decode_icm4_without_alpha(sec_data, sec_size));
          break;
        case RESOURCE_TYPE_ics4: // 16x16
          ret.type_to_image.emplace(sec_type, ResourceFile::decode_ics4_without_alpha(sec_data, sec_size));
          break;
        case RESOURCE_TYPE_icl4: // 32x32
          ret.type_to_image.emplace(sec_type, ResourceFile::decode_icl4_without_alpha(sec_data, sec_size));
          break;
        case RESOURCE_TYPE_ich4: // 48x48
          ret.type_to_image.emplace(sec_type, decode_4bit_image(sec_data, sec_size, 48, 48));
          break;

        // Fixed-size 8-bit images
        case RESOURCE_TYPE_icm8: // 16x12
          ret.type_to_image.emplace(sec_type, ResourceFile::decode_icm8_without_alpha(sec_data, sec_size));
          break;
        case RESOURCE_TYPE_ics8: // 16x16
          ret.type_to_image.emplace(sec_type, ResourceFile::decode_ics8_without_alpha(sec_data, sec_size));
          break;
        case RESOURCE_TYPE_icl8: // 32x32
          ret.type_to_image.emplace(sec_type, ResourceFile::decode_icl8_without_alpha(sec_data, sec_size));
          break;
        case RESOURCE_TYPE_ich8: // 48x48
          ret.type_to_image.emplace(sec_type, decode_8bit_image(sec_data, sec_size, 48, 48));
          break;

        // Fixed-size 8-bit alpha-only images
        case RESOURCE_TYPE_s8mk:
          ret.type_to_image.emplace(sec_type, decode_8bit_image(sec_data, sec_size, 16, 16, nullptr));
          break;
        case RESOURCE_TYPE_l8mk:
          ret.type_to_image.emplace(sec_type, decode_8bit_image(sec_data, sec_size, 32, 32, nullptr));
          break;
        case RESOURCE_TYPE_h8mk:
          ret.type_to_image.emplace(sec_type, decode_8bit_image(sec_data, sec_size, 48, 48, nullptr));
          break;
        case RESOURCE_TYPE_t8mk:
          ret.type_to_image.emplace(sec_type, decode_8bit_image(sec_data, sec_size, 128, 128, nullptr));
          break;

        // Fixed-size 24-bit packed images
        case RESOURCE_TYPE_is32:
          ret.type_to_image.emplace(sec_type, decode_icns_packed_rgb_argb(sec_data, sec_size, 16, 16, false));
          break;
        case RESOURCE_TYPE_il32:
          ret.type_to_image.emplace(sec_type, decode_icns_packed_rgb_argb(sec_data, sec_size, 32, 32, false));
          break;
        case RESOURCE_TYPE_ih32:
          ret.type_to_image.emplace(sec_type, decode_icns_packed_rgb_argb(sec_data, sec_size, 48, 48, false));
          break;
        case RESOURCE_TYPE_it32:
          // Note: This type specifically includes an apparently-unused 4-byte
          // field before the actual data
          if (sec_size < 4) {
            throw runtime_error("it32 data is too small");
          }
          ret.type_to_image.emplace(sec_type, decode_icns_packed_rgb_argb(
              reinterpret_cast<const uint8_t*>(sec_data) + 4, sec_size - 4, 128, 128, false));
          break;

        case RESOURCE_TYPE_icp4: // 16x16 JPEG / PNG / packed RGB
          add_jpeg2000_png_or_direct_color(ret, sec_type, sec_data, sec_size, 16, 16, false);
          break;
        case RESOURCE_TYPE_icp5: // 32x32 JPEG / PNG / packed RGB
          add_jpeg2000_png_or_direct_color(ret, sec_type, sec_data, sec_size, 32, 32, false);
          break;

        case RESOURCE_TYPE_ic04: // 16x16 JPEG / PNG / packed ARGB
          add_jpeg2000_png_or_direct_color(ret, sec_type, sec_data, sec_size, 16, 16, true);
          break;
        case RESOURCE_TYPE_ic05: // 32x32 (16x16@2x) JPEG / PNG / packed ARGB
          add_jpeg2000_png_or_direct_color(ret, sec_type, sec_data, sec_size, 32, 32, true);
          break;
        case RESOURCE_TYPE_icsb: // 18x18 JPEG / PNG / packed ARGB
          add_jpeg2000_png_or_direct_color(ret, sec_type, sec_data, sec_size, 18, 18, true);
          break;
        case RESOURCE_TYPE_icsB: // 36x36 (18x18@2x) JPEG / PNG
          add_jpeg2000_png_or_direct_color(ret, sec_type, sec_data, sec_size, 36, 36, true);
          break;
        case RESOURCE_TYPE_sb24: // 24x24 JPEG / PNG / packed ARGB
          add_jpeg2000_png_or_direct_color(ret, sec_type, sec_data, sec_size, 24, 24, true);
          break;
        case RESOURCE_TYPE_SB24: // 48x48 (24x24@2x) JPEG / PNG / packed ARGB
          add_jpeg2000_png_or_direct_color(ret, sec_type, sec_data, sec_size, 48, 48, true);
          break;

        case RESOURCE_TYPE_icp6: // 48x48 JPEG / PNG
        case RESOURCE_TYPE_ic07: // 128x128 JPEG / PNG
        case RESOURCE_TYPE_ic08: // 256x256 JPEG / PNG
        case RESOURCE_TYPE_ic09: // 512x512 JPEG / PNG
        case RESOURCE_TYPE_ic10: // 1024x1024 (512x512@2x) JPEG / PNG
        case RESOURCE_TYPE_ic11: // 32x32 (16x16@2x) JPEG / PNG
        case RESOURCE_TYPE_ic12: // 64x64 (32x32@2x) JPEG / PNG
        case RESOURCE_TYPE_ic13: // 256x256 (128x128@2x) JPEG / PNG
        case RESOURCE_TYPE_ic14: // 512x512 (128x128@2x) JPEG / PNG
          add_jpeg2000_png_or_direct_color(ret, sec_type, sec_data, sec_size);
          break;

        // Non-image types
        case RESOURCE_TYPE_TOC:
          ret.toc_data.assign(reinterpret_cast<const char*>(sec_data), sec_size);
          break;
        case RESOURCE_TYPE_icnV:
          ret.icon_composer_version = r.get_f32b();
          break;
        case RESOURCE_TYPE_name:
          ret.name.assign(reinterpret_cast<const char*>(sec_data), sec_size);
          break;
        case RESOURCE_TYPE_info:
          ret.info_plist.assign(reinterpret_cast<const char*>(sec_data), sec_size);
          break;
        case RESOURCE_TYPE_sbtp:
          ret.template_icns.reset(new DecodedIconImagesResource(
              ResourceFile::decode_icns(sec_data, sec_size)));
          break;
        case RESOURCE_TYPE_slct:
          ret.selected_icns.reset(new DecodedIconImagesResource(
              ResourceFile::decode_icns(sec_data, sec_size)));
          break;
        case 0xFDD92FA8: // Why did they not use ASCII chars for this type?
          ret.dark_icns.reset(new DecodedIconImagesResource(
              ResourceFile::decode_icns(sec_data, sec_size)));
          break;
        default:
          string type_str = string_for_resource_type(sec_type);
          throw runtime_error("unknown section type " + type_str);
      }
    } catch (const exception& e) {
      string type_str = string_for_resource_type(sec_type);
      throw runtime_error(string_printf("within icns/%s (reader at %zX): %s",
          type_str.c_str(), r.where(), e.what()));
    }
  }

  // Generate composite images for the types that have masks
  auto find_mask = +[](const DecodedIconImagesResource& icns, uint32_t sec_type) -> const Image& {
    auto it = icns.type_to_image.find(sec_type);
    if (it == icns.type_to_image.end()) {
      throw out_of_range("no mask for image");
    }
    return it->second;
  };
  for (const auto& it : ret.type_to_image) {
    try {
      switch (it.first) {
        // For the classic types, the alpha is copied from the corresponding
        // list image (ic*#)
        case RESOURCE_TYPE_icm4:
        case RESOURCE_TYPE_icm8:
          ret.type_to_composite_image.emplace(it.first,
              replace_image_channel(it.second, 3, find_mask(ret, RESOURCE_TYPE_icmN), 3));
          break;
        case RESOURCE_TYPE_ics4:
        case RESOURCE_TYPE_ics8:
          ret.type_to_composite_image.emplace(it.first,
              replace_image_channel(it.second, 3, find_mask(ret, RESOURCE_TYPE_icsN), 3));
          break;
        case RESOURCE_TYPE_icl4:
        case RESOURCE_TYPE_icl8:
          ret.type_to_composite_image.emplace(it.first,
              replace_image_channel(it.second, 3, find_mask(ret, RESOURCE_TYPE_ICNN), 3));
          break;
        case RESOURCE_TYPE_ich4:
        case RESOURCE_TYPE_ich8:
          ret.type_to_composite_image.emplace(it.first,
              replace_image_channel(it.second, 3, find_mask(ret, RESOURCE_TYPE_ichN), 3));
          break;

        // For 32-bit types, the mask image is decoded to a non-alpha Image with
        // the value in all three channels, so we just use the red one
        case RESOURCE_TYPE_is32:
          ret.type_to_composite_image.emplace(it.first,
              replace_image_channel(it.second, 3, find_mask(ret, RESOURCE_TYPE_s8mk), 0));
          break;
        case RESOURCE_TYPE_il32:
          ret.type_to_composite_image.emplace(it.first,
              replace_image_channel(it.second, 3, find_mask(ret, RESOURCE_TYPE_l8mk), 0));
          break;
        case RESOURCE_TYPE_ih32:
          ret.type_to_composite_image.emplace(it.first,
              replace_image_channel(it.second, 3, find_mask(ret, RESOURCE_TYPE_h8mk), 0));
          break;
        case RESOURCE_TYPE_it32:
          ret.type_to_composite_image.emplace(it.first,
              replace_image_channel(it.second, 3, find_mask(ret, RESOURCE_TYPE_t8mk), 0));
          break;
      }
    } catch (const out_of_range&) { }
  }

  return ret;
}

class QuickDrawResourceDasmPort : public QuickDrawPortInterface {
public:
  QuickDrawResourceDasmPort(ResourceFile* rf, size_t x, size_t y)
    : bounds(0, 0, y, x),
      clip_region(this->bounds),
      foreground_color(0xFFFF, 0xFFFF, 0xFFFF),
      background_color(0x0000, 0x0000, 0x0000),
      highlight_color(0xFFFF, 0x0000, 0xFFFF), // TODO: use the right color here
      op_color(0xFFFF, 0xFFFF, 0x0000), // TODO: use the right color here
      extra_space_nonspace(0),
      extra_space_space(0, 0),
      pen_loc(0, 0),
      pen_loc_frac(0),
      pen_size(1, 1),
      pen_mode(0), // TODO
      pen_visibility(0), // visible
      text_font(0), // TODO
      text_mode(0), // TODO
      text_size(0), // TODO
      text_style(0),
      foreground_color_index(0),
      background_color_index(0),
      pen_pixel_pattern(0, 0),
      fill_pixel_pattern(0, 0),
      background_pixel_pattern(0, 0),
      pen_mono_pattern(0xFFFFFFFFFFFFFFFF),
      fill_mono_pattern(0xAA55AA55AA55AA55),
      background_mono_pattern(0x0000000000000000),
      rf(rf),
      img(0, 0) {
    if (x >= 0x10000 || y >= 0x10000) {
      throw runtime_error("PICT resources cannot specify images larger than 65535x65535");
    }
  }
  virtual ~QuickDrawResourceDasmPort() = default;

  Image& image() {
    if (this->img.get_width() == 0) {
      this->img = Image(this->bounds.width(), this->bounds.height());
      // PICTs are rendered into an initially white field, so we fill the canvas
      // with white in case the PICT doesn't actually write all the pixels.
      this->img.clear(0xFFFFFFFF);
    }
    return this->img;
  }
  const Image& image() const {
    return const_cast<QuickDrawResourceDasmPort*>(this)->image();
  }

  // Image data accessors (Image, pixel map, or bitmap)
  virtual size_t width() const {
    return this->bounds.width();
  }
  virtual size_t height() const {
    return this->bounds.height();
  }
  virtual void write_pixel(ssize_t x, ssize_t y, uint8_t r, uint8_t g, uint8_t b) {
    this->image().write_pixel(x, y, r, g, b);
  }
  virtual void blit(
      const Image& src,
      ssize_t dest_x,
      ssize_t dest_y,
      size_t w,
      size_t h,
      ssize_t src_x = 0,
      ssize_t src_y = 0,
      shared_ptr<Region> mask = nullptr,
      ssize_t mask_origin_x = 0,
      ssize_t mask_origin_y = 0) {
    if (mask.get()) {
      Rect effective_mask_rect = mask->rect;
      effective_mask_rect.x1 -= mask_origin_x;
      effective_mask_rect.x2 -= mask_origin_x;
      effective_mask_rect.y1 -= mask_origin_y;
      effective_mask_rect.y2 -= mask_origin_y;
      if (effective_mask_rect.x1 != dest_x ||
          effective_mask_rect.y1 != dest_y ||
          effective_mask_rect.x2 != static_cast<ssize_t>(dest_x + w) ||
          effective_mask_rect.y2 != static_cast<ssize_t>(dest_y + h)) {
        string mask_rect_str = mask->rect.str();
        string effective_mask_rect_str = effective_mask_rect.str();
        throw runtime_error(string_printf(
            "mask region rect %s with effective %s is not same as dest rect [%zd, %zd, %zd, %zd]",
            mask_rect_str.c_str(), effective_mask_rect_str.c_str(),
            dest_x, dest_y, dest_x + w, dest_y + h));
      }
      this->image().mask_blit(src, dest_x, dest_y, w, h, src_x, src_y, mask->render());
    } else {
      this->image().blit(src, dest_x, dest_y, w, h, src_x, src_y);
    }
  }

  // External resource data accessors
  virtual vector<ColorTableEntry> read_clut(int16_t id) {
    return this->rf->decode_clut(id);
  }

  // QuickDraw state accessors
  Rect bounds;
  virtual const Rect& get_bounds() const {
    return this->bounds;
  }
  virtual void set_bounds(Rect z) {
    this->bounds = z;
  }

  Region clip_region;
  virtual const Region& get_clip_region() const {
    return this->clip_region;
  }
  virtual void set_clip_region(Region&& z) {
    this->clip_region = move(z);
  }

  Color foreground_color;
  virtual Color get_foreground_color() const {
    return this->foreground_color;
  }
  virtual void set_foreground_color(Color z) {
    this->foreground_color = z;
  }

  Color background_color;
  virtual Color get_background_color() const {
    return this->background_color;
  }
  virtual void set_background_color(Color z) {
    this->background_color = z;
  }

  Color highlight_color;
  virtual Color get_highlight_color() const {
    return this->highlight_color;
  }
  virtual void set_highlight_color(Color z) {
    this->highlight_color = z;
  }

  Color op_color;
  virtual Color get_op_color() const {
    return this->op_color;
  }
  virtual void set_op_color(Color z) {
    this->op_color = z;
  }

  int16_t extra_space_nonspace;
  virtual int16_t get_extra_space_nonspace() const {
    return this->extra_space_nonspace;
  }
  virtual void set_extra_space_nonspace(int16_t z) {
    this->extra_space_nonspace = z;
  }

  Fixed extra_space_space;
  virtual Fixed get_extra_space_space() const {
    return this->extra_space_space;
  }
  virtual void set_extra_space_space(Fixed z) {
    this->extra_space_space = z;
  }

  Point pen_loc;
  virtual Point get_pen_loc() const {
    return this->pen_loc;
  }
  virtual void set_pen_loc(Point z) {
    this->pen_loc = z;
  }

  int16_t pen_loc_frac;
  virtual int16_t get_pen_loc_frac() const {
    return this->pen_loc_frac;
  }
  virtual void set_pen_loc_frac(int16_t z) {
    this->pen_loc_frac = z;
  }

  Point pen_size;
  virtual Point get_pen_size() const {
    return this->pen_size;
  }
  virtual void set_pen_size(Point z) {
    this->pen_size = z;
  }

  int16_t pen_mode;
  virtual int16_t get_pen_mode() const {
    return this->pen_mode;
  }
  virtual void set_pen_mode(int16_t z) {
    this->pen_mode = z;
  }

  int16_t pen_visibility;
  virtual int16_t get_pen_visibility() const {
    return this->pen_visibility;
  }
  virtual void set_pen_visibility(int16_t z) {
    this->pen_visibility = z;
  }

  int16_t text_font;
  virtual int16_t get_text_font() const {
    return this->text_font;
  }
  virtual void set_text_font(int16_t z) {
    this->text_font = z;
  }

  int16_t text_mode;
  virtual int16_t get_text_mode() const {
    return this->text_mode;
  }
  virtual void set_text_mode(int16_t z) {
    this->text_mode = z;
  }

  int16_t text_size;
  virtual int16_t get_text_size() const {
    return this->text_size;
  }
  virtual void set_text_size(int16_t z) {
    this->text_size = z;
  }

  uint8_t text_style;
  virtual uint8_t get_text_style() const {
    return this->text_style;
  }
  virtual void set_text_style(uint8_t z) {
    this->text_style = z;
  }

  int16_t foreground_color_index;
  virtual int16_t get_foreground_color_index() const {
    return this->foreground_color_index;
  }
  virtual void set_foreground_color_index(int16_t z) {
    this->foreground_color_index = z;
  }

  int16_t background_color_index;
  virtual int16_t get_background_color_index() const {
    return this->background_color_index;
  }
  virtual void set_background_color_index(int16_t z) {
    this->background_color_index = z;
  }

  Image pen_pixel_pattern;
  virtual const Image& get_pen_pixel_pattern() const {
    return this->pen_pixel_pattern;
  }
  virtual void set_pen_pixel_pattern(Image&& z) {
    this->pen_pixel_pattern = move(z);
  }

  Image fill_pixel_pattern;
  virtual const Image& get_fill_pixel_pattern() const {
    return this->fill_pixel_pattern;
  }
  virtual void set_fill_pixel_pattern(Image&& z) {
    this->fill_pixel_pattern = move(z);
  }

  Image background_pixel_pattern;
  virtual const Image& get_background_pixel_pattern() const {
    return this->background_pixel_pattern;
  }
  virtual void set_background_pixel_pattern(Image&& z) {
    this->background_pixel_pattern = move(z);
  }

  Pattern pen_mono_pattern;
  virtual Pattern get_pen_mono_pattern() const {
    return this->pen_mono_pattern;
  }
  virtual void set_pen_mono_pattern(Pattern z) {
    this->pen_mono_pattern = z;
  }

  Pattern fill_mono_pattern;
  virtual Pattern get_fill_mono_pattern() const {
    return this->fill_mono_pattern;
  }
  virtual void set_fill_mono_pattern(Pattern z) {
    this->fill_mono_pattern = z;
  }

  Pattern background_mono_pattern;
  virtual Pattern get_background_mono_pattern() const {
    return this->background_mono_pattern;
  }
  virtual void set_background_mono_pattern(Pattern z) {
    this->background_mono_pattern = z;
  }

protected:
  ResourceFile* rf;
  Image img;
};

ResourceFile::DecodedPictResource ResourceFile::decode_PICT(int16_t id, uint32_t type) {
  return this->decode_PICT(this->get_resource(type, id));
}

ResourceFile::DecodedPictResource ResourceFile::decode_PICT(shared_ptr<const Resource> res) {
  try {
    return this->decode_PICT_internal(res);
  } catch (const exception& e) {
    fprintf(stderr, "warning: rendering of PICT:%hd failed (%s); attempting rendering using picttoppm\n", res->id, e.what());
    return {this->decode_PICT_external(res), "", ""};
  }
}

ResourceFile::DecodedPictResource ResourceFile::decode_PICT_internal(int16_t id, uint32_t type) {
  return this->decode_PICT_internal(this->get_resource(type, id));
}

ResourceFile::DecodedPictResource ResourceFile::decode_PICT_internal(shared_ptr<const Resource> res) {
  if (res->data.size() < sizeof(PictHeader)) {
    throw runtime_error("PICT too small for header");
  }

  try {
    StringReader r(res->data);
    const auto& header = r.get<PictHeader>();
    QuickDrawResourceDasmPort port(this, header.bounds.width(), header.bounds.height());
    QuickDrawEngine eng;
    eng.set_port(&port);
    eng.render_pict(res->data.data(), res->data.size());
    return {move(port.image()), "", ""};

  } catch (const pict_contains_undecodable_quicktime& e) {
    return {Image(0, 0), e.extension, e.data};
  }
}

Image ResourceFile::decode_PICT_external(int16_t id, uint32_t type) {
  return this->decode_PICT_external(this->get_resource(type, id));
}

Image ResourceFile::decode_PICT_external(shared_ptr<const Resource> res) {
  return ResourceFile::decode_PICT_external(res->data.data(), res->data.size());
}

Image ResourceFile::decode_PICT_external(const void* data, size_t size) {
  Subprocess proc({"picttoppm", "-noheader"}, -1, -1, fileno(stderr));
  string ppm_data = proc.communicate(data, size, 10000000);
  int proc_ret = proc.wait(true);
  if (proc_ret != 0) {
    throw runtime_error(string_printf("picttoppm failed (%d)", proc_ret));
  }
  if (ppm_data.empty()) {
    throw runtime_error("picttoppm succeeded but produced no output");
  }
  auto f = fmemopen_unique(ppm_data.data(), ppm_data.size());
  return Image(f.get());
}

vector<Color> ResourceFile::decode_pltt(int16_t id, uint32_t type) {
  return this->decode_pltt(this->get_resource(type, id));
}

vector<Color> ResourceFile::decode_pltt(shared_ptr<const Resource> res) {
  return ResourceFile::decode_pltt(res->data.data(), res->data.size());
}

vector<Color> ResourceFile::decode_pltt(const void* vdata, size_t size) {
  // pltt resources have a 16-byte header, which is coincidentally also the size
  // of each entry. I'm lazy so we'll just load it all at once and use the first
  // "entry" instead of manually making a header struct
  StringReader r(vdata, size);
  const auto& header = r.get<PaletteEntry>();

  // The first header word is the entry count; the rest of the header seemingly
  // doesn't matter at all
  vector<Color> ret;
  ret.reserve(header.c.r);
  while (ret.size() < header.c.r) {
    ret.emplace_back(r.get<PaletteEntry>().c);
  }
  return ret;
}

vector<ColorTableEntry> ResourceFile::decode_clut(int16_t id, uint32_t type) {
  return this->decode_clut(this->get_resource(type, id));
}

vector<ColorTableEntry> ResourceFile::decode_clut(shared_ptr<const Resource> res) {
  return ResourceFile::decode_clut(res->data.data(), res->data.size());
}

vector<ColorTableEntry> ResourceFile::decode_clut(const void* data, size_t size) {
  if (size < sizeof(ColorTableEntry)) {
    throw runtime_error("color table too small for header");
  }

  // clut resources have an 8-byte header, which is coincidentally also the size
  // of each entry. I'm lazy so we'll just load it all at once and use the first
  // "entry" instead of manually making a header struct
  StringReader r(data, size);
  const auto& header = r.get<ColorTableEntry>();

  // The last header word is the entry count; the rest of the header seemingly
  // doesn't matter at all. Unlike for pltt resources, clut counts are
  // inclusive - there are actually (count + 1) colors.
  if (header.c.b == 0xFFFF) {
    return vector<ColorTableEntry>();
  }

  vector<ColorTableEntry> ret;
  ret.reserve(header.c.b + 1);
  while (ret.size() <= header.c.b) {
    ret.emplace_back(r.get<ColorTableEntry>());
  }
  return ret;
}

vector<ColorTableEntry> ResourceFile::decode_actb(int16_t id, uint32_t type) {
  return this->decode_clut(id, type);
}

vector<ColorTableEntry> ResourceFile::decode_actb(shared_ptr<const Resource> res) {
  return ResourceFile::decode_clut(res);
}

vector<ColorTableEntry> ResourceFile::decode_actb(const void* data, size_t size) {
  return ResourceFile::decode_clut(data, size);
}

vector<ColorTableEntry> ResourceFile::decode_cctb(int16_t id, uint32_t type) {
  return this->decode_clut(id, type);
}

vector<ColorTableEntry> ResourceFile::decode_cctb(shared_ptr<const Resource> res) {
  return ResourceFile::decode_clut(res);
}

vector<ColorTableEntry> ResourceFile::decode_cctb(const void* data, size_t size) {
  return ResourceFile::decode_clut(data, size);
}

vector<ColorTableEntry> ResourceFile::decode_dctb(int16_t id, uint32_t type) {
  return this->decode_clut(id, type);
}

vector<ColorTableEntry> ResourceFile::decode_dctb(shared_ptr<const Resource> res) {
  return ResourceFile::decode_clut(res);
}

vector<ColorTableEntry> ResourceFile::decode_dctb(const void* data, size_t size) {
  return ResourceFile::decode_clut(data, size);
}

vector<ColorTableEntry> ResourceFile::decode_fctb(int16_t id, uint32_t type) {
  return this->decode_clut(id, type);
}

vector<ColorTableEntry> ResourceFile::decode_fctb(shared_ptr<const Resource> res) {
  return ResourceFile::decode_clut(res);
}

vector<ColorTableEntry> ResourceFile::decode_fctb(const void* data, size_t size) {
  return ResourceFile::decode_clut(data, size);
}

vector<ColorTableEntry> ResourceFile::decode_wctb(int16_t id, uint32_t type) {
  return this->decode_clut(id, type);
}

vector<ColorTableEntry> ResourceFile::decode_wctb(shared_ptr<const Resource> res) {
  return ResourceFile::decode_clut(res);
}

vector<ColorTableEntry> ResourceFile::decode_wctb(const void* data, size_t size) {
  return ResourceFile::decode_clut(data, size);
}

vector<ColorTableEntry> ResourceFile::decode_CTBL(int16_t id, uint32_t type) {
  return this->decode_CTBL(this->get_resource(type, id));
}

vector<ColorTableEntry> ResourceFile::decode_CTBL(shared_ptr<const Resource> res) {
  return ResourceFile::decode_CTBL(res->data.data(), res->data.size());
}

vector<ColorTableEntry> ResourceFile::decode_CTBL(const void* data, size_t size) {
  StringReader r(data, size);
  uint16_t num_colors = r.get_u16b();
  vector<ColorTableEntry> ret;
  for (size_t z = 0; z < num_colors; z++) {
    auto& e = ret.emplace_back();
    e.c.r = r.get_u8() * 0x101;
    e.c.g = r.get_u8() * 0x101;
    e.c.b = r.get_u8() * 0x101;
    e.color_num = r.get_u8();
  }
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// Sound decoding

struct WaveFileHeader {
  be_uint32_t riff_magic;   // 0x52494646 ('RIFF')
  le_uint32_t file_size;    // size of file - 8
  be_uint32_t wave_magic;   // 0x57415645

  be_uint32_t fmt_magic;    // 0x666d7420 ('fmt ')
  le_uint32_t fmt_size;     // 16
  le_uint16_t format;       // 1 = PCM
  le_uint16_t num_channels;
  le_uint32_t sample_rate;
  le_uint32_t byte_rate;    // num_channels * sample_rate * bits_per_sample / 8
  le_uint16_t block_align;  // num_channels * bits_per_sample / 8
  le_uint16_t bits_per_sample;

  union {
    struct {
      be_uint32_t smpl_magic;
      le_uint32_t smpl_size;
      le_uint32_t manufacturer;
      le_uint32_t product;
      le_uint32_t sample_period;
      le_uint32_t base_note;
      le_uint32_t pitch_fraction;
      le_uint32_t smpte_format;
      le_uint32_t smpte_offset;
      le_uint32_t num_loops; // = 1
      le_uint32_t sampler_data;

      le_uint32_t loop_cue_point_id; // Can be zero? We'll only have at most one loop in this context
      le_uint32_t loop_type; // 0 = normal, 1 = ping-pong, 2 = reverse
      le_uint32_t loop_start; // Start and end are byte offsets into the wave data, not sample indexes
      le_uint32_t loop_end;
      le_uint32_t loop_fraction; // Fraction of a sample to loop (0)
      le_uint32_t loop_play_count; // 0 = loop forever

      be_uint32_t data_magic;   // 0x64617461 ('data')
      le_uint32_t data_size;    // num_samples * num_channels * bits_per_sample / 8
      uint8_t data[0];
    } __attribute__((packed)) with;

    struct {
      be_uint32_t data_magic;   // 0x64617461 ('data')
      le_uint32_t data_size;    // num_samples * num_channels * bits_per_sample / 8
      uint8_t data[0];
    } __attribute__((packed)) without;
  } __attribute__((packed)) loop;

  WaveFileHeader(uint32_t num_samples, uint16_t num_channels, uint32_t sample_rate,
      uint16_t bits_per_sample, uint32_t loop_start = 0, uint32_t loop_end = 0,
      uint8_t base_note = 0x3C) {

    this->riff_magic = 0x52494646; // 'RIFF'
    // this->file_size is set below (it depends on whether there's a loop)
    this->wave_magic = 0x57415645; // 'WAVE'
    this->fmt_magic = 0x666D7420; // 'fmt '
    this->fmt_size = 16;
    this->format = 1;
    this->num_channels = num_channels;
    this->sample_rate = sample_rate;
    this->byte_rate = num_channels * sample_rate * bits_per_sample / 8;
    this->block_align = num_channels * bits_per_sample / 8;
    this->bits_per_sample = bits_per_sample;

    if (((loop_start > 0) && (loop_end > 0)) || (base_note != 0x3C) || (base_note != 0)) {
      this->file_size = num_samples * num_channels * bits_per_sample / 8 +
          sizeof(*this) - 8;

      this->loop.with.smpl_magic = 0x736D706C; // 'smpl'
      this->loop.with.smpl_size = 0x3C;
      this->loop.with.manufacturer = 0;
      this->loop.with.product = 0;
      this->loop.with.sample_period = 1000000000 / this->sample_rate;
      this->loop.with.base_note = base_note;
      this->loop.with.pitch_fraction = 0;
      this->loop.with.smpte_format = 0;
      this->loop.with.smpte_offset = 0;
      this->loop.with.num_loops = 1;
      this->loop.with.sampler_data = 0x18; // includes the loop struct below

      this->loop.with.loop_cue_point_id = 0;
      this->loop.with.loop_type = 0; // 0 = normal, 1 = ping-pong, 2 = reverse

      // Note: loop_start and loop_end are given to this function as sample
      // offsets, but in the wav file, they should be byte offsets
      this->loop.with.loop_start = loop_start * (bits_per_sample >> 3);
      this->loop.with.loop_end = loop_end * (bits_per_sample >> 3);

      this->loop.with.loop_fraction = 0;
      this->loop.with.loop_play_count = 0; // 0 = loop forever

      this->loop.with.data_magic = 0x64617461; // 'data'
      this->loop.with.data_size = num_samples * num_channels * bits_per_sample / 8;

    } else {
      // with_loop is longer than loop.without so we correct for the size
      // disparity manually here
      const uint32_t header_size = sizeof(*this) - sizeof(this->loop.with) +
          sizeof(this->loop.without);
      this->file_size = num_samples * num_channels * bits_per_sample / 8 +
          header_size - 8;

      this->loop.without.data_magic = 0x64617461; // 'data'
      this->loop.without.data_size = num_samples * num_channels * bits_per_sample / 8;
    }
  }

  bool has_loop() const {
    return (this->loop.with.smpl_magic == 0x736D706C);
  }

  size_t size() const {
    if (this->has_loop()) {
      return sizeof(*this);
    } else {
      return sizeof(*this) - sizeof(this->loop.with) + sizeof(this->loop.without);
    }
  }

  uint32_t get_data_size() const {
    if (this->has_loop()) {
      return this->loop.with.data_size;
    } else {
      return this->loop.without.data_size;
    }
  }
} __attribute__((packed));

struct SoundResourceHeaderFormat2 {
  be_uint16_t format_code; // = 2
  be_uint16_t reference_count;
  be_uint16_t num_commands;
} __attribute__((packed));

struct SoundResourceHeaderFormat1 {
  be_uint16_t format_code; // = 1
  be_uint16_t data_format_count; // we only support 0 or 1 here
} __attribute__((packed));

// 3 is not a standard header format; it's used by Beatnik for MPEG-encoded
// samples. This format is only parsed when the ResourceFile's index format is
// HIRF.
struct SoundResourceHeaderFormat3 {
  be_uint16_t format_code;
  be_uint32_t type; // 'none', 'ima4', 'imaW', 'mac3', 'mac6', 'ulaw', 'alaw', or 'mpga'-'mpgn'
  be_uint32_t sample_rate; // actually a Fixed16
  be_uint32_t decoded_bytes;
  be_uint32_t frame_count; // If MPEG, the number of blocks
  be_uint32_t encoded_bytes;
  be_uint32_t unused;
  be_uint32_t start_frame; // If MPEG, the number of uint16_ts to skip
  be_uint32_t channel_loop_start_frame[6];
  be_uint32_t channel_loop_end_frame[6];
  be_uint32_t name_resource_type;
  be_uint32_t name_resource_id;
  uint8_t base_note;
  uint8_t channel_count; // up to 6
  uint8_t bits_per_sample; // 8 or 16
  uint8_t is_embedded;
  uint8_t is_encrypted;
  uint8_t is_little_endian;
  uint8_t reserved1[2];
  be_uint32_t reserved2[8];
} __attribute__((packed));

struct SoundResourceHeaderMohawkChunkHeader {
  be_uint32_t type;
  be_uint32_t size; // not including this header
} __attribute__((packed));

struct SoundResourceHeaderMohawkFormat {
  // Used when header.type = 'Data' or 'Cue#'
  be_uint16_t sample_rate;
  be_uint32_t num_samples; // could be sample bytes, could also be uint16_t
  uint8_t sample_bits;
  uint8_t num_channels;
  be_uint32_t unknown[3];
  // Sample data immediately follows
} __attribute__((packed));

struct SoundResourceDataFormatHeader {
  be_uint16_t data_format_id; // we only support 5 here (sampled sound)
  be_uint32_t flags; // 0x40 = stereo
} __attribute__((packed));

struct SoundResourceCommand {
  be_uint16_t command;
  be_uint16_t param1;
  be_uint32_t param2;
} __attribute__((packed));

struct SoundResourceSampleBuffer {
  be_uint32_t data_offset; // from end of this struct
  be_uint32_t data_bytes;
  be_uint32_t sample_rate;
  be_uint32_t loop_start;
  be_uint32_t loop_end;
  uint8_t encoding;
  uint8_t base_note;
  uint8_t data[0];
} __attribute__((packed));

struct SoundResourceCompressedBuffer {
  be_uint32_t num_frames;
  uint8_t sample_rate[10]; // what kind of encoding is this? lolz
  be_uint32_t marker_chunk;
  be_uint32_t format;
  be_uint32_t reserved1;
  be_uint32_t state_vars; // high word appears to be sample size
  be_uint32_t left_over_block_ptr;
  be_uint16_t compression_id;
  be_uint16_t packet_size;
  be_uint16_t synth_id;
  be_uint16_t bits_per_sample;
  uint8_t data[0];
} __attribute__((packed));

static ResourceFile::DecodedSoundResource decode_snd_data(
    const void* vdata, size_t size, bool metadata_only, bool hirf_semantics,
    bool decompress_ysnd = false) {
  if (size < 4) {
    throw runtime_error("snd doesn\'t even contain a format code");
  }

  StringReader r(vdata, size);

  // These format codes ('Cue#' or 'Data') are the type codes of the first chunk
  // for a Mohawk-specific chunk-based format - we don't want to consume the
  // format code from r because it's part of the first chunk header
  uint32_t format_code32 = r.get_u32b(false);
  if (format_code32 == 0x43756523 || format_code32 == 0x44617461) {

    while (r.remaining() >= sizeof(SoundResourceHeaderMohawkChunkHeader)) {
      const auto& header = r.get<SoundResourceHeaderMohawkChunkHeader>();
      if (header.type == 0x43756523) {
        r.skip(header.size);
      } else if (header.type == 0x44617461) {
        const auto& data_header = r.get<SoundResourceHeaderMohawkFormat>();
        // TODO: we should obviously support different values for these fields
        // but I currently don't have any example files with different values so
        // I can't tell how the samples are interleaved, or even if num_samples
        // is actually num_bytes, num_frames, or something else.
        if (data_header.num_channels != 1) {
          throw runtime_error("MHK snd does not have exactly 1 channel");
        }
        if (data_header.sample_bits != 8) {
          throw runtime_error("MHK snd does not have 8-bit samples");
        }
        WaveFileHeader wav(
            data_header.num_samples,
            data_header.num_channels,
            data_header.sample_rate,
            data_header.sample_bits);
        StringWriter w;
        w.write(&wav, wav.size());
        w.write(r.readx(data_header.num_samples));
        return {
          .is_mp3 = false,
          .sample_rate = data_header.sample_rate,
          .base_note = 0x3C,
          .data = move(w.str()),
        };
      }
    }
    throw runtime_error("MHK snd does not contain a Data section");
  }

  uint16_t format_code16 = r.get_u16b(false);

  // Parse the resource header
  int num_channels = 1;
  size_t num_commands;
  if (format_code16 == 0x0001) {
    const auto& header = r.get<SoundResourceHeaderFormat1>();

    // If data format count is 0, assume mono
    if (header.data_format_count == 0) {
      num_channels = 1;

    } else if (header.data_format_count == 1) {
      const auto& data_format = r.get<SoundResourceDataFormatHeader>();
      if (data_format.data_format_id != 5) {
        throw runtime_error("snd data format is not sampled");
      }
      num_channels = (data_format.flags & 0x40) ? 2 : 1;

    } else {
      throw runtime_error("snd has multiple data formats");
    }

    num_commands = r.get_u16b();

  } else if (format_code16 == 0x0002) {
    const auto& header = r.get<SoundResourceHeaderFormat2>();
    num_commands = header.num_commands;

  } else if ((format_code16 == 0x0003) && hirf_semantics) {
    const auto& header = r.get<SoundResourceHeaderFormat3>();

    if ((header.type & 0xFFFFFF00) != 0x6D706700) {
      throw runtime_error("format 3 snd is not mp3");
    }

    // TODO: for little-endian samples, do we just byteswap the entire stream?
    if (header.is_little_endian) {
      throw runtime_error("format 3 snd is little-endian");
    }
    // TODO: for encrypted samples, do we just call decrypt_soundmusicsys_data
    // on the sample buffer?
    if (header.is_encrypted) {
      throw runtime_error("format 3 snd is encrypted");
    }
    if (decompress_ysnd) {
      throw runtime_error("cannot decompress Ysnd-encoded format 3 snd");
    }

    return {
        true,
        header.sample_rate >> 16,
        static_cast<uint8_t>(header.base_note ? header.base_note : 0x3C),
        metadata_only ? "" : r.read(r.remaining())};

  } else {
    throw runtime_error("snd is not format 1 or 2");
  }

  if (num_commands == 0) {
    throw runtime_error("snd contains no commands");
  }

  size_t sample_buffer_offset = 0;
  for (size_t x = 0; x < num_commands; x++) {
    const auto& command = r.get<SoundResourceCommand>();

    static const unordered_map<uint16_t, const char*> command_names({
        {0x0003, "quiet"},
        {0x0004, "flush"},
        {0x0005, "reinit"},
        {0x000A, "wait"},
        {0x000B, "pause"},
        {0x000C, "resume"},
        {0x000D, "callback"},
        {0x000E, "sync"},
        {0x0018, "available"},
        {0x0019, "version"},
        {0x001A, "get total cpu load"},
        {0x001B, "get channel cpu load"},
        {0x0028, "note"},
        {0x0029, "rest"},
        {0x002A, "set pitch"},
        {0x002B, "set amplitude"},
        {0x002C, "set timbre"},
        {0x002D, "get amplitude"},
        {0x002E, "set volume"},
        {0x002F, "get volume"},
        {0x003C, "load wave table"},
        {0x0052, "set sampled pitch"},
        {0x0053, "get sampled pitch"},
    });

    switch (command.command) {
      case 0x0000: // null (do nothing)
        break;
      case 0x8050: // load sample voice
      case 0x8051: // play sampled sound
        if (sample_buffer_offset) {
          throw runtime_error("snd contains multiple buffer commands");
        }
        sample_buffer_offset = command.param2;
        break;
      default:
        const char* name = nullptr;
        try {
          name = command_names.at(command.command);
        } catch (const out_of_range&) { }
        if (name) {
          throw runtime_error(string_printf(
              "command not implemented: %04hX (%s) %04hX %08X",
              command.command.load(), name, command.param1.load(), command.param2.load()));
        } else {
          throw runtime_error(string_printf(
              "command not implemented: %04hX %04hX %08X",
              command.command.load(), command.param1.load(), command.param2.load()));
        }
    }
  }

  // Some snds have an incorrect sample buffer offset, but they still play! I
  // guess Sound Manager ignores the offset in the command? (We do so here)
  const auto& sample_buffer = r.get<SoundResourceSampleBuffer>();
  uint16_t sample_rate = sample_buffer.sample_rate >> 16;
  uint8_t base_note = sample_buffer.base_note ? sample_buffer.base_note : 0x3C;

  if (metadata_only) {
    return {false, sample_rate, base_note, ""};
  }

  if (decompress_ysnd) {
    if (sample_buffer.encoding != 0x00) {
      throw runtime_error("Ysnd contains doubly-compressed buffer");
    }

    WaveFileHeader wav(sample_buffer.data_bytes, num_channels, sample_rate, 8,
        sample_buffer.loop_start, sample_buffer.loop_end, base_note);

    StringWriter w;
    w.write(&wav, wav.size());

    size_t end_size = sample_buffer.data_bytes + w.size();
    uint8_t p = 0x80;
    while (w.str().size() < end_size) {
      uint8_t x = r.get_u8();
      uint8_t d1 = (x >> 4) - 8;
      p += (d1 * 2);
      d1 += 8;
      if ((d1 != 0) && (d1 != 0x0F)) {
        w.put_u8(p);
        if (w.str().size() >= end_size) {
          break;
        }
      }
      x = (x & 0x0F) - 8;
      p += (x * 2);
      x += 8;
      if ((x != 0) && (x != 0x0F)) {
        w.put_u8(p);
      }
    }

    return {false, sample_rate, base_note, move(w.str())};
  }

  // Uncompressed data can be copied verbatim
  if (sample_buffer.encoding == 0x00) {
    if (sample_buffer.data_bytes == 0) {
      throw runtime_error("snd contains no samples");
    }

    // Some snds have erroneously large values in the data_bytes field, so only
    // trust it if it fits within the resource
    size_t num_samples = min<size_t>(sample_buffer.data_bytes, r.remaining());

    WaveFileHeader wav(num_samples, num_channels, sample_rate, 8,
        sample_buffer.loop_start, sample_buffer.loop_end, base_note);

    StringWriter w;
    w.write(&wav, wav.size());
    w.write(r.readx(num_samples));
    return {false, sample_rate, base_note, move(w.str())};

  // Compressed data will need to be decompressed first
  } else if ((sample_buffer.encoding == 0xFE) || (sample_buffer.encoding == 0xFF)) {
    const auto& compressed_buffer = r.get<SoundResourceCompressedBuffer>();

    // Hack: it appears Beatnik archives set the stereo flag even when the snd
    // is mono, so we ignore it in that case. (TODO: Does this also apply to
    // MACE3/6? I'm assuming it does here, but haven't verified this. Also, what
    // about the uncompressed case above?)
    if (hirf_semantics && (num_channels == 2)) {
      num_channels = 1;
    }

    switch (compressed_buffer.compression_id) {
      case 0xFFFE:
        throw runtime_error("snd uses variable-ratio compression");

      case 3:
      case 4: {
        bool is_mace3 = compressed_buffer.compression_id == 3;
        auto decoded_samples = decode_mace(compressed_buffer.data,
            compressed_buffer.num_frames * (is_mace3 ? 2 : 1) * num_channels,
            num_channels == 2, is_mace3);
        uint32_t loop_factor = is_mace3 ? 3 : 6;

        WaveFileHeader wav(decoded_samples.size() / num_channels, num_channels,
            sample_rate, 16, sample_buffer.loop_start * loop_factor,
            sample_buffer.loop_end * loop_factor, base_note);
        if (wav.get_data_size() != 2 * decoded_samples.size()) {
          throw runtime_error("computed data size does not match decoded data size");
        }

        StringWriter w;
        w.write(&wav, wav.size());
        w.write(decoded_samples.data(), wav.get_data_size());
        return {false, sample_rate, base_note, move(w.str())};
      }

      case 0xFFFF:
        // 'twos' and 'sowt' are equivalent to no compression and fall through
        // to the uncompressed case below. For all others, we'll have to
        // decompress somehow
        if ((compressed_buffer.format != 0x74776F73) && (compressed_buffer.format != 0x736F7774)) {
          vector<le_int16_t> decoded_samples;

          size_t num_frames = compressed_buffer.num_frames;
          uint32_t loop_factor;
          if (compressed_buffer.format == 0x696D6134) { // ima4
            decoded_samples = decode_ima4(
                compressed_buffer.data,
                num_frames * 34 * num_channels,
                (num_channels == 2));
            loop_factor = 4; // TODO: verify this. I don't actually have any examples right now

          } else if ((compressed_buffer.format == 0x4D414333) || (compressed_buffer.format == 0x4D414336)) { // MAC3, MAC6
            bool is_mace3 = compressed_buffer.format == 0x4D414333;
            decoded_samples = decode_mace(
                compressed_buffer.data,
                num_frames * (is_mace3 ? 2 : 1) * num_channels,
                num_channels == 2, is_mace3);
            loop_factor = is_mace3 ? 3 : 6;

          } else if (compressed_buffer.format == 0x756C6177) { // ulaw
            decoded_samples = decode_ulaw(compressed_buffer.data, num_frames);
            loop_factor = 2;

          } else if (compressed_buffer.format == 0x616C6177) { // alaw (guess)
            decoded_samples = decode_alaw(compressed_buffer.data, num_frames);
            loop_factor = 2;

          } else {
            throw runtime_error(string_printf("snd uses unknown compression (%08" PRIX32 ")",
                compressed_buffer.format.load()));
          }

          WaveFileHeader wav(decoded_samples.size() / num_channels, num_channels,
              sample_rate, 16, sample_buffer.loop_start * loop_factor,
              sample_buffer.loop_end * loop_factor, base_note);
          if (wav.get_data_size() != 2 * decoded_samples.size()) {
            throw runtime_error(string_printf(
              "computed data size (%" PRIu32 ") does not match decoded data size (%zu)",
              wav.get_data_size(), 2 * decoded_samples.size()));
          }

          StringWriter w;
          w.write(&wav, wav.size());
          w.write(decoded_samples.data(), wav.get_data_size());
          return {false, sample_rate, base_note, move(w.str())};
        }

        [[fallthrough]];
      case 0: { // No compression
        uint32_t num_samples = compressed_buffer.num_frames;
        uint16_t bits_per_sample = compressed_buffer.bits_per_sample;
        if (bits_per_sample == 0) {
          bits_per_sample = compressed_buffer.state_vars >> 16;
        }

        // Hack: if the sound is stereo and the computed data size is exactly
        // twice the available data size, treat it as mono
        if ((num_channels == 2) &&
            (num_samples * num_channels * (bits_per_sample / 8)) == 2 * r.remaining()) {
          num_channels = 1;
        }

        WaveFileHeader wav(num_samples, num_channels, sample_rate, bits_per_sample,
            sample_buffer.loop_start, sample_buffer.loop_end, base_note);
        if (wav.get_data_size() == 0) {
          throw runtime_error(string_printf(
            "computed data size is zero (%" PRIu32 " samples, %d channels, %" PRIu16 " kHz, %" PRIu16 " bits per sample)",
            num_samples, num_channels, sample_rate, bits_per_sample));
        }
        if (wav.get_data_size() > r.remaining()) {
          throw runtime_error(string_printf("computed data size exceeds actual data (%" PRIu32 " computed, %zu available)",
              wav.get_data_size(), r.remaining()));
        }

        // Byteswap the samples if it's 16-bit and not 'swot'
        string samples_str = r.readx(wav.get_data_size());
        if ((wav.bits_per_sample == 0x10) && (compressed_buffer.format != 0x736F7774)) {
          uint16_t* samples = reinterpret_cast<uint16_t*>(samples_str.data());
          for (uint32_t x = 0; x < samples_str.size() / 2; x++) {
            samples[x] = bswap16(samples[x]);
          }
        }

        StringWriter w;
        w.write(&wav, wav.size());
        w.write(samples_str);
        return {false, sample_rate, base_note, move(w.str())};
      }

      default:
        throw runtime_error("snd is compressed using unknown algorithm");
    }

  } else {
    throw runtime_error(string_printf("unknown encoding for snd data: %02hhX",
        sample_buffer.encoding));
  }
}



ResourceFile::DecodedSoundResource ResourceFile::decode_snd(
    int16_t id, uint32_t type, bool metadata_only) {
  return this->decode_snd(this->get_resource(type, id), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_snd(
    shared_ptr<const Resource> res, bool metadata_only) {
  return ResourceFile::decode_snd(res->data.data(), res->data.size(), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_snd(
    const void* data, size_t size, bool metadata_only) {
  return decode_snd_data(data, size, metadata_only,
      this->index_format() == IndexFormat::HIRF);
}



static string lzss_decompress(const void* vsrc, size_t size) {
  StringReader r(vsrc, size);
  string ret;

  for (;;) {
    if (r.eof()) {
      return ret;
    }
    uint8_t control_bits = r.get_u8();

    for (uint8_t control_mask = 0x01; control_mask; control_mask <<= 1) {
      if (control_bits & control_mask) {
        if (r.eof()) {
          return ret;
        }
        ret += static_cast<char>(r.get_s8());

      } else {
        if (r.where() >= r.size() - 1) {
          return ret;
        }
        uint16_t params = r.get_u16b();

        size_t copy_offset = ret.size() - ((1 << 12) - (params & 0x0FFF));
        uint8_t count = ((params >> 12) & 0x0F) + 3;
        size_t copy_end_offset = copy_offset + count;

        for (; copy_offset != copy_end_offset; copy_offset++) {
          ret += ret.at(copy_offset);
        }
      }
    }
  }
  return ret;
}

static string decompress_soundmusicsys_data(const void* data, size_t size) {
  StringReader r(data, size);

  // It looks like encrypted resources sometimes have 0xFF in the type field
  // (high byte of decompressed_size), even if delta-encoding wouldn't make
  // sense, like for MIDI resources. We assume we'll never decode a 4GB
  // resource, so mask out the high byte if it's 0xFF.
  // TODO: Do we need to support the other delta-encoding types here? Should we
  // factor the delta decoding logic into here?
  uint32_t decompressed_size = r.get_u32b();
  if (decompressed_size & 0xFF000000) {
    decompressed_size &= 0x00FFFFFF;
  }

  size_t compressed_size = r.remaining();
  string decompressed = lzss_decompress(r.getv(compressed_size), compressed_size);
  if (decompressed.size() != decompressed_size) {
    throw runtime_error(string_printf(
        "decompression produced incorrect amount of data (0x%zX bytes expected, 0x%" PRIX32 " bytes received)",
        decompressed.size(), decompressed_size));
  }
  return decompressed;
}

static string decrypt_soundmusicsys_data(const void* vsrc, size_t size) {
  StringReader r(vsrc, size);

  string ret;
  ret.reserve(size);
  uint32_t v = 56549L;
  while (!r.eof()) {
    uint8_t ch = r.get_u8();
    ret.push_back(ch ^ (v >> 8L));
    v = (static_cast<uint32_t>(ch) + v) * 52845L + 22719L;
  }
  return ret;
}

static string decrypt_soundmusicsys_cstr(StringReader& r) {
  uint32_t v = 56549L;
  string ret;
  for (;;) {
    uint8_t ch = r.get_u8();
    uint8_t ch_out = ch ^ (v >> 8L);
    if (ch_out == 0) {
      return ret;
    }
    ret.push_back(ch_out);
    v = (static_cast<uint32_t>(ch) + v) * 52845L + 22719L;
  }
}

string ResourceFile::decode_SMSD(int16_t id, uint32_t type) {
  return this->decode_SMSD(this->get_resource(type, id));
}

string ResourceFile::decode_SMSD(shared_ptr<const Resource> res) {
  return ResourceFile::decode_SMSD(res->data.data(), res->data.size());
}

string ResourceFile::decode_SMSD(const void* data, size_t size) {
  StringReader r(data, size);

  // There's just an 8-byte header, then the rest of it is 22050Hz 8-bit mono.
  // TODO: Is there anything useful in this 8-byte header? All of the examples
  // I've seen have various values in there but are all 22050Hz 8-bit mono, so
  // it seems the header doesn't have anything useful in it
  r.skip(8);
  size_t data_bytes = r.remaining();
  WaveFileHeader wav(data_bytes, 1, 22050, 8);

  StringWriter w;
  w.write(&wav, wav.size());
  w.write(r.getv(data_bytes), data_bytes);
  return move(w.str());
}

string ResourceFile::decode_SOUN(int16_t id, uint32_t type) {
  return this->decode_SOUN(this->get_resource(type, id));
}

string ResourceFile::decode_SOUN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_SOUN(res->data.data(), res->data.size());
}

string ResourceFile::decode_SOUN(const void* data, size_t size) {
  if (size < 0x16) {
    throw runtime_error("resource too small for header");
  }

  // SOUN resources are like this:
  //   uint8_t unknown[6]; // usually 07D001020001 (last byte is sometimes 00)
  //   int8_t delta_table[0x10];
  //   uint8_t data[0];

  StringReader r(data, size);
  r.skip(6); // unknown[6]

  // Technically this should be signed, but we'll just let it overflow
  uint8_t delta_table[0x10];
  r.readx(delta_table, 0x10);

  // We already know how much data is going to be produced, so we can write the
  // WAV header before decoding the data
  StringWriter w;
  WaveFileHeader wav(r.remaining() * 2, 1, 11025, 8);
  w.write(&wav, wav.size());

  // The data is a sequence of nybbles, which are indexes into the delta table.
  // The initial sample is 0x80 (center), and each nybble specifies which delta
  // to add to the initial sample.
  uint8_t sample = 0x80;
  while (!r.eof()) {
    uint8_t d = r.get_u8();
    sample += delta_table[(d >> 4) & 0x0F];
    w.put_u8(sample);
    sample += delta_table[d & 0x0F];
    w.put_u8(sample);
  }

  return move(w.str());
}

ResourceFile::DecodedSoundResource ResourceFile::decode_csnd(
    int16_t id, uint32_t type, bool metadata_only) {
  return this->decode_csnd(this->get_resource(type, id),  metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_csnd(
    shared_ptr<const Resource> res, bool metadata_only) {
  return ResourceFile::decode_csnd(res->data.data(), res->data.size(), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_csnd(
    const void* data, size_t size, bool metadata_only) {
  StringReader r(data, size);
  uint32_t type_and_size = r.get_u32b();

  uint8_t sample_type = type_and_size >> 24;
  if ((sample_type > 3) && (sample_type != 0xFF)) {
    throw runtime_error("invalid csnd sample type");
  }

  // Check that decompressed_size makes sense for the type (for types 1 and 2,
  // it must be a multiple of 2; for type 3, it must be a multiple of 4)
  size_t decompressed_size = type_and_size & 0x00FFFFFF;
  if (sample_type != 0xFF) {
    uint8_t sample_bytes = (sample_type == 2) ? sample_type : (sample_type + 1);
    if (decompressed_size % sample_bytes) {
      throw runtime_error("decompressed size is not a multiple of frame size");
    }
  }

  size_t compressed_size = r.remaining();
  string decompressed = lzss_decompress(r.getv(compressed_size), compressed_size);
  if (decompressed.size() < decompressed_size) {
    throw runtime_error("decompression did not produce enough data");
  }
  decompressed.resize(decompressed_size);

  // If sample_type isn't 0xFF, then the buffer is delta-encoded
  if (sample_type == 0) { // mono8
    uint8_t* data = reinterpret_cast<uint8_t*>(decompressed.data());
    uint8_t* data_end = data + decompressed.size();
    for (uint8_t sample = *data++; data != data_end; data++) {
      *data = (sample += *data);
    }

  } else if (sample_type == 2) { // mono16
    be_uint16_t* data = reinterpret_cast<be_uint16_t*>(decompressed.data());
    be_uint16_t* data_end = data + decompressed.size();
    for (uint16_t sample = *(data++); data != data_end; data++) {
      *data = (sample += *data);
    }

  } else if (sample_type == 1) { // stereo8
    uint8_t* data = reinterpret_cast<uint8_t*>(decompressed.data());
    uint8_t* data_end = data + decompressed.size();
    data += 2;
    for (uint8_t sample0 = data[-2], sample1 = data[-1]; data != data_end; data += 2) {
      data[0] = (sample0 += data[0]);
      data[1] = (sample1 += data[1]);
    }

  } else if (sample_type == 3) { // stereo16
    be_uint16_t* data = reinterpret_cast<be_uint16_t*>(decompressed.data());
    be_uint16_t* data_end = data + decompressed.size();
    data += 2;
    for (uint16_t sample0 = data[-2], sample1 = data[-1]; data != data_end; data += 2) {
      data[0] = (sample0 += data[0]);
      data[1] = (sample1 += data[1]);
    }
  }

  // The result is a snd resource, which we can then decode normally
  return decode_snd_data(decompressed.data(), decompressed.size(),
      metadata_only, this->index_format() == IndexFormat::HIRF);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_esnd(
    int16_t id, uint32_t type, bool metadata_only) {
  return this->decode_esnd(this->get_resource(type, id), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_esnd(
    shared_ptr<const Resource> res, bool metadata_only) {
  return ResourceFile::decode_esnd(res->data.data(), res->data.size(), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_esnd(
    const void* data, size_t size, bool metadata_only) {
  string decrypted = decrypt_soundmusicsys_data(data, size);
  return decode_snd_data(decrypted.data(), decrypted.size(), metadata_only,
      this->index_format() == IndexFormat::HIRF);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_ESnd(
    int16_t id, uint32_t type, bool metadata_only) {
  return this->decode_ESnd(this->get_resource(type, id), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_ESnd(
    shared_ptr<const Resource> res, bool metadata_only) {
  return ResourceFile::decode_ESnd(res->data.data(), res->data.size(), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_ESnd(
    const void* vdata, size_t size, bool metadata_only) {
  string data(reinterpret_cast<const char*>(vdata), size);
  uint8_t* ptr = reinterpret_cast<uint8_t*>(data.data());
  uint8_t* data_end = ptr + data.size();
  for (uint8_t sample = (*ptr++ ^= 0xFF); ptr != data_end; ptr++) {
    *ptr = (sample += (*ptr ^ 0xFF));
  }
  return decode_snd_data(data.data(), data.size(), metadata_only,
      (this->index_format() == IndexFormat::HIRF));
}

ResourceFile::DecodedSoundResource ResourceFile::decode_Ysnd(
    int16_t id, uint32_t type, bool metadata_only) {
  return this->decode_Ysnd(this->get_resource(type, id), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_Ysnd(
    shared_ptr<const Resource> res, bool metadata_only) {
  return ResourceFile::decode_Ysnd(res->data.data(), res->data.size(), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_Ysnd(
    const void* vdata, size_t size, bool metadata_only) {
  return decode_snd_data(vdata, size, metadata_only,
      (this->index_format() == IndexFormat::HIRF), true);
}

string ResourceFile::decode_cmid(int16_t id, uint32_t type) {
  return this->decode_cmid(this->get_resource(type, id));
}

string ResourceFile::decode_cmid(shared_ptr<const Resource> res) {
  return ResourceFile::decode_cmid(res->data.data(), res->data.size());
}

string ResourceFile::decode_cmid(const void* data, size_t size) {
  return decompress_soundmusicsys_data(data, size);
}

string ResourceFile::decode_emid(int16_t id, uint32_t type) {
  return this->decode_emid(this->get_resource(type, id));
}

string ResourceFile::decode_emid(shared_ptr<const Resource> res) {
  return ResourceFile::decode_emid(res->data.data(), res->data.size());
}

string ResourceFile::decode_emid(const void* vdata, size_t size) {
  return decrypt_soundmusicsys_data(vdata, size);
}

string ResourceFile::decode_ecmi(int16_t id, uint32_t type) {
  return this->decode_ecmi(this->get_resource(type, id));
}

string ResourceFile::decode_ecmi(shared_ptr<const Resource> res) {
  return ResourceFile::decode_ecmi(res->data.data(), res->data.size());
}

string ResourceFile::decode_ecmi(const void* data, size_t size) {
  string decrypted = decrypt_soundmusicsys_data(data, size);
  return decompress_soundmusicsys_data(decrypted.data(), decrypted.size());
}



////////////////////////////////////////////////////////////////////////////////
// Sequenced music decoding

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

  be_int16_t snd_id; // or csnd or esnd
  be_uint16_t base_note; // if zero, use the snd's base_note
  uint8_t panning;
  uint8_t flags1;
  uint8_t flags2;
  int8_t smod_id;
  be_int16_t smod_params[2];
  be_uint16_t num_key_regions;
} __attribute__((packed));

struct InstrumentResourceKeyRegion {
  // low/high are inclusive
  uint8_t key_low;
  uint8_t key_high;

  be_int16_t snd_id;
  be_int16_t smod_params[2];
} __attribute__((packed));

ResourceFile::DecodedInstrumentResource::KeyRegion::KeyRegion(uint8_t key_low,
    uint8_t key_high, uint8_t base_note, int16_t snd_id, uint32_t snd_type) :
    key_low(key_low), key_high(key_high), base_note(base_note), snd_id(snd_id),
    snd_type(snd_type) { }

ResourceFile::DecodedInstrumentResource ResourceFile::decode_INST(int16_t id, uint32_t type) {
  return this->decode_INST(this->get_resource(type, id));
}

ResourceFile::DecodedInstrumentResource ResourceFile::decode_INST(shared_ptr<const Resource> res) {
  unordered_set<int16_t> ids_in_progress;
  return this->decode_INST_recursive(res, ids_in_progress);
}

ResourceFile::DecodedInstrumentResource ResourceFile::decode_INST_recursive(
    shared_ptr<const Resource> res, unordered_set<int16_t>& ids_in_progress) {
  if (!ids_in_progress.emplace(res->id).second) {
    throw runtime_error("reference cycle between INST resources");
  }

  StringReader r(res->data);

  const auto& header = r.get<InstrumentResourceHeader>();

  DecodedInstrumentResource ret;
  ret.base_note = header.base_note;
  ret.constant_pitch = (header.flags2 & InstrumentResourceHeader::Flags2::PLAY_AT_SAMPLED_FREQ);
  // If the UseSampleRate flag is not set, then the synthesizer apparently
  // doesn't correct for sample rate differences at all. This means that if your
  // INSTs refer to snds that are 11025kHz but you're playing at 22050kHz, your
  // song will be shifted up an octave. Even worse, if you have snds with
  // different sample rates, the pitches of all notes will be messed up. (It
  // would seem like this should be enabled in all cases, but apparently it's
  // not enabled in a lot of cases, and some songs depend on that. Sigh...)
  ret.use_sample_rate = (header.flags1 & InstrumentResourceHeader::Flags1::USE_SAMPLE_RATE);

  auto add_key_region = [&](int16_t snd_id, uint8_t key_low, uint8_t key_high) {
    uint8_t base_note = (header.flags2 & InstrumentResourceHeader::Flags2::PLAY_AT_SAMPLED_FREQ) ?
        0x3C : header.base_note.load();

    uint32_t snd_type = this->find_resource_by_id(snd_id,
        {RESOURCE_TYPE_esnd, RESOURCE_TYPE_csnd, RESOURCE_TYPE_snd, RESOURCE_TYPE_INST});
    if (snd_type == RESOURCE_TYPE_INST) {
      // TODO: Should we apply overrides from the current INST here? (Should we
      // take into account header.base_note, for example?) I seem to recall
      // seeing some sort of nominal abstraction within SoundMusicSys that would
      // support e.g. applications of base_notes from both INSTs in succession,
      // but I'm too lazy to go find it right now. For now, we just copy the
      // relevant parts of the other INST's key regions into this one; this
      // seems to give the correct result for the INSTs with subordinates that
      // I've seen.
      auto sub_res = this->get_resource(snd_type, snd_id);
      auto sub_inst = this->decode_INST_recursive(sub_res, ids_in_progress);
      for (const auto& sub_rgn : sub_inst.key_regions) {
        // If the sub region doesn't overlap any of the requested range, skip it
        if ((sub_rgn.key_high < key_low) || (sub_rgn.key_low > key_high)) {
          continue;
        }
        // Clamp the sub region's low/high keys to the requested range and copy
        // it into the current INST
        ret.key_regions.emplace_back(
            max<uint8_t>(key_low, sub_rgn.key_low),
            min<uint8_t>(key_high, sub_rgn.key_high),
            sub_rgn.base_note,
            sub_rgn.snd_id,
            sub_rgn.snd_type);
      }

    } else {
      // If the snd has PlayAtSampledFreq, set a fake base_note of 0x3C to
      // ignore whatever the snd/csnd/esnd says.
      ret.key_regions.emplace_back(key_low, key_high, base_note, snd_id, snd_type);
    }
  };

  if (header.num_key_regions == 0) {
    add_key_region(header.snd_id, 0x00, 0x7F);
  } else {
    for (size_t x = 0; x < header.num_key_regions; x++) {
      const auto& rgn = r.get<InstrumentResourceKeyRegion>();
      add_key_region(rgn.snd_id, rgn.key_low, rgn.key_high);
    }
  }

  uint16_t tremolo_count = r.get_u16b();
  while (ret.tremolo_data.size() < tremolo_count) {
    ret.tremolo_data.emplace_back(r.get_u16b());
  }
  r.skip(2); // Should be 0x8000, but is sometimes incorrect - be lenient here
  r.skip(2); // Reserved

  ret.copyright = r.readx(r.get_u8());
  ret.author = r.readx(r.get_u8());

  ids_in_progress.erase(res->id);
  return ret;
}



struct SMSSongResourceHeader {
  struct InstrumentOverride {
    be_uint16_t midi_channel_id;
    be_uint16_t inst_resource_id;
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

  be_int16_t midi_id;
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
  be_uint16_t tempo_bias; // 0 = default = 16667; linear, so 8333 = half-speed
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
  be_uint16_t mix_level;
  uint8_t flags1;
  uint8_t note_decay; // In 1/60ths apparently
  uint8_t percussion_instrument; // Channel 10; 0 = none, 0xFF = GM percussion
  uint8_t flags2;

  be_uint16_t instrument_override_count;

  // Variable-length fields follow:
  // InstrumentOverride instrument_overrides[instrument_override_count];
  // pstring copyright;
  // pstring author;
} __attribute__((packed));

struct RMFSongResourceHeader {
  // Many of these fields are the same as those in SMSSongResourceHeader; see
  // that structure for comments.
  be_int16_t midi_id;
  uint8_t reserved1;
  uint8_t reverb_type;
  be_uint16_t tempo_bias;
  uint8_t midi_format; // (RMF) 0 = private, 1 = RMF structure, 2 = RMF linear
  uint8_t encrypted;
  be_int16_t semitone_shift;
  be_uint16_t max_concurrent_streams;
  be_uint16_t max_voices;
  be_uint16_t max_signals;
  be_uint16_t volume_bias; // 0 = normal = 007F; linear, so 00FE = double volume
  uint8_t is_in_instrument_bank;
  uint8_t reserved2;
  be_uint32_t reserved3[7];
  be_uint16_t num_subresources;
} __attribute__((packed));

static ResourceFile::DecodedSongResource decode_SONG_SMS(
    const void* vdata, size_t size) {
  StringReader r(vdata, size);

  const auto& header = r.get<SMSSongResourceHeader>();

  // Note: They split the pitch shift field in a later version of the library;
  // some older SONGs that have a negative value in the pitch_shift field may
  // also set filter_type to 0xFF because it was part of pitch_shift before.
  // We currently don't use filter_type at all, so we don't bother changing it.
  // if (header.filter_type == 0xFF) {
  //   header.filter_type = 0;
  // }

  ResourceFile::DecodedSongResource ret;
  ret.is_rmf = false;
  ret.midi_id = header.midi_id;
  ret.midi_format = 0xFFFF; // standard MIDI
  ret.tempo_bias = header.tempo_bias;
  ret.volume_bias = 127;
  ret.semitone_shift = header.semitone_shift;
  ret.percussion_instrument = header.percussion_instrument;
  ret.allow_program_change = (header.flags1 & SMSSongResourceHeader::Flags1::ENABLE_MIDI_PROGRAM_CHANGE);
  for (size_t x = 0; x < header.instrument_override_count; x++) {
    const auto& override = r.get<SMSSongResourceHeader::InstrumentOverride>();
    ret.instrument_overrides.emplace(
        override.midi_channel_id, override.inst_resource_id);
  }
  if (!r.eof()) {
    ret.copyright_text = r.readx(r.get_u8());
  }
  if (!r.eof()) {
    ret.composer = r.readx(r.get_u8());
  }
  return ret;
}

static ResourceFile::DecodedSongResource decode_SONG_RMF(
    const void* vdata, size_t size) {
  StringReader r(vdata, size);

  const auto& header = r.get<RMFSongResourceHeader>();
  ResourceFile::DecodedSongResource ret;
  ret.is_rmf = true;
  ret.midi_id = header.midi_id;
  ret.midi_format = header.midi_format;
  ret.tempo_bias = header.tempo_bias;
  ret.volume_bias = header.volume_bias;
  ret.semitone_shift = header.semitone_shift;
  ret.percussion_instrument = -1;
  ret.allow_program_change = true;

  for (uint16_t x = 0; x < header.num_subresources; x++) {
    uint32_t type = r.get_u32b();
    if (type == 0x524D4150) { // RMAP (instrument override)
      uint16_t from_inst = r.get_u16b();
      uint16_t to_inst = r.get_u16b();
      ret.instrument_overrides.emplace(from_inst, to_inst);
    } else if (type == 0x56454C43) {
      ret.velocity_override_map.clear();
      ret.velocity_override_map.reserve(0x80);
      while (ret.velocity_override_map.size() < 0x80) {
        ret.velocity_override_map.emplace_back(r.get_u16b());
      }

    } else if (type == 0x5449544C) { // 'TITL'
      ret.title = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x50455246) { // 'PERF'
      ret.performer = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x434F4D50) { // 'COMP'
      ret.composer = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x434F5044) { // 'COPD'
      ret.copyright_date = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x434F504C) { // 'COPL'
      ret.copyright_text = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x4C494343) { // 'LICC'
      ret.license_contact = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x4C555345) { // 'LUSE'
      ret.license_uses = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x4C444F4D) { // 'LDOM'
      ret.license_domain = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x4C54524D) { // 'LTRM'
      ret.license_term = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x45585044) { // 'EXPD'
      ret.license_expiration = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x4E4F5445) { // 'NOTE'
      ret.note = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x494E4458) { // 'INDX'
      ret.index_number = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x47454E52) { // 'GENR'
      ret.genre = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x53554247) { // 'SUBG'
      ret.subgenre = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();

    } else {
      throw runtime_error("unknown SONG subresource type");
    }
  }

  return ret;
}

ResourceFile::DecodedSongResource ResourceFile::decode_SONG(int16_t id, uint32_t type) {
  return this->decode_SONG(this->get_resource(type, id));
}

ResourceFile::DecodedSongResource ResourceFile::decode_SONG(shared_ptr<const Resource> res) {
  return this->decode_SONG(res->data.data(), res->data.size());
}

ResourceFile::DecodedSongResource ResourceFile::decode_SONG(const void* vdata, size_t size) {
  if (this->index_format() == IndexFormat::HIRF) {
    return decode_SONG_RMF(vdata, size);
  } else {
    return decode_SONG_SMS(vdata, size);
  }
}

struct TuneResourceHeader {
  be_uint32_t header_size; // Includes the sample description commands in the MIDI stream
  be_uint32_t magic; // 'musi'
  be_uint32_t reserved1;
  be_uint16_t reserved2;
  be_uint16_t index;
  be_uint32_t flags;
  // MIDI track data immediately follows
} __attribute__((packed));

string ResourceFile::decode_Tune(int16_t id, uint32_t type) {
  return this->decode_Tune(this->get_resource(type, id));
}

string ResourceFile::decode_Tune(shared_ptr<const Resource> res) {
  return ResourceFile::decode_Tune(res->data.data(), res->data.size());
}

string ResourceFile::decode_Tune(const void* vdata, size_t size) {
  struct MIDIChunkHeader {
    be_uint32_t magic; // MThd or MTrk
    be_uint32_t size;
  } __attribute__((packed));
  struct MIDIHeader {
    MIDIChunkHeader header;
    be_uint16_t format;
    be_uint16_t track_count;
    be_uint16_t division;
  } __attribute__((packed));

  StringReader r(vdata, size);

  const auto& tune = r.get<TuneResourceHeader>();
  if (tune.magic != 0x6D757369) { // 'musi'
    throw runtime_error("Tune identifier is incorrect");
  }

  // Convert Tune events into MIDI events
  struct Event {
    uint64_t when;
    uint8_t status;
    string data;

    Event(uint64_t when, uint8_t status, uint8_t param) :
        when(when), status(status) {
      this->data.push_back(param);
    }

    Event(uint64_t when, uint8_t status, uint8_t param1, uint8_t param2) :
        when(when), status(status) {
      this->data.push_back(param1);
      this->data.push_back(param2);
    }
  };

  vector<Event> events;
  unordered_map<uint16_t, uint8_t> partition_id_to_channel;
  uint64_t current_time = 0;

  while (!r.eof()) {
    uint32_t event = r.get_u32b();
    uint8_t type = (event >> 28) & 0x0F;

    switch (type) {
      case 0x00:
      case 0x01: // pause
        current_time += (event & 0x00FFFFFF);
        break;

      case 0x02: // simple note event
      case 0x03: // simple note event
      case 0x09: { // extended note event
        uint8_t key, vel;
        uint16_t partition_id, duration;
        if (type == 0x09) {
          uint32_t options = r.get_u32b();
          partition_id = (event >> 16) & 0xFFF;
          key = (event >> 8) & 0xFF;
          vel = (options >> 22) & 0x7F;
          duration = options & 0x3FFFFF;
        } else {
          partition_id = (event >> 24) & 0x1F;
          key = ((event >> 18) & 0x3F) + 32;
          vel = (event >> 11) & 0x7F;
          duration = event & 0x7FF;
        }

        uint8_t channel;
        try {
          channel = partition_id_to_channel.at(partition_id);
        } catch (const out_of_range&) {
          throw runtime_error("notes produced on uninitialized partition");
        }

        events.emplace_back(current_time, 0x90 | channel, key, vel);
        events.emplace_back(current_time + duration, 0x80 | channel, key, vel);
        break;
      }

      case 0x04: // simple controller event
      case 0x05: // simple controller event
      case 0x0A: { // extended controller event
        uint16_t message, partition_id, value;
        if (type == 0x0A) {
          uint32_t options = r.get_u32b();
          message = (options >> 16) & 0x3FFF;
          partition_id = (event >> 16) & 0xFFF;
          value = options & 0xFFFF;
        } else {
          message = (event >> 16) & 0xFF;
          partition_id = (event >> 24) & 0x1F;
          value = event & 0xFFFF;
        }

        // controller messages can create channels
        uint8_t channel = partition_id_to_channel.emplace(
            partition_id, partition_id_to_channel.size()).first->second;
        if (channel >= 0x10) {
          throw runtime_error("not enough MIDI channels");
        }

        if (message == 0) {
          // bank select (ignore for now)
          break;

        } else if (message == 32) {
          // pitch bend

          // clamp the value and convert to MIDI range (14-bit)
          int16_t s_value = static_cast<int16_t>(value);
          if (s_value < -0x0200) {
            s_value = -0x0200;
          }
          if (s_value > 0x01FF) {
            s_value = 0x01FF;
          }
          s_value = (s_value + 0x200) * 0x10;

          events.emplace_back(current_time, 0xE0 | channel, s_value & 0x7F, (s_value >> 7) & 0x7F);

        } else {
          // some other controller message
          events.emplace_back(current_time, 0xB0 | channel, message, value >> 8);
        }

        break;
      }

      case 0x0F: { // metadata message
        uint16_t partition_id = (event >> 16) & 0xFFF;
        uint32_t message_size = (event & 0xFFFF) * 4;
        if (message_size < 8) {
          throw runtime_error("metadata message too short for type field");
        }

        string message_data = r.readx(message_size - 4);
        if (message_data.size() != message_size - 4) {
          throw runtime_error("metadata message exceeds track boundary");
        }

        // the second-to-last word is the message type
        uint16_t message_type = *reinterpret_cast<const be_uint16_t*>(
            message_data.data() + message_data.size() - 4) & 0x3FFF;

        // meta messages can create channels
        uint8_t channel = partition_id_to_channel.emplace(
            partition_id, partition_id_to_channel.size()).first->second;
        if (channel >= 0x10) {
          throw runtime_error("not enough MIDI channels");
        }

        switch (message_type) {
          case 1: { // instrument definition
            if (message_size != 0x5C) {
              throw runtime_error("message size is incorrect");
            }
            uint32_t instrument = *reinterpret_cast<const be_uint32_t*>(message_data.data() + 0x50);
            events.emplace_back(current_time, 0xC0 | channel, instrument);
            events.emplace_back(current_time, 0xB0 | channel, 7, 0x7F); // volume
            events.emplace_back(current_time, 0xB0 | channel, 10, 0x40); // panning
            events.emplace_back(current_time, 0xE0 | channel, 0x00, 0x40); // pitch bend
            break;
          }

          case 6: { // extended (?) instrument definition
            if (message_size != 0x88) {
              throw runtime_error("message size is incorrect");
            }
            uint32_t instrument = *reinterpret_cast<const be_uint32_t*>(message_data.data() + 0x7C);
            events.emplace_back(current_time, 0xC0 | channel, instrument);
            events.emplace_back(current_time, 0xB0 | channel, 7, 0x7F); // volume
            events.emplace_back(current_time, 0xB0 | channel, 10, 0x40); // panning
            events.emplace_back(current_time, 0xE0 | channel, 0x00, 0x40); // pitch bend
            break;
          }

          case 5: // tune difference
          case 8: // MIDI channel (probably we should use this)
          case 10: // nop
          case 11: // notes used
            break;

          default:
            throw runtime_error(string_printf(
                "unknown metadata event %08" PRIX32 "/%hX (end offset 0x%zX)",
                event, message_type, r.where() + sizeof(TuneResourceHeader)));
        }

        break;
      }

      case 0x08: // reserved (ignored; has 4-byte argument)
      case 0x0C: // reserved (ignored; has 4-byte argument)
      case 0x0D: // reserved (ignored; has 4-byte argument)
      case 0x0E: // reserved (ignored; has 4-byte argument)
        r.go(r.where() + 4);
      case 0x06: // marker (ignored)
      case 0x07: // marker (ignored)
        break;

      default:
        throw runtime_error("unsupported event in stream");
    }
  }

  // append the MIDI track end event
  events.emplace_back(current_time, 0xFF, 0x2F, 0x00);

  // sort the events by time, since there can be out-of-order note off events
  stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
    return a.when < b.when;
  });

  // generate the MIDI track
  string midi_track_data;
  current_time = 0;
  for (const Event& event : events) {
    uint64_t delta = event.when - current_time;
    current_time = event.when;

    // write the delay field (encoded as variable-length int)
    string delta_str;
    while (delta > 0x7F) {
      delta_str.push_back(delta & 0x7F);
      delta >>= 7;
    }
    delta_str.push_back(delta);
    for (size_t x = 1; x < delta_str.size(); x++) {
      delta_str[x] |= 0x80;
    }
    reverse(delta_str.begin(), delta_str.end());
    midi_track_data += delta_str;

    // write the event contents
    midi_track_data.push_back(event.status);
    midi_track_data += event.data;
  }

  // generate the MIDI headers
  MIDIHeader midi_header;
  midi_header.header.magic = 0x4D546864; // 'MThd'
  midi_header.header.size = 6;
  midi_header.format = 0;
  midi_header.track_count = 1;
  midi_header.division = 600; // ticks per quarter note

  MIDIChunkHeader track_header;
  track_header.magic = 0x4D54726B; // 'MTrk'
  track_header.size = midi_track_data.size();

  // generate the file and return it
  StringWriter w;
  w.put<MIDIHeader>(midi_header);
  w.put<MIDIChunkHeader>(track_header);
  w.write(midi_track_data);
  return move(w.str());
}



////////////////////////////////////////////////////////////////////////////////
// String decoding


static const string mac_roman_table_rtf[0x100] = {
  // 00
  // Note: we intentionally incorrectly decode \r as \line here to convert CR
  // line breaks to LF line breaks which modern systems use
  "\\\'00", "\\'01", "\\'02", "\\'03", "\\'04", "\\'05", "\\'06", "\\'07",
  "\\'08", "\\line ", "\n", "\\'0B", "\\'0C", "\\line ", "\\'0E",  "\\'0F",
  // 10
  "\\'10", "\xE2\x8C\x98", "\xE2\x87\xA7", "\xE2\x8C\xA5",
  "\xE2\x8C\x83", "\\'15", "\\'16", "\\'17",
  "\\'18", "\\'19", "\\'1A", "\\'1B", "\\'1C", "\\'1D", "\\'1E", "\\'1F",
  // 20
  " ", "!", "\"", "#", "$", "%", "&", "\'",
  "(", ")", "*", "+", ",", "-", ".", "/",
  // 30
  "0", "1", "2", "3", "4", "5", "6", "7",
  "8", "9", ":", ";", "<", "=", ">", "?",
  // 40
  "@", "A", "B", "C", "D", "E", "F", "G",
  "H", "I", "J", "K", "L", "M", "N", "O",
  // 50
  "P", "Q", "R", "S", "T", "U", "V", "W",
  "X", "Y", "Z", "[", "\\\\", "]", "^", "_",
  // 60
  "`", "a", "b", "c", "d", "e", "f", "g",
  "h", "i", "j", "k", "l", "m", "n", "o",
  // 70
  "p", "q", "r", "s", "t", "u", "v", "w",
  "x", "y", "z", "{", "|", "}", "~", "\\'7F",
  // 80
  "\\u196A", "\\u197A", "\\u199C", "\\u201E", "\\u209N", "\\u214O", "\\u220U", "\\u225a",
  "\\u224a", "\\u226a", "\\u228a", "\\u227a", "\\u229a", "\\u231c", "\\u233e", "\\u232e",
  // 90
  "\\u234e", "\\u235e", "\\u237i", "\\u236i", "\\u238i", "\\u239i", "\\u241n", "\\u243o",
  "\\u242o", "\\u244o", "\\u246o", "\\u245o", "\\u250u", "\\u249u", "\\u251u", "\\u252u",
  // A0
  "\\u8224?", "\\u176?", "\\u162c", "\\u163?", "\\u167?", "\\u8226?", "\\u182?", "\\u223?",
  "\\u174R", "\\u169C", "\\u8482?", "\\u180?", "\\u168?", "\\u8800?", "\\u198?", "\\u216O",
  // B0
  "\\u8734?", "\\u177?", "\\u8804?", "\\u8805?", "\\u165?", "\\u181?", "\\u8706?", "\\u8721?",
  "\\u8719?", "\\u960?", "\\u8747?", "\\u170?", "\\u186?", "\\u937?", "\\u230?", "\\u248o",
  // C0
  "\\u191?", "\\u161?", "\\u172?", "\\u8730?", "\\u402?", "\\u8776?", "\\u8710?", "\\u171?",
  "\\u187?", "\\u8230?", "\\u160 ", "\\u192A", "\\u195A", "\\u213O", "\\u338?", "\\u339?",
  // D0
  "\\u8211-", "\\u8212-", "\\u8220\"", "\\u8221\"", "\\u8216\'", "\\u8217\'", "\\u247/", "\\u9674?",
  "\\u255y", "\\u376Y", "\\u8260/", "\\u8364?", "\\u8249<", "\\u8250>", "\\u-1279?", "\\u-1278?",
  // E0
  "\\u8225?", "\\u183?", "\\u8218,", "\\u8222?", "\\u8240?", "\\u194A", "\\u202E", "\\u193A",
  "\\u203E", "\\u200E", "\\u205I", "\\u206I", "\\u207I", "\\u204I", "\\u211O", "\\u212O",
  // F0
  "\\u-1793?", "\\u210O", "\\u218U", "\\u219U", "\\u217U", "\\u305i", "\\u710^", "\\u732~",
  "\\u175?", "\\u728?", "\\u729?", "\\u730?", "\\u184?", "\\u733?", "\\u731?", "\\u711?",
};


ResourceFile::DecodedStringSequence ResourceFile::decode_STRN(int16_t id, uint32_t type) {
  return this->decode_STRN(this->get_resource(type, id));
}

ResourceFile::DecodedStringSequence ResourceFile::decode_STRN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_STRN(res->data.data(), res->data.size());
}

ResourceFile::DecodedStringSequence ResourceFile::decode_STRN(const void* vdata, size_t size) {
  StringReader r(vdata, size);
  size_t count = r.get_u16b();

  vector<string> ret;
  while (ret.size() < count) {
    ret.emplace_back(decode_mac_roman(r.readx(r.get_u8())));
  }

  return {ret, r.read(r.remaining())};
}

ResourceFile::DecodedString ResourceFile::decode_STR(int16_t id, uint32_t type) {
  return this->decode_STR(this->get_resource(type, id));
}

ResourceFile::DecodedString ResourceFile::decode_STR(shared_ptr<const Resource> res) {
  return ResourceFile::decode_STR(res->data.data(), res->data.size());
}

ResourceFile::DecodedString ResourceFile::decode_STR(const void* vdata, size_t size) {
  if (size == 0) {
    return {"", ""};
  }

  StringReader r(vdata, size);
  string s = decode_mac_roman(r.readx(r.get_u8()));
  return {move(s), r.read(r.remaining())};
}

string ResourceFile::decode_card(int16_t id, uint32_t type) {
  return this->decode_card(this->get_resource(type, id));
}

string ResourceFile::decode_card(shared_ptr<const Resource> res) {
  return ResourceFile::decode_card(res->data.data(), res->data.size());
}

string ResourceFile::decode_card(const void* vdata, size_t size) {
  if (size == 0) {
    return "";
  }
  StringReader r(vdata, size);
  uint8_t len = r.get_u8();
  return decode_mac_roman(&r.get<char>(true, len), len);
}

string ResourceFile::decode_TEXT(int16_t id, uint32_t type) {
  return this->decode_TEXT(this->get_resource(type, id));
}

string ResourceFile::decode_TEXT(shared_ptr<const Resource> res) {
  return ResourceFile::decode_TEXT(res->data.data(), res->data.size());
}

string ResourceFile::decode_TEXT(const void* data, size_t size) {
  return decode_mac_roman(reinterpret_cast<const char*>(data), size);
}


struct StyleResourceCommand {
  be_uint32_t offset;
  // These two fields seem to scale with size; they might be line/char spacing
  be_uint16_t unknown1;
  be_uint16_t unknown2;
  be_uint16_t font_id;
  be_uint16_t style_flags;
  be_uint16_t size;
  Color color;
} __attribute__((packed));

string ResourceFile::decode_styl(int16_t id, uint32_t type) {
  return this->decode_styl(this->get_resource(type, id));
}

string ResourceFile::decode_styl(shared_ptr<const Resource> res) {
  // Get the text now, so we'll fail early if there's no resource
  string text;
  try {
    text = this->get_resource(RESOURCE_TYPE_TEXT, res->id)->data;
  } catch (const out_of_range&) {
    throw runtime_error("style has no corresponding TEXT");
  }

  if (text.empty()) {
    throw runtime_error("corresponding TEXT resource is empty");
  }

  StringReader r(res->data);
  size_t num_commands = r.get_u16b();
  size_t commands_start_offset = r.where(); // This should always be 2

  string ret = "{\\rtf1\\ansi\n{\\fonttbl";

  // Collect all the fonts and write the font table
  map<uint16_t, uint16_t> font_table;
  for (size_t x = 0; x < num_commands; x++) {
    const auto& cmd = r.get<StyleResourceCommand>();

    size_t font_table_entry = font_table.size();
    if (font_table.emplace(cmd.font_id, font_table_entry).second) {
      const char* font_name = name_for_font_id(cmd.font_id);
      if (font_name == nullptr) {
        // TODO: This is a bad assumption
        font_name = "Helvetica";
      }
      // TODO: We shouldn't necessarily say every font is a swiss font
      ret += string_printf("\\f%zu\\fswiss %s;", font_table_entry, font_name);
    }
  }
  ret += "}\n{\\colortbl";

  // Collect all the colors and write the color table
  map<uint64_t, uint16_t> color_table;
  r.go(commands_start_offset);
  for (size_t x = 0; x < num_commands; x++) {
    const auto& cmd = r.get<StyleResourceCommand>();

    size_t color_table_entry = color_table.size();
    if (color_table.emplace(cmd.color.to_u64(), color_table_entry).second) {
      ret += string_printf("\\red%d\\green%d\\blue%d;",
          cmd.color.r >> 8, cmd.color.g >> 8, cmd.color.b >> 8);
    }
  }
  ret += "}\n";

  // Write the stylized blocks
  r.go(commands_start_offset);
  for (size_t x = 0; x < num_commands; x++) {
    const auto& cmd = r.get<StyleResourceCommand>();

    uint32_t offset = cmd.offset;
    uint32_t end_offset = (x + 1 == num_commands)
        ? text.size()
        : r.get<StyleResourceCommand>(false).offset.load();
    if (offset >= text.size()) {
      throw runtime_error("offset is past end of TEXT resource data");
    }
    if (end_offset <= offset) {
      throw runtime_error("block size is zero or negative");
    }
    string text_block = text.substr(offset, end_offset - offset);

    // TODO: We can produce smaller files by omitting commands for parts of the
    // format that haven't changed
    size_t font_id = font_table.at(cmd.font_id);
    size_t color_id = color_table.at(cmd.color.to_u64());
    ssize_t expansion = 0;
    if (cmd.style_flags & TextStyleFlag::CONDENSED) {
      expansion = -cmd.size / 2;
    } else if (cmd.style_flags & TextStyleFlag::EXTENDED) {
      expansion = cmd.size / 2;
    }
    ret += string_printf("\\f%zu\\%s\\%s\\%s\\%s\\fs%d \\cf%zu \\expan%zd ",
        font_id,
        (cmd.style_flags & TextStyleFlag::BOLD) ? "b" : "b0",
        (cmd.style_flags & TextStyleFlag::ITALIC) ? "i" : "i0",
        (cmd.style_flags & TextStyleFlag::OUTLINE) ? "outl" : "outl0",
        (cmd.style_flags & TextStyleFlag::SHADOW) ? "shad" : "shad0",
        cmd.size * 2,
        color_id,
        expansion);
    if (cmd.style_flags & TextStyleFlag::UNDERLINE) {
      ret += string_printf("\\ul \\ulc%zu ", color_id);
    } else {
      ret += "\\ul0 ";
    }

    for (char ch : text_block) {
      ret += mac_roman_table_rtf[static_cast<uint8_t>(ch)];
    }
  }
  ret += "}";

  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// Font decoding

ResourceFile::DecodedFontResource ResourceFile::decode_FONT(int16_t id, uint32_t type) {
  return this->decode_FONT(this->get_resource(type, id));
}

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
  be_uint16_t type_flags;
  be_uint16_t first_char;
  be_uint16_t last_char;
  be_uint16_t max_width;
  be_int16_t max_kerning;
  be_int16_t descent; // if positive, this is the high word of the width offset table offset
  be_uint16_t rect_width;
  be_uint16_t rect_height; // also bitmap height
  be_uint16_t width_offset_table_offset;
  be_int16_t max_ascent;
  be_int16_t max_descent;
  be_int16_t leading;
  be_uint16_t bitmap_row_width;
  // Variable-length fields follow:
  // - bitmap image table (each aligned to 16-bit boundary)
  // - bitmap location table
  // - width offset table
  // - glyph-width table
  // - image height table
} __attribute__((packed));

ResourceFile::DecodedFontResource ResourceFile::decode_FONT(
    shared_ptr<const Resource> res) {
  StringReader r(res->data.data(), res->data.size());
  const auto& header = r.get<FontResourceHeader>();

  DecodedFontResource ret;
  uint16_t depth_flags = header.type_flags & FontResourceHeader::TypeFlags::BIT_DEPTH_MASK;
  if (depth_flags == FontResourceHeader::TypeFlags::MONOCHROME) {
    ret.source_bit_depth = 1;
  } else if (depth_flags == FontResourceHeader::TypeFlags::BIT_DEPTH_2) {
    ret.source_bit_depth = 2;
  } else if (depth_flags == FontResourceHeader::TypeFlags::BIT_DEPTH_4) {
    ret.source_bit_depth = 4;
  } else { // depth_flags == FontResourceHeader::TypeFlags::BIT_DEPTH_8
    ret.source_bit_depth = 8;
  }
  ret.is_dynamic = !!(header.type_flags & FontResourceHeader::TypeFlags::IS_DYNAMIC);
  ret.has_non_black_colors = !!(header.type_flags & FontResourceHeader::TypeFlags::HAS_NON_BLACK_COLORS);
  ret.fixed_width = !!(header.type_flags & FontResourceHeader::TypeFlags::FIXED_WIDTH);
  ret.first_char = header.first_char;
  ret.last_char = header.last_char;
  ret.max_width = header.max_width;
  ret.max_kerning = header.max_kerning;
  ret.rect_width = header.rect_width;
  ret.rect_height = header.rect_height;
  ret.max_ascent = header.max_ascent;
  ret.max_descent = header.max_descent;
  ret.leading = header.leading;

  if (header.type_flags & FontResourceHeader::TypeFlags::HAS_COLOR_TABLE) {
    ret.color_table = this->decode_fctb(res->id);
  }

  Image glyphs_bitmap;
  string bitmap_data = r.readx(header.bitmap_row_width * header.rect_height * 2);
  if (ret.source_bit_depth == 1) {
    glyphs_bitmap = decode_monochrome_image(
        bitmap_data.data(), bitmap_data.size(), header.bitmap_row_width * 16,
        header.rect_height);
  } else if (ret.source_bit_depth == 2) {
    throw runtime_error("2-bit font bitmaps are not implemented");
  } else if (ret.source_bit_depth == 4) {
    throw runtime_error("4-bit font bitmaps are not implemented");
  } else if (ret.source_bit_depth == 8) {
    throw runtime_error("8-bit font bitmaps are not implemented");
  } else {
    throw logic_error("unknown font bit depth");
  }

  // +2 here because last_char is inclusive, and there's the missing glyph at
  // the end as well
  size_t num_glyphs = (header.last_char + 2) - header.first_char;
  ret.glyphs.resize(num_glyphs);

  uint16_t glyph_start_x = r.get_u16b();
  for (uint32_t ch = header.first_char; ch < header.first_char + num_glyphs; ch++) {
    // TODO: clean this up a little to not use a prev variable
    uint16_t next_glyph_start_x = r.get_u16b();
    auto& glyph = ret.glyphs.at(ch - header.first_char);
    glyph.ch = ch;
    glyph.bitmap_offset = glyph_start_x;
    glyph.bitmap_width = next_glyph_start_x - glyph_start_x;
    glyph_start_x = next_glyph_start_x;
  }
  if (ret.glyphs.empty()) {
    throw runtime_error("no glyphs in font");
  }

  for (uint32_t ch = header.first_char; ch < header.first_char + num_glyphs; ch++) {
    auto& glyph = ret.glyphs.at(ch - header.first_char);
    glyph.offset = r.get_s8();
    glyph.width = r.get_u8();
    if (glyph.offset == -1 && glyph.width == 0xFF) {
      continue;
    }

    // TODO: handle negative offsets here
    if (glyph.offset < 0) {
      throw runtime_error("glyphs with negative offsets are not implemented");
    }

    if (glyph.width > 0) {
      glyph.img = Image(glyph.width + glyph.offset, header.rect_height);
      glyph.img.clear(0xE0E0E0FF);
    }
    if (glyph.bitmap_width > 0) {
      glyph.img.blit(glyphs_bitmap, glyph.offset, 0, glyph.bitmap_width, header.rect_height, glyph.bitmap_offset, 0);
    }
  }

  ret.missing_glyph = move(ret.glyphs.back());
  ret.glyphs.pop_back();

  return ret;
}

ResourceFile::DecodedFontResource ResourceFile::decode_NFNT(int16_t id, uint32_t type) {
  return this->decode_FONT(this->get_resource(type, id));
}

ResourceFile::DecodedFontResource ResourceFile::decode_NFNT(shared_ptr<const Resource> res) {
  return this->decode_FONT(res);
}

vector<ResourceFile::DecodedFontInfo> ResourceFile::decode_finf(int16_t id, uint32_t type) {
  return this->decode_finf(this->get_resource(type, id));
}

vector<ResourceFile::DecodedFontInfo> ResourceFile::decode_finf(shared_ptr<const Resource> res) {
  return ResourceFile::decode_finf(res->data.data(), res->data.size());
}

vector<ResourceFile::DecodedFontInfo> ResourceFile::decode_finf(const void* data, size_t size) {
  if (size == 0) {
    return {};
  }

  StringReader r(data, size);
  size_t count = r.get_u16b();

  vector<DecodedFontInfo> ret;
  for (size_t x = 0; x < count; x++) {
    auto& finf = ret.emplace_back();
    finf.font_id = r.get_u16b();
    finf.style_flags = r.get_u16b();
    finf.size = r.get_u16b();
  }
  return ret;
}

ResourceFile::DecodedROMOverridesResource ResourceFile::decode_ROvN(int16_t id, uint32_t type) {
  return this->decode_ROvN(this->get_resource(type, id));
}

ResourceFile::DecodedROMOverridesResource ResourceFile::decode_ROvN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_ROvN(res->data.data(), res->data.size());
}

ResourceFile::DecodedROMOverridesResource ResourceFile::decode_ROvN(const void* data, size_t size) {
  if (size == 0) {
    return {};
  }

  StringReader r(data, size);
  DecodedROMOverridesResource ret;
  ret.rom_version = r.get_u16b();
  size_t count = r.get_u16b();
  for (size_t x = 0; x < count; x++) {
    ret.overrides.emplace_back(r.get<DecodedROMOverride>());
  }
  return ret;
}
