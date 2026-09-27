#include <stdio.h>

#include <phosg/Arguments.hh>

#include "TrapInfo.hh"

int main(int argc, char** argv) {
  phosg::Arguments args(argv + 1, argc - 1);

  const auto& show_protos = args.get_multi<std::string>("show-prototype");
  if (!show_protos.empty()) {
    std::unordered_map<std::string, std::string> results;
    for (const auto& name : show_protos) {
      results.emplace(name, "");
    }
    for (const auto& ti : ResourceDASM::all_68k_traps()) {
      if (results.contains(ti.name)) {
        results[ti.name] = ti.str(false, false);
      }
    }
    for (const auto& name : show_protos) {
      const auto& proto = results.at(name);
      if (proto.empty()) {
        phosg::fwrite_fmt(stdout, "/* {} not found */\n", name);
      } else {
        phosg::fwrite_fmt(stdout, "{}\n", proto);
      }
    }
  } else {
    ResourceDASM::assert_trap_infos_ordered();
    fprintf(stderr, "-- trap info ok\n");
  }

  return 0;
}
