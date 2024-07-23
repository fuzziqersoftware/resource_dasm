#include "Formats.hh"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>
#include <vector>

#include "../ResourceFile.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {

struct Entry {
  enum class Type : uint32_t {
    DATA_FORK = 1,
    RESOURCE_FORK = 2,
    FILE_NAME = 3,
    COMMENT = 4, // From Finder's Get Info window, presumably
    BW_ICON = 5,
    COLOR_ICON = 6,
    FILE_INFO = 7,
    DATES = 8,
    FINDER_INFO = 9,
    MAC_FILE_INFO = 10,
    PRODOS_FILE_INFO = 11,
    MSDOS_FILE_INFO = 12,
    AFP_SHORT_NAME = 13,
    AFP_FILE_INFO = 14,
    AFP_DIRECTORY_ID = 15,
  };

  be_uint32_t be_type;
  be_uint32_t offset;
  be_uint32_t size;

  inline Type type() const {
    return static_cast<Type>(this->be_type.load());
  }
} __attribute__((packed));

struct Header {
  be_uint32_t signature; // 00051600 = AppleSingle, 00051607 = AppleDouble
  be_uint32_t version; // 00010000 or 00020000, apparently
  char home_filesystem[0x10]; // Unused in version 00020000?
  be_uint16_t num_entries;
  // Variable-length field:
  // Entry entries[num_entries];
} __attribute__((packed));

bool maybe_applesingle_appledouble(const StringReader& r) {
  try {
    const auto& header = r.pget<Header>(0);
    return ((header.signature == 0x00051600 || header.signature == 0x00051607) &&
        (header.version == 0x00010000 || header.version == 0x00020000));
  } catch (const out_of_range&) {
    return false;
  }
}

DecodedAppleSingle parse_applesingle_appledouble(StringReader& r) {
  const auto& header = r.get<Header>();
  if (header.signature != 0x00051600 && header.signature != 0x00051607) {
    throw runtime_error("file is not AppleSingle or AppleDouble");
  }
  if (header.version != 0x00010000 && header.version != 0x00020000) {
    throw runtime_error("unknown AppleSingle/AppleDouble version");
  }

  DecodedAppleSingle ret;
  for (size_t z = 0; z < header.num_entries; z++) {
    const auto& entry = r.get<Entry>();
    switch (entry.type()) {
      case Entry::Type::DATA_FORK:
        ret.data_fork = r.preadx(entry.offset, entry.size);
        break;
      case Entry::Type::RESOURCE_FORK: {
        auto sub_r = r.subx(entry.offset, entry.size);
        ret.resource_fork = parse_resource_fork(sub_r);
        break;
      }
      case Entry::Type::FILE_NAME:
        ret.file_name = r.preadx(entry.offset, entry.size);
        break;
      case Entry::Type::COMMENT:
        ret.comment = r.preadx(entry.offset, entry.size);
        break;
      case Entry::Type::BW_ICON:
        // TODO: Figure out the format of this and convert it to an Image
        ret.bw_icon = r.preadx(entry.offset, entry.size);
        break;
      case Entry::Type::COLOR_ICON:
        // TODO: Figure out the format of this and convert it to an Image
        ret.color_icon = r.preadx(entry.offset, entry.size);
        break;
      case Entry::Type::FILE_INFO:
        // TODO: Figure out the format of this and parse it
        ret.file_info = r.preadx(entry.offset, entry.size);
        break;
      case Entry::Type::DATES:
        // TODO: Parse this like so (according to appledouble.h):
        // // File Dates are stored as the # of seconds before or after
        // // 12am Jan 1, 2000 GMT.  The default value is 0x80000000.
        // struct MacTimes {
        //   uint32_t  creation;
        //   uint32_t  modification;
        //   uint32_t  backup;
        //   uint32_t  access;
        // };
        ret.dates = r.preadx(entry.offset, entry.size);
        break;
      case Entry::Type::FINDER_INFO:
        // TODO: Figure out the format of this and parse it. appledouble.h says:
        // Finder Information is two 16 byte quantities.
        // Newly created files have all 0's in both entries.
        ret.finder_info = r.preadx(entry.offset, entry.size);
        break;
      case Entry::Type::MAC_FILE_INFO:
        // TODO: Figure out the format of this and parse it. appledouble.h says:
        // Macintosh File Info entry (10) a 32 bit bitmask.
        ret.mac_file_info = r.preadx(entry.offset, entry.size);
        break;
      case Entry::Type::PRODOS_FILE_INFO:
        // TODO: Figure out the format of this and parse it
        ret.prodos_file_info = r.preadx(entry.offset, entry.size);
        break;
      case Entry::Type::MSDOS_FILE_INFO:
        // TODO: Figure out the format of this and parse it
        ret.msdos_file_info = r.preadx(entry.offset, entry.size);
        break;
      case Entry::Type::AFP_SHORT_NAME:
        ret.afp_short_name = r.preadx(entry.offset, entry.size);
        break;
      case Entry::Type::AFP_FILE_INFO:
        // TODO: Figure out the format of this and parse it
        ret.afp_file_info = r.preadx(entry.offset, entry.size);
        break;
      case Entry::Type::AFP_DIRECTORY_ID:
        // TODO: Figure out the format of this and parse it
        ret.afp_directory_id = r.preadx(entry.offset, entry.size);
        break;
    }
  }
  return ret;
}

