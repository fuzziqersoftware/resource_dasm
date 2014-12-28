#!/bin/bash

set -e

mkdir -p Disassembly
./realmz_dasm "Data Files" "Disassembly/Global Data"

ls Scenarios | while read scenario
do
  if [ -d "Scenarios/$scenario" ]
  then
    rm -rf "Disassembly/$scenario"
    mkdir -p "Disassembly/$scenario"
    ./realmz_dasm "Data Files" "Scenarios/$scenario" "Disassembly/$scenario"
  fi
done
