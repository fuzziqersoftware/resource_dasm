# realmz_dasm

This project contains tools I wrote for reverse-engineering classic Mac OS games.

The most general of these is resource_dasm, which reads and converts resources from the resource fork of any classic Mac OS file, including applications.

There are several programs for working with specific games:
- realmz_dasm, a disassembler and map generator for Realmz scenarios
- render_infotron_levels, a map generator for Infotron levels
- dc_dasm, a disassembler for Dark Castle data files
- sc2k_decode_sprite, a renderer for SimCity 2000 sprite resources

## Building

- Install Netpbm (http://netpbm.sourceforge.net/).
- Build and install phosg (https://github.com/fuzziqersoftware/phosg).
- Run `make`.

## The tools

### resource_dasm

resource_dasm is a disassembler for classic Mac OS resource forks. It extracts resources from the resource fork of any file and converts many classic Mac OS resource formats (pictures, sounds, text, etc.) into modern formats. Specifically:

    Type -- Output format      -- Notes
    -----------------------------------
    cicn -- 32-bit BMP         -- *1
    clut -- 24-bit BMP         --
    crsr -- 32-bit BMP         -- *1 *5
    CURS -- 32-bit BMP         -- *5
    icl4 -- 24/32-bit BMP      -- *4
    icl8 -- 24/32-bit BMP      -- *4
    ICN# -- 32-bit BMP         --
    ICON -- 24-bit BMP         --
    icns -- Icon images (icns) --
    ics# -- 32-bit BMP         --
    ics4 -- 24/32-bit BMP      -- *3
    ics8 -- 24/32-bit BMP      -- *3
    MIDI -- MIDI sequence      --
    Midi -- MIDI sequence      --
    midi -- MIDI sequence      --
    MOOV -- QuickTime Movie    --
    MooV -- QuickTime Movie    --
    moov -- QuickTime Movie    --
    PAT  -- 24-bit BMP         -- *6
    PAT# -- 24-bit BMP         -- *7
    PICT -- 24-bit BMP         -- *A
    pltt -- 24-bit BMP         --
    ppat -- 24-bit BMP         -- *8
    ppt# -- 24-bit BMP         -- *9
    SICN -- 24-bit BMP         -- *2
    snd  -- WAV                -- *E
    SONG -- smssynth JSON      -- *F
    STR  -- Plain text         -- *B
    STR# -- Plain text         -- *B *C
    styl -- RTF                -- *D
    TEXT -- Plain text         -- *B
    Tune -- MIDI sequence      -- *G

    Notes:
    *1 -- Produces two images (one color, one monochrome).
    *2 -- Produces one image for each icon in the resource.
    *3 -- If a corresponding ics# resource exists, produces a 32-bit BMP;
          otherwise, produces a 24-bit BMP with no alpha channel.
    *3 -- If a corresponding ICN# resource exists, produces a 32-bit BMP;
          otherwise, produces a 24-bit BMP with no alpha channel.
    *5 -- The hotspot coordinates are appended to the output filename.
    *6 -- Produces two images (one instance of the pattern, and one 8x8 tiling).
    *7 -- Produces two images for each pattern in the resource, as in *6.
    *8 -- Produces four images (one instance of the pattern, one 8x8 tiling,
          one instance of the monochrome pattern, and one 8x8 tiling thereof).
    *9 -- Produces four images for each pattern in the resource, as in *8.
    *A -- This decoder depends on picttoppm, which is part of NetPBM.
    *B -- Converts line endings to Unix style.
    *C -- Produces one text file for each string in the resource.
    *D -- Some esoteric style options may not translate correctly.
    *E -- Can decompress IMA 4:1, MACE 3:1, MACE 6:1, and mu-law; A-law
          decompression is implemented but untested. Please send me an example
          file if you have one and it doesn't work.
    *F -- Instrument decoding is experimental and imperfect; some notes may not
          decode properly. The JSON file can be played with smssynth, which is
          part of gctools (http://www.github.com/fuzziqersoftware/gctools). When
          playing, the decoded snd and MIDI resources must be in the same
          directory as the JSON file and have the same names as when they were
          initially decoded.
    *G -- Tune decoding is experimental and probably will produce unplayable
          MIDI files.

If resource_dasm fails to convert a resource, or doesn't know how to, it will produce the resource's raw data instead.

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