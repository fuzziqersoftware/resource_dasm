# resource_dasm

This project contains tools I wrote for reverse-engineering classic Mac OS games.

The most general of these is **resource_dasm**, which reads and converts resources from the resource fork of any classic Mac OS file, including applications.

There are several programs for working with specific games:
- **dc_dasm**, a disassembler for Dark Castle data files
- **macski_decompress**, a decompressor for COOK/CO2K/RUN4 encoding used in MacSki
- **mohawk_dasm**, a disassembler for Mohawk archive files used in Myst, Riven, and Prince of Persia 2
- **realmz_dasm**, a disassembler and map generator for Realmz scenarios
- **render_infotron_levels**, a map generator for Infotron levels
- **sc2k_decode_sprite**, a renderer for SimCity 2000 sprite resources
- **bt_decode_sprite**, a renderer for Bubble Trouble and Harry the Handsome Executive sprite resources

## Building

- Install Netpbm (http://netpbm.sourceforge.net/). This is only needed for converting PICT resources; if you don't care about PICTs, you can skip this step.
- Build and install phosg (https://github.com/fuzziqersoftware/phosg).
- Run `make`.

## The tools

### resource_dasm

resource_dasm is a disassembler for classic Mac OS resource forks. It extracts resources from the resource fork of any file and converts many classic Mac OS resource formats (images, sounds, text, etc.) into modern formats. Run resource_dasm without any arguments for usage information.

Currently, resource_dasm can convert these resource types:

    Type -- Output format               -- Notes
    --------------------------------------------
    ADBS -- Plain text (MC68K assembly) -- *K
    CDEF -- Plain text (MC68K assembly) -- *K
    cicn -- Windows BMP (32-bit)        -- *1
    clok -- Plain text (MC68K assembly) -- *K
    clut -- Windows BMP (24-bit)        --
    cmid -- MIDI sequence               --
    CODE -- Plain text (MC68K assembly) -- *K *L
    crsr -- Windows BMP (32-bit)        -- *1 *4
    csnd -- Microsoft WAV               -- *D
    CURS -- Windows BMP (32-bit)        -- *4
    dcmp -- Plain text (MC68K assembly) -- *K
    ecmi -- MIDI sequence               -- *G
    emid -- MIDI sequence               -- *G
    esnd -- Microsoft WAV               -- *G
    ESnd -- Microsoft WAV               -- *H
    icl4 -- Windows BMP (24/32-bit)     -- *3
    icl8 -- Windows BMP (24/32-bit)     -- *3
    icm# -- Windows BMP (32-bit)        --
    icm4 -- Windows BMP (24/32-bit)     -- *3
    icm8 -- Windows BMP (24/32-bit)     -- *3
    ICN# -- Windows BMP (32-bit)        --
    icns -- Icon images (icns)          --
    ICON -- Windows BMP (24-bit)        --
    ics# -- Windows BMP (32-bit)        --
    ics4 -- Windows BMP (24/32-bit)     -- *3
    ics8 -- Windows BMP (24/32-bit)     -- *3
    INIT -- Plain text (MC68K assembly) -- *K
    kcs# -- Windows BMP (32-bit)        --
    kcs4 -- Windows BMP (24/32-bit)     -- *3
    kcs8 -- Windows BMP (24/32-bit)     -- *3
    LDEF -- Plain text (MC68K assembly) -- *K
    MADH -- PlayerPRO MADH module       --
    MDBF -- Plain text (MC68K assembly) -- *K
    MDEF -- Plain text (MC68K assembly) -- *K
    MIDI -- MIDI sequence               --
    Midi -- MIDI sequence               --
    midi -- MIDI sequence               --
    MOOV -- QuickTime Movie             --
    MooV -- QuickTime Movie             --
    moov -- QuickTime Movie             --
    PACK -- Plain text (MC68K assembly) -- *K
    PAT  -- Windows BMP (24-bit)        -- *5
    PAT# -- Windows BMP (24-bit)        -- *6
    PICT -- Windows BMP (24-bit)        -- *9
    pltt -- Windows BMP (24-bit)        --
    ppat -- Windows BMP (24-bit)        -- *7
    ppt# -- Windows BMP (24-bit)        -- *8
    proc -- Plain text (MC68K assembly) -- *K
    PTCH -- Plain text (MC68K assembly) -- *K
    ptch -- Plain text (MC68K assembly) -- *K
    ROvr -- Plain text (MC68K assembly) -- *K
    SERD -- Plain text (MC68K assembly) -- *K
    SICN -- Windows BMP (24-bit)        -- *2
    SMSD -- Microsoft WAV               -- *J
    snd  -- Microsoft WAV               -- *D
    snth -- Plain text (MC68K assembly) -- *K
    SONG -- smssynth JSON               -- *E
    STR  -- Plain text                  -- *A
    STR# -- Plain text                  -- *A *B
    styl -- Rich text format (RTF)      -- *C
    TEXT -- Plain text                  -- *A
    Tune -- MIDI sequence               -- *F
    WDEF -- Plain text (MC68K assembly) -- *K

    Notes:
    *1 -- Produces two images (one color, one monochrome).
    *2 -- Produces one image for each icon in the resource.
    *3 -- If a corresponding monochrome resource exists (ICN# for icl4/8, icm#
          for icl4/8, ics# for ics4/8, kcs# for kcs4/8), produces a 32-bit BMP;
          otherwise, produces a 24-bit BMP with no alpha channel. All color
          information in the original resource is reproduced in the output, even
          for fully-transparent pixels. If the icon was intended to be used with
          a nonstandard compositing mode, the colors of fully-transparent pixels
          may have been relevant, but most image viewers and editors don't have
          a way to display this information.
    *4 -- The hotspot coordinates are appended to the output filename. The alpha
          channel in the cursor resource doesn't have the same meaning as in a
          normal image file; pixels with non-white color and non-solid alpha
          cause the background to be inverted when rendered by classic Mac OS.
          resource_dasm faithfully reproduces the color values of these pixels
          in the output file, but most modern image editors won't show these
          "transparent" pixels.
    *5 -- Produces two images (one instance of the pattern, and one 8x8 tiling).
    *6 -- Produces two images for each pattern in the resource, as in *5.
    *7 -- Produces four images (one instance of the pattern, one 8x8 tiling,
          one instance of the monochrome pattern, and one 8x8 tiling thereof).
    *8 -- Produces four images for each pattern in the resource, as in *7.
    *9 -- This decoder depends on picttoppm, which is part of NetPBM. There is
          a rare failure mode in which picttoppm hangs forever; you may need to
          manually kill the picttoppm process if this happens. resource_dasm
          will consider it a normal failure and export the resource's raw data
          instead.
    *A -- Converts line endings to Unix style.
    *B -- Produces one text file for each string in the resource.
    *C -- Some esoteric style options may not translate correctly. styl
          resources provide styling information for the TEXT resource with the
          same ID, so such a resource must be present to properly decode a styl.
    *D -- Always produces uncompressed WAV files, even if the resource's data is
          compressed. resource_dasm can decompress IMA 4:1, MACE 3:1, MACE 6:1,
          and mu-law compression. A-law decompression is implemented but is
          currently untested and probably doesn't work. Please send me an
          example file if you have one and it doesn't work.
    *E -- Instrument decoding is experimental and imperfect; some notes may not
          decode properly. The JSON file can be played with smssynth, which is
          part of gctools (http://www.github.com/fuzziqersoftware/gctools). When
          playing, the decoded snd and MIDI resources must be in the same
          directory as the JSON file and have the same names as when they were
          initially decoded.
    *F -- Tune decoding is experimental and probably will produce unplayable
          MIDI files.
    *G -- Decryption support is based on reading SoundMusicSys source and hasn't
          been tested on real resources. Please send me an example file if you
          have one and it doesn't work.
    *H -- ESnd resources (as opposed to esnd resources) were only used in two
          games I know of, and the decoder implementation is based on reverse-
          engineering one of those games. The format is likely nonstandard.
    *J -- This resource appears to have a fixed format, with a constant sample
          rate, sample width and channel count. You may have to adjust these
          parameters in the output if it turns out that these are somehow
          configurable.
    *K -- Not all opcodes are implemented; some more esoteric opcodes may be
          disassembled as "<<unimplemented>>".
    *L -- The disassembler attempts to find exported functions by parsing the
          jump table in the CODE 0 resource, but if this resource is missing or
          not in the expected format, it silently skips this step. Generally, if
          any "export_X:" labels appear in the disassembly, then export
          resolution succeeded and all of the labels should be correct
          (otherwise they will all be missing).

If resource_dasm fails to convert a resource, or doesn't know how to, it will produce the resource's raw data instead.

Most of the decoder implementations in resource_dasm are based on reverse-engineering existing software and pawing through the dregs of old documentation, so some rarer types of resources probably won't work yet. However, I want this project to be as complete as possible, so if you have a resource that you think should be decodable but resource_dasm can't decode it, send it to me (perhaps by attaching to a GitHub issue) and I'll try my best to make resource_dasm understand it.

resource_dasm attempts to transparently decompress resources that are marked by the resource manager as compressed. Current support for decompression is incomplete; it depends on an embedded MC68K emulator that doesn't (yet) implement the entire CPU. All four decompressors built into the Mac OS System file should work properly, as well as Ben Mickaelian's self-modifying decompressor that was used in After Dark's You Bet Your Head module and a fairly simple decompressor that may have originally been part of FutureBASIC. There are probably other decompressors out there that I haven't seen; if you see errors like "execution failed" when using resource_dasm, please send me the .bin file that caused the failure and all the dcmp resources from the same source file.

### dc_dasm

Dark Castle is a 2D platformer. dc_dasm extracts the contents of the DC Data file and decodes the contained sounds and images. Run it from the folder containing the DC Data file.

### macski_decompress

MacSki is a skiing game with a somewhat sarcastic sense of humor. macski_decompress decodes the compressed resources used in later versions of the game. To use it, extract raw resources using `resource_dasm --save-raw=yes --skip-decode`, then after decompressing each file with macski_decompress, the individual resources can be converted with e.g. `resource_dasm --decode-type=PICT MacSki_PICT_30000.bin.dec`.

### mohawk_dasm

Run mohawk_dasm and give it the name of a Mohawk file. It will generate multiple files in the same directory as the original file, one for each resource contained in the archive.

### realmz_dasm

Realmz is a fantasy role-playing game for Windows and classic Mac OS. realmz_dasm is a disassembler for Realmz scenarios; it produces annotated maps of all land and dungeon levels, as well as descriptions of all events and encounters that may occur in the scenario.

To use realmz_dasm, put realmz_dasm and realmz_dasm_all.sh in the same directory as Realmz, and run realmz_dasm_all.sh from there. This will produce a directory named realmz_dasm_all.out containing some very large image files (maps of all the land and dungeon levels), the scenario scripts, and the resources contained in each scenario (icons, sounds, text). realmz_dasm can handle both Windows and Mac scenario formats and detects each scenario's format automatically.

### render_infotron_levels

Infotron is a puzzle game very much like Supaplex (and Move Blocks and Eat Stuff). render_infotron_levels decodes the levels from the game's resource fork and draws maps of them. Just put render_infotron_levels in the "Info Datafiles" folder and run it from there.

### sc2k_decode_sprite

SimCity 2000 is a resource-management game about building cities. sc2k_decode_sprite converts the SPRT resources included in the game into uncompressed bmp files. Just give it a SPRT file and a pltt file (both produced by resource_dasm) and it will do the rest.

### bt_decode_sprite

Bubble Trouble is an arcade kill-and-avoid-the-enemies game. Harry the Handsome Executive is a... hard-to-describe game. bt_decode_sprite converts the btSP resources included in Bubble Trouble and the HrSp resources included in Harry the Handsome Executive into uncompressed bmp files. Run it like this:

- For BT: `bt_decode_sprite --btsp btsp_file.bin clut_file.bin`
- For Harry: `bt_decode_sprite --hrsp hrsp_file.bin clut_file.bin`

For Bubble Trouble, the clut file should come from the Bubble Trouble game. For Harry, the clut should be the standard system clut (get it from System, or just use the Bubble Trouble clut if you have it).
