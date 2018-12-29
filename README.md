# realmz_dasm

This project contains tools I wrote for reverse-engineering classic Mac OS games.

The most general of these is resource_dasm, which disassembles the resource fork of any classic Mac OS file, including applications.

There are several more specific files for specific games:
- realmz_dasm, a disassembler for Realmz scenarios
- render_infotron_levels, a map generator for Infotron levels
- dc_dasm, a disassembler for Dark Castle data files
- sc2k_decode_sprite, a disassembler for SimCity 2000 sprite resources

## Building

- Install Netpbm (http://netpbm.sourceforge.net/).
- Build and install phosg (https://github.com/fuzziqersoftware/phosg).
- Run `make`.

## The tools

### resource_dasm

resource_dasm is a disassembler for classic Mac OS resource forks. It extracts resources from the resource fork of any file and converts many classic Mac OS resource formats (icons, pictures, sounds, etc.) into modern formats. Specifically:
- Converts cicn, ICON, SICN, ics#/4/8, icl4/8, CURS, crsr, PAT, PAT#, ppat, and PICT resources to uncompressed bmp files. May fail on icons with nonstandard sizes or formats, or PICTs containing unusual opcodes.
- Converts snd resources to uncompressed wav files.
- Converts TEXT resources to txt files with Unix line endings.

resource_dasm attempts to transparently decompress resources that are stored in compressed formats. Current support for decompression is incomplete; it depends on an embedded MC68K emulator that doesn't (yet) implement the entire CPU. If you use resource_dasm and it fails on a compressed resource, send me the file and I'll add support for it.

Run resource_dasm without any arguments for usage information.

### realmz_dasm

Realmz is a fantasy role-playing game for Windows and classic Mac OS. realmz_dasm is a disassembler for Realmz scenarios; it produces annotated maps of all land and dungeon levels, as well as descriptions of all events and encounters that may occur in the scenario.

To use realmz_dasm, put realmz_dasm and disassemble_all.sh in the same directory as Realmz, and run disassemble_all.sh from there. This will produce a directory named "Disassembly" containing some very large image files (maps of all the land and dungeon levels), the scenario script, and the resources contained in each scenario (icons, sounds, text).

### render_infotron_levels

Infotron is a puzzle game very much like Supaplex (and Move Blocks and Eat Stuff). render_infotron_levels decodes the levels from the game's resource fork and draws maps of them. Just put render_infotron_levels in the "Info Datafiles" folder and run it from there.

### dc_dasm

Dark Castle is a 2D platformer. dc_dasm extracts the contents of the DC Data file and decodes the contained sounds and images. Run it from the folder containing the DC Data file.

### sc2k_decode_sprite

SimCity 2000 is a resource-management game about building cities. sc2k_decode_sprite converts the SPRT resources included in the game into uncompressed bmp files. Just give it a SPRT file and a pltt file (both produced by resource_dasm) and it will do the rest.