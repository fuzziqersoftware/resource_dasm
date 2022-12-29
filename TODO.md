# Things to do

## resource_dasm

* Build a regression testing framework with test cases for every resource type and every known decompressor

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

* infa: TMPL from ResEdit appears incorrect
* POST: TMPL from ResEdit appears incorrect
* Tune: ScummVM appears to contain an implementation of Tune resources (https://github.com/scummvm/scummvm/blob/master/audio/midiparser_qt.cpp), but it seems less complex than what resource_dasm does; though resource_dasm's implementation doesn't work well. Investigate this.


##### CREL/DATA

* CREL: looks like another form of CODE relocation info; there appear to be at least two different formats (see Realmz 5.1.6 vs. After Dark)
  * Another example: SimCity 2000; here it looks like just a list of words pointing to offsets within the corresponding CODE
* DATA and DREL: some apps use these to initialize the A5 world

Symantec's (THINK C, THINK Pascal) linker creates at least some of these: https://github.com/ksherlock/mpw/wiki/Symantec-Far-Model-CODE-Resources

> Q: I am writing a virus scanning program and I need to examine code resources of an application to verify that they are valid. What information does the Symantec Linker place in the first two bytes of the code resource?
> 
> A: For all CODE segments besides CODE 0, there is a code segment header. The THINK Linkers use the upper bit of this header to indicate a model Far CODE segment. The runtime loader resides in CODE 1 of the application and is the first piece of code executed. The loader loads and initializes the DATA and STRS, installs hooks for _LoadSeg, _UnloadSeg, and _ExitToShell traps, and calls the main program.
> 
> If the code is using a far model, the _LoadSeg and _UnloadSeg bottlenecks completely replace the standard segment loader. The standard 4-byte CODE segment header is interpreted differently to accommodate the larger jump table, so it is incompatible with the ROM segment loader. The header has the following format:
> 
> ```
> |15 |14                           0|
> |----------------------------------|
> |R  |Index of 1st Jump Table Entry | 
> |F  |Number of Jump Table Entries  |
> |----------------------------------| 
> ```
> The R bit indicates that the segment has relocations which must be applied at runtime. These are stored in a CREL resource with the same resource ID as the CODE segment. The F bit is used to distinguish a far header from the standard header.
> 
> Be aware that this format is different from the header that MPW and Metrowerks use as well as the CFM-68K header format.

Possible decoder for CREL, DATA and ZERO: https://github.com/ubuntor/m68k_mac_reversing_tools/blob/main/dump.py


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
* mondoBlobboDemo23: graphics seem to have a simple format with row offsets & custom bytewise compression; use vrfs_dump to get the files
* Oh No! More Lemmings: there are some minor offset issues still (see uppermost left horizontal pipe on Dangerzone)

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

## icon_unarchiver

* Support more icon archive formats, e.g. from the various tools the IconFactory has created over the years.
