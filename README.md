# resource_dasm

This project contains multiple tools for reverse-engineering classic Mac OS applications and games.

The most general of these is **resource_dasm**, which reads and converts resources from the resource fork of any classic Mac OS file, including applications. resource_dasm can also disassemble raw 68k or PowerPC machine code, as well as PEFF executables that contain code for either of those CPU architectures. Most of resource_dasm's functionality is also included in a library built alongside it named libresource_dasm.

There are several programs for working with specific programs (mostly games):
- **bt_render**: converts sprites from Bubble Trouble and Harry the Handsome Executive into BMP images
- **dc_dasm**: disassembles DC Data from Dark Castle and converts the sprites into BMP images
- **ferazel_render**: generates maps from Ferazel's Wand world files
- **harry_render**: generates maps from Harry the Handsome Executive world files
- **hypercard_dasm**: disassembles HyperCard stacks
- **infotron_render**: generates maps from Infotron levels files
- **macski_decomp**: decompresses the COOK/CO2K/RUN4 encodings used by MacSki
- **mohawk_dasm**: disassembles Mohawk archives used by Myst, Riven, Prince of Persia 2, and other games
- **mshines_render**: generates maps from Monkey Shines world files
- **realmz_dasm**: generates maps from Realmz scenarios and disassembles the scenario scripts into readable assembly-like syntax
- **sc2k_render**: converts sprites from SimCity 2000 into BMP images
- **step_on_it_render**: converts sprites from Step On It! into BMP images

There's also a basic image renderer called **render_bits** which is useful in figuring out embedded images or 2-D arrays in unknown file formats.

## Building

