#!/bin/bash

set -e

OUTPUT_DIR=realmz_dasm_all.out

mkdir -p realmz_dasm_all.out
./realmz_dasm "Data Files" "$OUTPUT_DIR/Global Data"

ls Scenarios | while read scenario
do
  if [ -d "Scenarios/$scenario" ]
  then
    rm -rf "$OUTPUT_DIR/$scenario"
    mkdir -p "$OUTPUT_DIR/$scenario"
    ./realmz_dasm "Data Files" "Scenarios/$scenario" "$OUTPUT_DIR/$scenario"
  fi
done
