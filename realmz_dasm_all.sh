#!/bin/bash

set -e
set -x

INPUT_DIR="$1"

OUTPUT_DIR=realmz_dasm_all.out

mkdir -p realmz_dasm_all.out
./realmz_dasm "$INPUT_DIR/Data Files" "$OUTPUT_DIR/Global Data"

ls "$INPUT_DIR/Scenarios" | while read scenario
do
  if [ -d "$INPUT_DIR/Scenarios/$scenario" ]
  then
    rm -rf "$OUTPUT_DIR/$scenario"
    mkdir -p "$OUTPUT_DIR/$scenario"
    ./realmz_dasm "$INPUT_DIR/Data Files" "$INPUT_DIR/Scenarios/$scenario" "$OUTPUT_DIR/$scenario"
  fi
done
