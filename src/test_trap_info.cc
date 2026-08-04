#include <stdio.h>

#include "TrapInfo.hh"

int main(int, char**) {
  ResourceDASM::assert_trap_infos_ordered();
  fprintf(stderr, "-- trap info ok\n");
  return 0;
}
