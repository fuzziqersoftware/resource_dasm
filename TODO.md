# Things to do

## resource_dasm

### Code style

* Use StringReader in most places where we currently do pointer arithmetic
* Create some abstraction or consistent pattern for generating outputs based on multiple input resources (for example, generating the SONG JSON template or a .icns file from ICN#/icl4/icl8/etc.)
* Consider using phosg log functions instead of fprintf(stderr, ...) everywhere

### Compression

* Are there any more ncmps in existence? resource_dasm currently only has System 0 and 2.

### Unimplemented/incomplete resource formats

#### Documented

* FOND
* DITL (IM: Toolbox Essentials, 6-153)
* ictb (IM: Toolbox Essentials, 6-159)
* mctb (IM: Toolbox Essentials, 3-156; there's a TMPL for this too)
* MOOV/MooV/moov
* ppat type 2 is not monochrome; it's RGB (see QuickDraw docs). Unclear if these are ever stored in resource forks though
* Should we add icm and kcs icons to .icns output files too? (kcs should be added as ics types, which could conflict)

#### Undocumented

* CREL: looks like another form of CODE relocation info; there appear to be at least two different formats (see Realmz 5.1.6 vs. After Dark)
  * Another example: SimCity 2000; here it looks like just a list of words pointing to offsets within the corresponding CODE
* DATA and DREL: some apps use these to initialize the A5 world
* infa: TMPL from ResEdit appears incorrect
* POST: TMPL from ResEdit appears incorrect
* Tune: ScummVM appears to contain an implementation of Tune resources (https://github.com/scummvm/scummvm/blob/master/audio/midiparser_qt.cpp), but it seems less complex than what resource_dasm does; though resource_dasm's implementation doesn't work well. Investigate this.

#### Custom formats used in multiple games/apps

* 'ADCR' encoding (used for some CODEs, including CODE 0)
* 'ajcp' compression/encryption (used in Zen and Prince of Darkness... is it a FutureBASIC thing?)
* hypercard_dasm fails horrendously on Spelunx and its secondary stacks (probably the stack format is intermediate)

#### Custom formats used in a single game/app

* Adventures of Billy: IMAG (header starts with 'AGFX' and 'ACPI' tags; Googling these is not helpful so probably will need some disassembling + delabeling)
* Avara: HSND (has header TMPL, but also uses a Huffman compression scheme with a fairly complex decoder. Avara is now open-source and the decoder implementation is at https://github.com/avaraline/Avara/tree/main/src/util/huffman)
* Bugs Bannis: Levels are fixed-size tilemaps, but the tile numbers don't align with the tilesheet PICT. Figure this out, or complete the remap table in bugs_bannis_render.
* DeadEnd: SNGV (seems to be a very simple sequence format; it has a TMPL even)
* Flashback PPSS: figure out what subset of the clut to use for each image set
* Oh No! More Lemmings: there are some minor offset issues still (see uppermost left horizontal pipe on Dangerzone)
* Mario Teaches Typing: Img, Pak (sprites, probably)

## m68kexec

* Implement options like --load-pe but for PEFF, REL, and classic 68k executables
  * These are all relocated at load time; add a way to specify a preferred address in the CLI option

## m68kdasm

* Implement x86 assembler
* Implement m68k assembler
* Fix PPC assembler/disassembler mismatch errors. Many of these are due to unused bits in the opcode (which are not represented in the disassembled output, so are re-assembled as zeroes).
  * 40000000 => 66781184 (0x3FB0000) mismatches
  * 4C000000 => 720352 (0xAFDE0) mismatches
  * 7C000000 => 4160136 (0x3F7A88) mismatches
  * EC000000 => 12316672 (0xBBF000) mismatches
  * FC000000 => 13252472 (0xCA3778) mismatches
* Add export symbols as labels in PEFF disassembly (this is tricky because they often point to transition vectors which are relocated at load time)

## realmz_dasm

* Rogue encounter and item IDs (and result codes) appear incorrect in complex encounter disassembly
* Custom choice option strings seem wrong in script disassembly
* Localize things better in script disassembly (e.g. paste XAP disassembly into places where XAPs are referenced; paste complex encounter disassembly into complex_encounter opcodes, etc.)
