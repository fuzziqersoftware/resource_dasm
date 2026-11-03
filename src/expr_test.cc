#include <phosg/Strings.hh>

#include "Emulators/Expression.hh"

int main(int argc, char** argv) {
  if (argc != 2) {
    phosg::fwrite_fmt(stderr, "Usage: expr_test <expr>\n");
    return -1;
  }

  auto expr = ResourceDASM::Expression::Node::parse(argv[1]);
  phosg::fwrite_fmt(stdout, "Parsed as: {}\n", expr->str());

  return 0;
}