- Install Netpbm (http://netpbm.sourceforge.net/). This is only needed for converting PICT resources that resource_dasm can't decode by itself - if you don't care about PICTs, you can skip this step.
- Build and install phosg (https://github.com/fuzziqersoftware/phosg).
- Run `make`.

This project should build properly on sufficiently recent versions of macOS and Linux.

## Using resource_dasm

resource_dasm is a disassembler for classic Mac OS resource forks. It extracts resources from the resource fork of any file and converts many classic Mac OS resource formats (images, sounds, text, etc.) into modern formats.

Examples:

    # Disassemble all resources from a specific file (the output is written to
    # the <filename>.out directory by default):
    ./resource_dasm files/Tesserae

    # Disassemble all resources from all files in a folder, writing the output
    # files into a parallel folder structure in the current directory:
    ./resource_dasm "files/Apeiron ƒ/" ./apeiron.out

    # Disassemble a specific resource from a specific file:
    ./resource_dasm "files/MacSki 1.7/MacSki Sounds" ./macski.out \
        --target-type=snd --target-id=1023

    # Disassemble a PowerPC application's resources and its code:
    ./resource_dasm "files/Adventures of Billy" ./billy.out
    ./resource_dasm "files/Adventures of Billy" ./billy.out/dasm.txt \
        --disassemble-pef

    # Due to copying files across different types of filesystems, you might
    # have a file's resource fork in the data fork of a separate file instead.
    # To disassemble such a file:
    ./resource_dasm "windows/Realmz/Data Files/Portraits.rsf" ./portraits.out \
        --data-fork

This isn't all resource_dasm can do; run it without any arguments for further usage information.

### Capabilities

Currently, resource_dasm can convert these resource types:

    Type -- Output                                                  -- Notes
    ------------------------------------------------------------------------
    actb -- .bmp (24-bit)                                           -- *E
    ADBS -- .txt (68K assembly)                                     -- *C
    cctb -- .bmp (24-bit)                                           -- *E
    CDEF -- .txt (68K assembly)                                     -- *C
    cfrg -- .txt (description of code fragments)                    -- *D
    cicn -- .bmp (32-bit and monochrome)                            --
    clok -- .txt (68K assembly)                                     -- *C
    clut -- .bmp (24-bit)                                           -- *E
    cmid -- .midi                                                   --
    CODE -- .txt (68K assembly or import table description)         -- *B *C
    crsr -- .bmp (32-bit and monochrome)                            -- *1
    csnd -- .wav                                                    -- *5
    CURS -- .bmp (32-bit)                                           -- *1
    dcmp -- .txt (68K assembly)                                     -- *C
    dctb -- .bmp (24-bit)                                           -- *E
    DRVR -- .txt (68K assembly)                                     -- *C
    ecmi -- .midi                                                   -- *8
    emid -- .midi                                                   -- *8
    esnd -- .wav                                                    -- *5 *8
    ESnd -- .wav                                                    -- *5 *9
    fctb -- .bmp (24-bit)                                           -- *E
    FONT -- .txt (description) and .bmp (24-bit glyphs)             --
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
    INIT -- .txt (68K assembly)                                     -- *C
    kcs# -- .bmp (32-bit)                                           --
    kcs4 -- .bmp (24 or 32-bit)                                     -- *0
    kcs8 -- .bmp (24 or 32-bit)                                     -- *0
    LDEF -- .txt (68K assembly)                                     -- *C
    MADH -- .madh (PlayerPRO module)                                --
    MADI -- .madi (PlayerPRO module)                                --
    MDBF -- .txt (68K assembly)                                     -- *C
    MDEF -- .txt (68K assembly)                                     -- *C
    MIDI -- .midi                                                   --
    Midi -- .midi                                                   --
    midi -- .midi                                                   --
    MOOV -- .mov                                                    --
    MooV -- .mov                                                    --
    moov -- .mov                                                    --
    ncmp -- .txt (PPC32 assembly and header description)            --
    ndmc -- .txt (PPC32 assembly and header description)            --
    ndrv -- .txt (PPC32 assembly and header description)            --
    NFNT -- .txt (description) and .bmp (24-bit glyphs)             --
    nift -- .txt (PPC32 assembly and header description)            --
    nitt -- .txt (PPC32 assembly and header description)            --
    nlib -- .txt (PPC32 assembly and header description)            --
    nsnd -- .txt (PPC32 assembly and header description)            --
    ntrb -- .txt (PPC32 assembly and header description)            --
    PACK -- .txt (68K assembly)                                     -- *C
    PAT  -- .bmp (24-bit; pattern and 8x8 tiling)                   --
    PAT# -- .bmp (24-bit; pattern and 8x8 tiling for each pattern)  --
    PICT -- .bmp (24-bit) or other format                           -- *2
    pltt -- .bmp (24-bit)                                           -- *E
    ppat -- .bmp (24-bit; pattern, 8x8, monochrome, monochrome 8x8) --
    ppt# -- .bmp (24-bit; 4 images as above for each pattern)       --
    proc -- .txt (68K assembly)                                     -- *C
    PTCH -- .txt (68K assembly)                                     -- *C
    ptch -- .txt (68K assembly)                                     -- *C
    ROvr -- .txt (68K assembly)                                     -- *C
    SERD -- .txt (68K assembly)                                     -- *C
    sfnt -- .ttf (TrueType font)                                    --
    SICN -- .bmp (24-bit, one per icon)                             --
    SIZE -- .txt (description of parameters)                        --
    SMOD -- .txt (68K assembly)                                     -- *C
    SMSD -- .wav                                                    -- *A
    snd  -- .wav                                                    -- *5
    snth -- .txt (68K assembly)                                     -- *C
    SONG -- .json (smssynth)                                        -- *6
    STR  -- .txt                                                    -- *3
    STR# -- .txt (one file per string)                              -- *3
    styl -- .rtf                                                    -- *4
    TEXT -- .txt                                                    -- *3
    Tune -- .midi                                                   -- *7
    wctb -- .bmp (24-bit)                                           -- *E
    WDEF -- .txt (68K assembly)                                     -- *C

    Notes:
    *0 -- If a corresponding monochrome resource exists (ICN# for icl4/8, icm#
          for icm4/8, ics# for ics4/8, kcs# for kcs4/8), produces a 32-bit BMP;
          otherwise, produces a 24-bit BMP with no alpha channel. All color
          information in the original resource is reproduced in the output, even
          for fully-transparent pixels. If the icon was intended to be used with
          a nonstandard compositing mode, the colors of fully-transparent pixels
          may have been relevant, but most modern image viewers and editors
          don't have a way to display this information.
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
          mode in which picttoppm hangs forever; resource_dasm gives it 10
          seconds to do its job before killing it and giving up. If picttoppm
          fails to decode the PICT or is killed, resource_dasm will prepend the
          necessary header and save it as a PICT file instead of a BMP.
    *3 -- Text is decoded using the Mac OS Roman encoding and line endings (\r)
          are converted to Unix style (\n).
    *4 -- Some esoteric style options may not be translated correctly. styl
          resources provide styling information for the TEXT resource with the
          same ID, so such a resource must be present to properly decode a styl.
    *5 -- Always produces uncompressed WAV files, even if the resource's data is
          compressed. resource_dasm can decompress IMA 4:1, MACE 3:1, MACE 6:1,
          and mu-law compression. A-law decompression is implemented but is
          currently untested. Please open a GitHub issue and upload an example
          file if you have one and it doesn't work.
    *6 -- Instrument decoding is experimental and imperfect; some notes may not
          decode properly. The JSON file can be played with smssynth, which is
          part of gctools (http://www.github.com/fuzziqersoftware/gctools). When
          playing, the decoded snd/csnd/esnd and MIDI/cmid/emid/ecmi resources
          must be in the same directory as the JSON file and have the same names
          as when they were initially decoded.
    *7 -- Tune decoding is experimental and probably will produce unplayable
          MIDI files.
    *8 -- Decryption support is based on reading SoundMusicSys source and hasn't
          been tested on real resources. Please open a GitHub issue and upload
          an example file if you have one and it doesn't work.
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
    *D -- Most PowerPC applications have their executable code in the data fork.
          To disassemble it, use the --disassemble-pef option (example above).
    *E -- For color table resources, the raw data is always saved even if it is
          decoded properly, since the original data contains 16-bit values for
          each channel and the output BMP file has less-precise 8-bit channels.

If resource_dasm fails to convert a resource, or doesn't know how to, it will produce the resource's raw data instead.

Most of the decoder implementations in resource_dasm are based on reverse-engineering existing software and pawing through the dregs of old documentation, so some rarer types of resources probably won't work yet. However, I want this project to be as complete as possible, so if you have a resource that you think should be decodable but resource_dasm can't decode it, send it to me (perhaps by attaching to a GitHub issue) and I'll try my best to make resource_dasm understand it.

resource_dasm attempts to transparently decompress resources that are marked by the resource manager as compressed. This is done by executing 68K or PowerPC code contained in a dcmp or ncmp resource, either contained in the same file as the compressed resource or in the System file. Decompression therefore depends on embedded 68K and PowerPC emulators that don't (yet) implement the entire CPU, so they may fail on some esoteric resources or decompressors. All four 68K decompressors built into the Mac OS System file (and included with resource_dasm) should work properly, as well as Ben Mickaelian's self-modifying decompressor that was used in some After Dark modules and a fairly simple decompressor that may have originally been part of FutureBASIC. There are probably other decompressors out there that I haven't seen; if you see "warning: failed to decompress resource" when using resource_dasm, please send me the .bin file that caused the failure and all the dcmp and ncmp resources from the same file.

### Using resource_dasm as a library

Run `sudo make install-lib` to copy the header files and library to the relevant paths after building (see the Makefile for the exact paths).

You can then `#include <resource_dasm/ResourceFile.hh>` and create `ResourceFile` objects in your own projects to read and decode resource fork data. Make sure to link with `-lresource_dasm` as well. There is not much documentation for this library beyond what's in the header file, but usage of the `ResourceFile` class should be fairly straightforward.

## Using the more specific tools

### render_bits

render_bits is useful to answer the question "might this random-looking binary data actually be an image or 2-D array?" Give it a color format and some binary data, and it will produce a full-color BMP file that you can look at with your favorite image viewer or editor. If the output looks like garbage, play around with the width and color format until you figure out the right parameters.

Run render_bits without any options for usage information.

### bt_render

bt_render converts the btSP resources included in Bubble Trouble and the HrSp resources included in Harry the Handsome Executive into uncompressed bmp files. Run it like this:

- For Bubble Trouble: `bt_decode_sprite --btsp btsp_file.bin clut_file.bin`
- For Harry: `bt_decode_sprite --hrsp hrsp_file.bin clut_file.bin`

For Bubble Trouble, the clut file should come from the Bubble Trouble game. For Harry, the clut should be the standard system clut (get it from System, or just use the Bubble Trouble clut if you have it).

### dc_dasm

dc_dasm extracts the contents of the DC Data file from Dark Castle and decodes the contained sounds and images. Run it from the folder containing the DC Data file, or run it like `dc_data "path/to/DC Data" output_directory`.

### ferazel_render

ferazel_render decodes the levels from Ferazel's Wand and draws maps of them. Just put ferazel_render in the same folder as all the Ferazel's Wand data files and run `./ferazel_render` in that directory.

### harry_render

harry_render decodes the levels from Harry the Handsome Executive and draws maps of them. Just put render_harry_levels in the same folder as all the game's data files and run it from there. You'll have to manually supply a clut file, though - run it like `./harry_render --clut-file=System_clut_9.bin` (for example).

### hypercard_dasm

hypercard_dasm decodes HyperCard stacks, producing text files with the metadata and scripts for the stack and each background, card, and part (button or field). It also draws images of the parts layout for each background and card, primarily to aid in digital spelunking of some early Cyan games (e.g. Cosmic Osmo, The Manhole) that use lots of invisible buttons.

### infotron_render

infotron_render decodes the levels from Infotron and draws maps of them. Just put infotron_render in the "Info Datafiles" folder and run `./infotron_render` in that directory.

### macski_decomp

macski_decomp decodes the compressed resources used in later versions of MacSki. To use it, pass `--external-preprocessor=./macski_decomp` when running resource_dasm.

You can also run it by itself, like `./macski_decomp < input-file > output-file`.

### mohawk_dasm

Run mohawk_dasm and give it the name of a Mohawk file. It will generate multiple files in the same directory as the original file, one for each resource contained in the archive. It will not attempt to decode the resources - you can use `resource_dasm --decode-type=XXXX ...` to do so after extracting.

### mshines_render

mshines_render decodes the levels from Monkey Shines and draws maps of them. Give it a Bonzo World file and an output prefix (for example, run it like `mshines_render "Bonzo World 1" ./bonzo_world_1_maps`) and it will generate a couple of BMP files - one for the main world and one for the bonus level.

### realmz_dasm

realmz_dasm is a disassembler for Realmz scenarios; it produces annotated maps of all land and dungeon levels, as well as descriptions of all events and encounters that may occur in the scenario.

To use realmz_dasm, put realmz_dasm and realmz_dasm_all.sh in the same directory as Realmz, and run realmz_dasm_all.sh from there. This will produce a directory named realmz_dasm_all.out containing some very large image files (maps of all the land and dungeon levels), the scenario scripts, and the resources contained in each scenario (icons, sounds, text). realmz_dasm can handle both Windows and Mac scenario formats and detects each scenario's format automatically.

### sc2k_render

sc2k_render converts the SPRT resources from SimCity 2000 into uncompressed BMP files. Use resource_dasm to get all the SPRT and pltt resources from the game, then run sc2k_render like `sc2k_render SimCity_2000_SPRT_200.bin SimCity_2000_pltt_200.bin` (for example).

### step_on_it_render

step_on_it_render converts the sssf resources from Step On It! into uncompressed BMP files. Use resource_dasm to get all the sssf and clut resources from the graphics file, then run step_on_it_render like `step_on_it_render sssf_file.bin clut_file.bin` (for example).
