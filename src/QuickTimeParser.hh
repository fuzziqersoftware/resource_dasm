#pragma once

#include <phosg/Strings.hh>
#include <set>

#include "ResourceFormats.hh"
#include "ResourceTypes.hh"
#include "TextCodecs.hh"

namespace ResourceDASM {
namespace QuickTime {

struct AtomHeader {
  /* 00 */ phosg::be_uint32_t size;
  /* 04 */ phosg::be_uint32_t type;
  /* 08 */
} __attribute__((packed));

class Parser {
public:
  Parser() = default;
  virtual ~Parser() = default;

  inline void parse(std::string_view data, ssize_t expected_child_count = 1) {
    this->parse_atom_list(phosg::StringReader(data), expected_child_count);
  }
  inline void parse(const void* data, size_t size, ssize_t expected_child_count = 1) {
    this->parse_atom_list(phosg::StringReader(data, size), expected_child_count);
  }
  inline void parse(phosg::StringReader& r, ssize_t expected_child_count = 1) {
    this->parse_atom_list(r, expected_child_count);
  }

protected:
  struct AtomPathNode {
    uint32_t type;
    uint32_t size;
    size_t offset;
    std::string str() const;
  };
  std::vector<AtomPathNode> current_path;

  std::string render_current_path() const;

  template <typename T>
  const T& get_fixed_atom(phosg::StringReader& r) const {
    if (r.remaining() != sizeof(T)) {
      this->throw_parse_error("Atom size is incorrect");
    }
    return r.get<T>();
  }

  template <typename T = std::runtime_error, typename... ArgTs>
  [[noreturn]] void throw_parse_error(std::format_string<ArgTs...> fmt, ArgTs&&... args) const {
    throw T(std::format("({}) ", this->render_current_path()) +
        std::format(std::forward<std::format_string<ArgTs...>>(fmt), std::forward<ArgTs>(args)...));
  }

  bool is_within_atom(uint32_t type) const;
  void parse_atom_list(
      phosg::StringReader r, ssize_t expected_child_count = -1, std::set<uint32_t> required_children = {});

  virtual void handle_atom(uint32_t type, phosg::StringReader& r) = 0;
};

} // namespace QuickTime
} // namespace ResourceDASM
