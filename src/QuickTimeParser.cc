#include "QuickTimeParser.hh"
#include "TextCodecs.hh"

#include <phosg/Strings.hh>

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

void Parser::parse_atom_list(phosg::StringReader r, ssize_t expected_child_count) {
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
    this->handle_atom(header.type, r.getv(header.size - sizeof(AtomHeader)), header.size - sizeof(AtomHeader));
    this->current_path.pop_back();
    atoms_parsed++;
  }
  if ((expected_child_count >= 0) && (atoms_parsed != expected_child_count)) {
    this->throw_parse_error("Incorrect child count (expected {}, received {})", expected_child_count, atoms_parsed);
  }
}

} // namespace QuickTime
} // namespace ResourceDASM