DecodedAppleSingle parse_applesingle_appledouble(const string& data) {
  StringReader r(data.data(), data.size());
  return parse_applesingle_appledouble(r);
}

ResourceFile parse_applesingle_appledouble_resource_fork(const string& data) {
  auto parsed = parse_applesingle_appledouble(data);
  return std::move(parsed.resource_fork);
}

string DecodedAppleSingle::serialize() const {
  size_t offset = 0;
  vector<pair<Entry, const string*>> entries;
  auto add_entry = [&](Entry::Type type, const string& data) {
    if (!data.empty()) {
      entries.emplace_back(make_pair(
          Entry{.be_type = static_cast<uint32_t>(type), .offset = offset, .size = data.size()},
          &data));
      offset += data.size();
    }
  };

  string rf_data;
  if (!this->resource_fork.empty()) {
    rf_data = serialize_resource_fork(this->resource_fork);
  }

  add_entry(Entry::Type::DATA_FORK, this->data_fork);
  add_entry(Entry::Type::RESOURCE_FORK, rf_data);
  add_entry(Entry::Type::FILE_NAME, this->file_name);
  add_entry(Entry::Type::COMMENT, this->comment);
  add_entry(Entry::Type::BW_ICON, this->bw_icon);
  add_entry(Entry::Type::COLOR_ICON, this->color_icon);
  add_entry(Entry::Type::FILE_INFO, this->file_info);
  add_entry(Entry::Type::DATES, this->dates);
  add_entry(Entry::Type::FINDER_INFO, this->finder_info);
  add_entry(Entry::Type::MAC_FILE_INFO, this->mac_file_info);
  add_entry(Entry::Type::PRODOS_FILE_INFO, this->prodos_file_info);
  add_entry(Entry::Type::MSDOS_FILE_INFO, this->msdos_file_info);
  add_entry(Entry::Type::AFP_SHORT_NAME, this->afp_short_name);
  add_entry(Entry::Type::AFP_FILE_INFO, this->afp_file_info);
  add_entry(Entry::Type::AFP_DIRECTORY_ID, this->afp_directory_id);

  size_t header_size = sizeof(Header) + entries.size() * sizeof(Entry);
  for (auto& it : entries) {
    it.first.offset += header_size;
  }

  StringWriter w;
  {
    Header header;
    header.signature = 0x00051600;
    header.version = 0x00020000;
    memset(header.home_filesystem, 0x00, sizeof(header.home_filesystem));
    header.num_entries = entries.size();
    w.put<Header>(header);
  }
  for (const auto& it : entries) {
    w.put<Entry>(it.first);
  }
  for (const auto& it : entries) {
    w.write(*it.second);
  }
  return std::move(w.str());
}

} // namespace ResourceDASM
