#include "QuickTimeParser.hh"

#include <phosg/Strings.hh>

#include "TextCodecs.hh"

namespace ResourceDASM {
namespace QuickTime {

std::string Parser::AtomPathNode::str() const {
  return std::format("{:X}:{}:{:X}", this->offset, string_for_resource_type(this->type), this->size);
}

std::string Parser::render_current_path() const {
  std::string ret;
  for (const auto& node : this->current_path) {
    if (!ret.empty()) {
      ret.push_back(',');
    }
    ret += node.str();
  }
  return ret;
}

bool Parser::is_within_atom(uint32_t type) const {
  for (auto it = this->current_path.rbegin(); it != this->current_path.rend(); it++) {
    if (it->type == type) {
      return true;
    }
  }
  return false;
}

void Parser::parse_atom_list(
    phosg::StringReader r, ssize_t expected_child_count, std::set<uint32_t> required_children) {
  uint32_t base_offset = this->current_path.empty() ? 0 : (this->current_path.back().offset + sizeof(AtomHeader));
  ssize_t atoms_parsed = 0;
  while (!r.eof()) {
    if ((expected_child_count >= 0) && (atoms_parsed >= expected_child_count)) {
      this->throw_parse_error("Excess data in atom after {} children", this->render_current_path(), atoms_parsed);
    }
    const auto& header = r.get<AtomHeader>();
    this->current_path.emplace_back(AtomPathNode{header.type, header.size, base_offset + r.where() - sizeof(AtomHeader)});
    if (header.size < sizeof(AtomHeader)) {
      this->throw_parse_error("Invalid atom header size ({})", header.size);
    }
    auto data_r = r.extract(header.size - sizeof(AtomHeader));
    this->handle_atom(header.type, data_r);
    if (!data_r.eof()) {
      this->throw_parse_error("Some atom data was not parsed (parsed 0x{:X} bytes, received 0x{:X} bytes)",
          data_r.where(), data_r.size());
    }
    this->current_path.pop_back();
    required_children.erase(header.type);
    atoms_parsed++;
  }
  if ((expected_child_count >= 0) && (atoms_parsed != expected_child_count)) {
    this->throw_parse_error("Incorrect child count (expected {}, received {})", expected_child_count, atoms_parsed);
  }
  if (!required_children.empty()) {
    std::string missing_atoms;
    for (uint32_t type : required_children) {
      if (!missing_atoms.empty()) {
        missing_atoms += ", ";
      }
      missing_atoms += string_for_resource_type(type);
    }
    this->throw_parse_error("Some required children are missing: {}", missing_atoms);
  }
}

} // namespace QuickTime
} // namespace ResourceDASM
