# Things to do

## resource_dasm

### Bugs

* Sometimes WINDs fail to decode with the system template with the error "skip beyond end of string"

### Code style

* Use StringReader in most places where we currently do pointer arithmetic

### Unimplemented/incomplete resource formats

#### Documented

* FOND
* ICN#/ics# (these can technically have more than a single monochrome image and mask image in them, but we currently don't support this)
* DITL (IM: Toolbox Essentials, 6-153)
* ictb (IM: Toolbox Essentials, 6-159)
* mctb (IM: Toolbox Essentials, 3-156; there's a TMPL for this too)
* MOOV/MooV/moov
* Unimplemented equivalences from ResEdit RMAPs:
  * PREC 0, 1 => PRC0, PREC 3, 4 => PRC3
  * INTL 0 => itl0, INTL 1 => itl1
* ppat type 2 is not monochrome; it's RGB (see QuickDraw docs). Unclear if these are ever stored in resource forks though

#### Undocumented

* CREL: looks like another form of CODE relocation info; there appear to be at least two different formats (see Realmz 5.1.6 vs. After Dark)
  * Another example: SimCity 2000; here it looks like just a list of words pointing to offsets within the corresponding CODE
* DATA and DREL: some apps use these to initialize the A5 world
* infa: TMPL from ResEdit appears incorrect
* POST: TMPL from ResEdit appears incorrect
* Tune: ScummVM appears to contain an implementation of Tune resources (https://github.com/scummvm/scummvm/blob/master/audio/midiparser_qt.cpp), but it seems less complex than what resource_dasm does. Investigate this.

#### Custom formats used in multiple games/apps

* 'ADCR' encoding (used for some CODEs, including CODE 0)
* 'ajcp' compression/encryption (used in Zen and Prince of Darkness... is it a FutureBASIC thing?)
* hypercard_dasm fails horrendously on Spelunx and its secondary stacks (probably the stack format is intermediate)

#### Custom formats used in a single game/app

* Avara: HSND (has TMPL, but appears to also have undocumented compression)
* DeadEnd: SNGV (seems to be a very simple sequence format; it has a TMPL even)
* Factory: 1img, 4img, 8img (these look like just fixed-size uncompressed images; find the right clut and render them)
* Oh No! More Lemmings: there are some minor offset issues still (see uppermost left horizontal pipe on Dangerzone)
* Mario Teaches Typing: Img, Pak (sprites, probably)
* Spectre Supreme: shap (these might be 3D models)

## m68kexec

* Implement options like --load-pe but for PEFF, REL, and classic 68k executables

## realmz_dasm

* Rogue encounter and item IDs (and result codes) appear incorrect in complex encounter disassembly
* Custom choice option strings seem wrong in script disassembly
* Localize things better in script disassembly (e.g. paste XAP disassembly into places where XAPs are referenced; paste complex encounter disassembly into complex_encounter opcodes, etc.)
