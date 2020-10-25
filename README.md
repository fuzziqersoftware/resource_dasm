# resource_dasm

This project contains tools I wrote for reverse-engineering classic Mac OS games.

The most general of these is **resource_dasm**, which reads and converts resources from the resource fork of any classic Mac OS file, including applications.

There are several programs for working with specific games:
- **bt_decode_sprite**, a renderer for Bubble Trouble and Harry the Handsome Executive sprite resources
- **dc_dasm**, a disassembler for Dark Castle data files
- **macski_decompress**, a decompressor for COOK/CO2K/RUN4 encoding used in MacSki
- **mohawk_dasm**, a disassembler for Mohawk archive files used in Myst, Riven, and Prince of Persia 2
- **realmz_dasm**, a disassembler and map generator for Realmz scenarios (also works with scenarios in Windows format)
- **render_ferazels_wand_levels**, a map generator for Ferazel's Wand levels
- **render_infotron_levels**, a map generator for Infotron levels
- **render_monkey_shines_world**, a map generator for Monkey Shines worlds
- **sc2k_decode_sprite**, a renderer for SimCity 2000 sprite resources
- **render_bits**, a simple color converter for visualizing binary data

## Building

- Install Netpbm (http://netpbm.sourceforge.net/). This is only needed for converting PICT resources that resource_dasm can't decode by itself - if you don't care about PICTs, you can skip this step.
- Build and install phosg (https://github.com/fuzziqersoftware/phosg).
- Run `make`.

## The tools

### resource_dasm

resource_dasm is a disassembler for classic Mac OS resource forks. It extracts resources from the resource fork of any file and converts many classic Mac OS resource formats (images, sounds, text, etc.) into modern formats. Run resource_dasm without any arguments for usage information.

Currently, resource_dasm can convert these resource types:

    Type -- Output                                                  -- Notes
    ------------------------------------------------------------------------
    ADBS -- .txt (MC68K assembly)                                   -- *C
    CDEF -- .txt (MC68K assembly)                                   -- *C
    cicn -- .bmp (32-bit and monochrome)                            --
    clok -- .txt (MC68K assembly)                                   -- *C
    clut -- .bmp (24-bit)                                           --
    cmid -- .midi                                                   --
    CODE -- .txt (MC68K assembly or import table description)       -- *B *C
    crsr -- .bmp (32-bit and monochrome)                            -- *1
    csnd -- .wav                                                    -- *5
    CURS -- .bmp (32-bit)                                           -- *1
    dcmp -- .txt (MC68K assembly)                                   -- *C
    ecmi -- .midi                                                   -- *8
    emid -- .midi                                                   -- *8
    esnd -- .wav                                                    -- *5 *8
    ESnd -- .wav                                                    -- *5 *9
    icl4 -- .bmp (24 or 32-bit)                                     -- *0
    icl8 -- .bmp (24 or 32-bit)                                     -- *0
    icm# -- .bmp (32-bit)                                           --
    icm4 -- .bmp (24 or 32-bit)                                     -- *0
    icm8 -- .bmp (24 or 32-bit)                                     -- *0
    ICN# -- .bmp (32-bit)                                           --
    icns -- .icns                                                   --
    ICON -- .bmp (24-bit)                                           --
    ics# -- .bmp (32-bit)                                           --
    ics4 -- .bmp (24 or 32-bit)                                     -- *0
    ics8 -- .bmp (24 or 32-bit)                                     -- *0
    INIT -- .txt (MC68K assembly)                                   -- *C
    kcs# -- .bmp (32-bit)                                           --
    kcs4 -- .bmp (24 or 32-bit)                                     -- *0
    kcs8 -- .bmp (24 or 32-bit)                                     -- *0
    LDEF -- .txt (MC68K assembly)                                   -- *C
    MADH -- .madh (PlayerPRO module)                                --
    MDBF -- .txt (MC68K assembly)                                   -- *C
    MDEF -- .txt (MC68K assembly)                                   -- *C
    MIDI -- .midi                                                   --
    Midi -- .midi                                                   --
    midi -- .midi                                                   --
    MOOV -- .mov                                                    --
    MooV -- .mov                                                    --
    moov -- .mov                                                    --
    PACK -- .txt (MC68K assembly)                                   -- *C
    PAT  -- .bmp (24-bit; pattern and 8x8 tiling)                   --
    PAT# -- .bmp (24-bit; pattern and 8x8 tiling for each pattern)  --
    PICT -- .bmp (24-bit) or other format                           -- *2
    pltt -- .bmp (24-bit)                                           --
    ppat -- .bmp (24-bit; pattern, 8x8, monochrome, monochrome 8x8) --
    ppt# -- .bmp (24-bit; 4 images as above for each pattern)       --
    proc -- .txt (MC68K assembly)                                   -- *C
    PTCH -- .txt (MC68K assembly)                                   -- *C
    ptch -- .txt (MC68K assembly)                                   -- *C
    ROvr -- .txt (MC68K assembly)                                   -- *C
    SERD -- .txt (MC68K assembly)                                   -- *C
    SICN -- .bmp (24-bit, one per icon)                             --
    SMOD -- .txt (MC68K assembly)                                   -- *C
    SMSD -- .wav                                                    -- *A
    snd  -- .wav                                                    -- *5
    snth -- .txt (MC68K assembly)                                   -- *C
    SONG -- .json (smssynth)                                        -- *6
    STR  -- .txt                                                    -- *3
    STR# -- .txt (one file per string)                              -- *3
    styl -- .rtf                                                    -- *4
    TEXT -- .txt                                                    -- *3
    Tune -- .midi                                                   -- *7
    WDEF -- .txt (MC68K assembly)                                   -- *C

    Notes:
    *0 -- If a corresponding monochrome resource exists (ICN# for icl4/8, icm#
          for icl4/8, ics# for ics4/8, kcs# for kcs4/8), produces a 32-bit BMP;
          otherwise, produces a 24-bit BMP with no alpha channel. All color
          information in the original resource is reproduced in the output, even
          for fully-transparent pixels. If the icon was intended to be used with
          a nonstandard compositing mode, the colors of fully-transparent pixels
          may have been relevant, but most image viewers and editors don't have
          a way to display this information.
    *1 -- The hotspot coordinates are appended to the output filename. The alpha
          channel in the cursor resource doesn't have the same meaning as in a
          normal image file; pixels with non-white color and non-solid alpha
          cause the background to be inverted when rendered by classic Mac OS.
          resource_dasm faithfully reproduces the color values of these pixels
          in the output file, but most modern image editors won't show these
          "transparent" pixels.
    *2 -- resource_dasm contains multiple PICT decoders. It will first attempt
          to decode the PICT using its internal decoder, which usually produces
          correct results but fails on PICTs that contain complex drawing
          opcodes. This decoder can handle basic QuickTime images as well (e.g.
          embedded JPEGs and PNGs), but can't do any drawing under or over them,
          or matte/mask effects. PICTs that contain embedded images in other
          formats will result in output files in those formats rather than BMP.
          In case this decoder fails, resource_dasm will fall back to a decoder
          that uses picttoppm, which is part of NetPBM. There is a rare failure
          mode in which picttoppm hangs forever; you may need to manually kill
          the picttoppm process if this happens. If picttoppm fails to decode
          the PICT or is killed, resource_dasm will prepend the necessary header
          and save it as a PICT file instead of a BMP.
    *3 -- Decodes text using the Mac OS Roman encoding and converts line endings
          to Unix style.
    *4 -- Some esoteric style options may not translate correctly. styl
          resources provide styling information for the TEXT resource with the
          same ID, so such a resource must be present to properly decode a styl.
    *5 -- Always produces uncompressed WAV files, even if the resource's data is
          compressed. resource_dasm can decompress IMA 4:1, MACE 3:1, MACE 6:1,
          and mu-law compression. A-law decompression is implemented but is
          currently untested and probably doesn't work. Please send me an
          example file if you have one and it doesn't work.
    *6 -- Instrument decoding is experimental and imperfect; some notes may not
          decode properly. The JSON file can be played with smssynth, which is
          part of gctools (http://www.github.com/fuzziqersoftware/gctools). When
          playing, the decoded snd and MIDI resources must be in the same
          directory as the JSON file and have the same names as when they were
          initially decoded.
    *7 -- Tune decoding is experimental and probably will produce unplayable
          MIDI files.
    *8 -- Decryption support is based on reading SoundMusicSys source and hasn't
          been tested on real resources. Please send me an example file if you
          have one and it doesn't work.
    *9 -- ESnd resources (as opposed to esnd resources) were only used in two
          games I know of, and the decoder implementation is based on reverse-
          engineering one of those games. The format is likely nonstandard.
    *A -- This resource appears to have a fixed format, with a constant sample
          rate, sample width and channel count. You may have to adjust these
          parameters in the output if it turns out that these are somehow
          configurable.
    *B -- The disassembler attempts to find exported functions by parsing the
          jump table in the CODE 0 resource, but if this resource is missing or
          not in the expected format, it silently skips this step. Generally, if
          any "export_X:" labels appear in the disassembly, then export
          resolution succeeded and all of the labels should be correct
          (otherwise they will all be missing). When passing a CODE resource to
          --decode-type, resource_dasm will assume it's not CODE 0 and will
          disassemble it as actual code rather than an import table.
    *C -- Not all opcodes are implemented; some more esoteric opcodes may be
          disassembled as "<<unimplemented>>".

If resource_dasm fails to convert a resource, or doesn't know how to, it will produce the resource's raw data instead.

Most of the decoder implementations in resource_dasm are based on reverse-engineering existing software and pawing through the dregs of old documentation, so some rarer types of resources probably won't work yet. However, I want this project to be as complete as possible, so if you have a resource that you think should be decodable but resource_dasm can't decode it, send it to me (perhaps by attaching to a GitHub issue) and I'll try my best to make resource_dasm understand it.

resource_dasm attempts to transparently decompress resources that are marked by the resource manager as compressed. Current support for decompression is incomplete; it depends on an embedded MC68K emulator that doesn't (yet) implement the entire CPU. All four decompressors built into the Mac OS System file should work properly, as well as Ben Mickaelian's self-modifying decompressor that was used in some After Dark modules and a fairly simple decompressor that may have originally been part of FutureBASIC. There are probably other decompressors out there that I haven't seen; if you see errors like "execution failed" when using resource_dasm, please send me the .bin file that caused the failure and all the dcmp resources from the same source file.

### bt_decode_sprite

Bubble Trouble is an arcade kill-and-avoid-the-enemies game. Harry the Handsome Executive is a... hard-to-describe game. bt_decode_sprite converts the btSP resources included in Bubble Trouble and the HrSp resources included in Harry the Handsome Executive into uncompressed bmp files. Run it like this:

- For BT: `bt_decode_sprite --btsp btsp_file.bin clut_file.bin`
- For Harry: `bt_decode_sprite --hrsp hrsp_file.bin clut_file.bin`

For Bubble Trouble, the clut file should come from the Bubble Trouble game. For Harry, the clut should be the standard system clut (get it from System, or just use the Bubble Trouble clut if you have it).

### dc_dasm

Dark Castle is a 2D platformer. dc_dasm extracts the contents of the DC Data file and decodes the contained sounds and images. Run it from the folder containing the DC Data file, or give it the DC Data filename and an output directory on the command line.

### macski_decompress

MacSki is a skiing game with a somewhat sarcastic sense of humor. macski_decompress decodes the compressed resources used in later versions of the game. To use it, extract raw resources using `resource_dasm --save-raw=yes --skip-decode`, then after decompressing each file with macski_decompress, the individual resources can be converted with e.g. `resource_dasm --decode-type=PICT MacSki_PICT_30000.bin.dec`.

### mohawk_dasm

Run mohawk_dasm and give it the name of a Mohawk file. It will generate multiple files in the same directory as the original file, one for each resource contained in the archive.

### realmz_dasm

Realmz is a fantasy role-playing game for Windows and classic Mac OS. realmz_dasm is a disassembler for Realmz scenarios; it produces annotated maps of all land and dungeon levels, as well as descriptions of all events and encounters that may occur in the scenario.

To use realmz_dasm, put realmz_dasm and realmz_dasm_all.sh in the same directory as Realmz, and run realmz_dasm_all.sh from there. This will produce a directory named realmz_dasm_all.out containing some very large image files (maps of all the land and dungeon levels), the scenario scripts, and the resources contained in each scenario (icons, sounds, text). realmz_dasm can handle both Windows and Mac scenario formats and detects each scenario's format automatically.

### render_ferazels_wand_levels

Ferazel's Wand is an action-adventure game in which you destroy evil monsters and save the world using magic spells. render_ferazels_wand_levels decodes the levels from the game's resource fork and draws maps of them. Just put render_ferazels_wand_levels in the same folder as all the Ferazel's Wand data files and run it from there.

### render_harry_levels

render_harry_levels decodes the levels from Harry the Handsome Executive and draws maps of them. Just put render_harry_levels in the same folder as all the game's data files and run it from there. You'll have to manually supply a clut file, though (like for bt_decode_sprite).

### render_infotron_levels

Infotron is a puzzle game very much like Supaplex (and Move Blocks and Eat Stuff). render_infotron_levels decodes the levels from the game's resource fork and draws maps of them. Just put render_infotron_levels in the "Info Datafiles" folder and run it from there.

### render_monkey_shines_world

Monkey Shines is a platformer game published by Fantasoft. render_monkey_shines_world decodes the levels from the game files' resource forks and draws maps of them. Give it a Bonzo World file and an output prefix (for example, run it like `render_monkey_shines_world "Bonzo World 1" ./bonzo_world_1_maps`) and it will generate a couple of BMP files.

### sc2k_decode_sprite

SimCity 2000 is a resource-management game about building cities. sc2k_decode_sprite converts the SPRT resources included in the game into uncompressed bmp files. Just give it a SPRT file and a pltt file (both produced by resource_dasm) and it will do the rest.

### render_bits

render_bits is useful to answer the question "might this random-looking binary data actually be an image?" Give it a color format and some binary data, and it will produce a full-color BMP file that you can look at with your favorite image editor. If the output looks like garbage, play around with the width and color format until you figure out the right parameters.

Run render_bits without any options for usage information.
