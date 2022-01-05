# resource_dasm

This project contains multiple tools for reverse-engineering classic Mac OS applications and games.

The most general of these is **resource_dasm**, which reads and converts resources from the resource fork of any classic Mac OS file, including applications. resource_dasm can also disassemble raw 68k or PowerPC machine code, as well as PEFF executables that contain code for either of those CPU architectures. Most of resource_dasm's functionality is also included in a library built alongside it named libresource_file.

There are several programs for working with specific programs (mostly games):
- **bt_render**: converts sprites from Bubble Trouble and Harry the Handsome Executive into BMP images
- **dc_dasm**: disassembles DC Data from Dark Castle and converts the sprites into BMP images
- **ferazel_render**: generates maps from Ferazel's Wand world files
- **gamma_zee_render**: generates maps of Gamma Zee mazes
- **harry_render**: generates maps from Harry the Handsome Executive world files
- **hypercard_dasm**: disassembles HyperCard stacks and draws card images
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
- Install CMake.
- Build and install phosg (https://github.com/fuzziqersoftware/phosg).
- Run `cmake .`, then `make`.

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

resource_dasm can convert these resource types:

    Type   -- Output format                                           -- Notes
    --------------------------------------------------------------------------
    Text resources
      bstr -- .txt (one file per string)                              -- *3
      card -- .txt                                                    --
      finf -- .txt (description of contents)                          --
      FONT -- .txt (description) and .bmp (one image per glyph)       --
      NFNT -- .txt (description) and .bmp (one image per glyph)       --
      PSAP -- .txt                                                    --
      sfnt -- .ttf (TrueType font)                                    --
      STR  -- .txt                                                    -- *3
      STR# -- .txt (one file per string)                              -- *3
      styl -- .rtf                                                    -- *4
      TEXT -- .txt                                                    -- *3
      wstr -- .txt                                                    --
    --------------------------------------------------------------------------
    Image and color resources
      actb -- .bmp (24-bit)                                           -- *E
      acur -- .txt (list of cursor frame IDs)                         --
      cctb -- .bmp (24-bit)                                           -- *E
      cicn -- .bmp (32-bit and monochrome)                            --
      clut -- .bmp (24-bit)                                           -- *E
      crsr -- .bmp (32-bit and monochrome)                            -- *1
      CURS -- .bmp (32-bit)                                           -- *1
      dctb -- .bmp (24-bit)                                           -- *E
      fctb -- .bmp (24-bit)                                           -- *E
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
      kcs# -- .bmp (32-bit)                                           --
      kcs4 -- .bmp (24 or 32-bit)                                     -- *0
      kcs8 -- .bmp (24 or 32-bit)                                     -- *0
      PAT  -- .bmp (24-bit; pattern and 8x8 tiling)                   --
      PAT# -- .bmp (24-bit; pattern and 8x8 tiling for each pattern)  --
      PICT -- .bmp (24-bit) or other format                           -- *2
      pltt -- .bmp (24-bit)                                           -- *E
      ppat -- .bmp (24-bit; pattern, 8x8, monochrome, monochrome 8x8) --
      ppt# -- .bmp (24-bit; 4 images as above for each pattern)       --
      SICN -- .bmp (24-bit, one per icon)                             --
      wctb -- .bmp (24-bit)                                           -- *E
    --------------------------------------------------------------------------
    Sound and sequence resources
      cmid -- .midi                                                   --
      csnd -- .wav                                                    -- *5
      ecmi -- .midi                                                   -- *8
      emid -- .midi                                                   -- *8
      esnd -- .wav                                                    -- *5 *8
      ESnd -- .wav                                                    -- *5 *9
      MADH -- .madh (PlayerPRO module)                                --
      MADI -- .madi (PlayerPRO module)                                --
      MIDI -- .midi                                                   --
      Midi -- .midi                                                   --
      midi -- .midi                                                   --
      SMSD -- .wav                                                    -- *A
      snd  -- .wav                                                    -- *5
      SONG -- .json (smssynth)                                        -- *6
      Tune -- .midi                                                   -- *7
    --------------------------------------------------------------------------
    Code resources
      ADBS -- .txt (68K assembly)                                     -- *C
      CDEF -- .txt (68K assembly)                                     -- *C
      cdek -- .txt (PPC32 assembly and header description)            --
      cdev -- .txt (68K assembly)                                     -- *C
      cfrg -- .txt (description of code fragments)                    -- *D
      citt -- .txt (68K assembly)                                     -- *C
      clok -- .txt (68K assembly)                                     -- *C
      cmtb -- .txt (68K assembly)                                     -- *C
      cmu! -- .txt (68K assembly)                                     -- *C
      CODE -- .txt (68K assembly or import table description)         -- *B *C
      code -- .txt (68K assembly)                                     -- *C
      dcmp -- .txt (68K assembly)                                     -- *C
      dcod -- .txt (PPC32 assembly and header description)            --
      dem  -- .txt (68K assembly)                                     -- *C
      drvr -- .txt (68K assembly)                                     -- *C
      DRVR -- .txt (68K assembly)                                     -- *C
      enet -- .txt (68K assembly)                                     -- *C
      epch -- .txt (PPC32 assembly)                                   --
      expt -- .txt (PPC32 assembly)                                   --
      fovr -- .txt (PPC32 assembly and header description)            --
      gcko -- .txt (68K assembly)                                     -- *C
      gdef -- .txt (68K assembly)                                     -- *C
      GDEF -- .txt (68K assembly)                                     -- *C
      gnld -- .txt (68K assembly)                                     -- *C
      INIT -- .txt (68K assembly)                                     -- *C
      krnl -- .txt (PPC32 assembly)                                   --
      LDEF -- .txt (68K assembly)                                     -- *C
      lmgr -- .txt (68K assembly)                                     -- *C
      lodr -- .txt (68K assembly)                                     -- *C
      ltlk -- .txt (68K assembly)                                     -- *C
      MBDF -- .txt (68K assembly)                                     -- *C
      MDEF -- .txt (68K assembly)                                     -- *C
      ncmp -- .txt (PPC32 assembly and header description)            --
      ndmc -- .txt (PPC32 assembly and header description)            --
      ndrv -- .txt (PPC32 assembly and header description)            --
      nift -- .txt (PPC32 assembly and header description)            --
      nitt -- .txt (PPC32 assembly and header description)            --
      nlib -- .txt (PPC32 assembly and header description)            --
      nsnd -- .txt (PPC32 assembly and header description)            --
      nsrd -- .txt (PPC32 assembly)                                   --
      ntrb -- .txt (PPC32 assembly and header description)            --
      osl  -- .txt (68K assembly)                                     -- *C
      otdr -- .txt (68K assembly)                                     -- *C
      otlm -- .txt (68K assembly)                                     -- *C
      PACK -- .txt (68K assembly)                                     -- *C
      pnll -- .txt (68K assembly)                                     -- *C
      ppct -- .txt (PPC32 assembly and header description)            --
      proc -- .txt (68K assembly)                                     -- *C
      PTCH -- .txt (68K assembly)                                     -- *C
      ptch -- .txt (68K assembly)                                     -- *C
      pthg -- .txt (68K or PPC32 assembly and header description)     -- *C
      qtcm -- .txt (PPC32 assembly and header description)            --
      ROvr -- .txt (68K assembly)                                     -- *C
      scal -- .txt (PPC32 assembly and header description)            --
      scod -- .txt (68K assembly)                                     -- *C
      SERD -- .txt (68K assembly)                                     -- *C
      sfvr -- .txt (PPC32 assembly and header description)            --
      shal -- .txt (68K assembly)                                     -- *C
      sift -- .txt (68K assembly)                                     -- *C
      SMOD -- .txt (68K assembly)                                     -- *C
      snth -- .txt (68K assembly)                                     -- *C
      tdig -- .txt (68K assembly)                                     -- *C
      tokn -- .txt (68K assembly)                                     -- *C
      vdig -- .txt (68K or PPC32 assembly and header description)     -- *C
      wart -- .txt (68K assembly)                                     -- *C
      WDEF -- .txt (68K assembly)                                     -- *C
      XCMD -- .txt (68K assembly)                                     -- *C
      XFCN -- .txt (68K assembly)                                     -- *C
    --------------------------------------------------------------------------
    Miscellaneous resources
      ALRT -- .txt (alert parameters)                                 --
      APPL -- .txt (description of contents)                          --
      BNDL -- .txt (description of contents)                          --
      CMDK -- .txt (list of keys)                                     --
      CMNU -- .txt (description of menu)                              --
      cmnu -- .txt (description of menu)                              --
      CNTL -- .txt (description of control)                           --
      CTY# -- .txt (description of cities)                            --
      DITL -- .txt (dialog parameters)                                --
      DLOG -- .txt (dialog parameters)                                --
      errs -- .txt (description of error ranges)                      --
      FBTN -- .txt (description of buttons)                           --
      FDIR -- .txt (description of contents)                          --
      fld# -- .txt (description of folders)                           --
      FREF -- .txt (description of file references)                   --
      FRSV -- .txt (list of font IDs)                                 --
      FWID -- .txt (font parameters)                                  --
      GNRL -- .txt (description of contents)                          --
      hwin -- .txt (description of help window)                       --
      icmt -- .txt (icon reference and comment)                       --
      inbb -- .txt (description of contents)                          --
      indm -- .txt (description of contents)                          --
      infs -- .txt (description of contents)                          --
      inpk -- .txt (description of contents)                          --
      inra -- .txt (description of contents)                          --
      insc -- .txt (description of contents)                          --
      ITL1 -- .txt (short dates flag value)                           --
      itlb -- .txt (internationalization parameters)                  --
      itlc -- .txt (internationalization parameters)                  --
      itlk -- .txt (keyboard mappings)                                --
      KBDN -- .txt (keyboard name)                                    --
      LAYO -- .txt (description of layout)                            --
      MBAR -- .txt (list of menu IDs)                                 --
      mcky -- .txt (threshold values)                                 --
      MENU -- .txt (description of menu)                              --
      nrct -- .txt (rectangle boundaries)                             --
      PAPA -- .txt (printer parameters)                               --
      PICK -- .txt (picker parameters)                                --
      ppcc -- .txt (description of contents)                          --
      PRC0 -- .txt (description of contents)                          --
      PRC3 -- .txt (description of contents)                          --
      qrsc -- .txt (description of queries)                           --
      resf -- .txt (list of fonts)                                    --
      RMAP -- .txt (type mapping and list of ID exceptions)           --
      ROv# -- .txt (list of overridden resource IDs)                  --
      RVEW -- .txt (description of contents)                          --
      scrn -- .txt (screen device parameters)                         --
      sect -- .txt (description of contents)                          --
      SIGN -- .txt (description of contents)                          --
      SIZE -- .txt (description of parameters)                        --
      TMPL -- .txt (description of format)                            --
      TOOL -- .txt (description of contents)                          --
      vers -- .txt (version flags and strings)                        --
      WIND -- .txt (window parameters)                                --

    Notes:
    *0: Produces a 32-bit BMP if a corresponding monochrome resource exists
        (ICN# for icl4/8, icm# for icm4/8, ics# for ics4/8, kcs# for kcs4/8). If
        no monochrome resource exists, produces a 24-bit BMP instead. All color
        information in the original resource is reproduced in the output, even
        for fully-transparent pixels. If the icon was originally intended to be
        used with a nonstandard compositing mode, the colors of fully-
        transparent pixels may have been relevant, but most modern image viewers
        and editors don't have a way to display this information.
    *1: The hotspot coordinates are appended to the output filename. As in *0,
        resource_dasm faithfully reproduces the color values of transparent
        pixels in the output file, but most modern image editors won't show
        these "transparent" pixels.
    *2: resource_dasm implements multiple PICT decoders. It will first attempt
        to decode the PICT using its internal decoder, which usually produces
        correct results but fails on PICTs that contain complex drawing opcodes.
        This decoder can handle basic QuickTime images as well (e.g. embedded
        JPEGs and PNGs), but can't do any drawing under or over them, or
        matte/mask effects. PICTs that contain embedded JPEGs or PNGs will
        result in a JPEG or PNG output rather than a BMP. In case this decoder
        fails, resource_dasm will fall back to a decoder that uses picttoppm,
        which is part of NetPBM. There is a rare failure mode in which picttoppm
        hangs forever; resource_dasm gives it 10 seconds to do its job before
        killing it and giving up. If picttoppm fails to decode the PICT or is
        killed, resource_dasm will prepend the necessary header and save it as a
        PICT file instead of a BMP.
    *3: Text is decoded using the Mac OS Roman encoding and line endings (\r)
        are converted to Unix style (\n).
    *4: Some rare style options may not be translated correctly. styl resources
        provide styling information for the TEXT resource with the same ID, so
        such a resource must be present to properly decode a styl.
    *5: Always produces uncompressed WAV files, even if the resource's data is
        compressed. resource_dasm can decompress IMA 4:1, MACE 3:1, MACE 6:1,
        A-law, and mu-law compression. A-law decompression is not tested; please
        open a GitHub issue and upload an example file if it doesn't work.
    *6: Instrument decoding is experimental and imperfect; some notes may not
        decode properly. The JSON file can be played with smssynth, which is
        part of gctools (http://www.github.com/fuzziqersoftware/gctools). When
        playing, the decoded snd/csnd/esnd and MIDI/cmid/emid/ecmi resources
        must be in the same directory as the JSON file and have the same names
        as when they were initially decoded.
    *7: Tune decoding is experimental and will likely produce unplayable MIDIs.
    *8: Decryption support is based on reading SoundMusicSys source and hasn't
        been tested on real resources. Please open a GitHub issue and upload an
        example file if it doesn't work.
    *9: ESnd resources (as opposed to esnd resources) were only used in two
        games I know of, and the decoder implementation is based on reverse-
        engineering one of those games. The format is likely nonstandard.
    *A: This resource appears to have a fixed format, with a constant sample
        rate, sample width and channel count. You may have to adjust these
        parameters in the output if it turns out that these are configurable.
    *B: The disassembler attempts to find exported functions by parsing the jump
        table in the CODE 0 resource, but if this resource is missing or not in
        the expected format, it skips this step and does not fail. Generally, if
        any "export_X:" labels appear in the disassembly, then export resolution
        succeeded and all of the labels should be correct (otherwise they will
        all be missing). When passing a CODE resource to --decode-type,
        resource_dasm assumes it's not CODE 0 and will disassemble it as actual
        code rather than an import table.
    *C: Floating-point opcodes (F-class) are not implemented and will
        disassemble as ".extension <opcode> // unimplemented".
    *D: Most PowerPC applications have their executable code in the data fork.
        To disassemble it, use the --disassemble-pef option (example above).
    *E: For color table resources, the raw data is always saved even if it is
        decoded properly, since the original data contains 16-bit values for
        each channel and the output BMP file has less-precise 8-bit channels.

If resource_dasm fails to convert a resource, or doesn't know how to, it will attempt to decode the resource using the corresponding TMPL (template) resource if it exists. If there's no appropriate TMPL, the TMPL is corrupt, or the TMPL can't decode the resource, resource_dasm will produce the resource's raw data instead.

Most of the decoder implementations in resource_dasm are based on reverse-engineering existing software and pawing through the dregs of old documentation, so some rarer types of resources probably won't work yet. However, I want this project to be as complete as possible, so if you have a resource that you think should be decodable but resource_dasm can't decode it, send it to me (perhaps by attaching to a GitHub issue) and I'll try my best to make resource_dasm understand it.

#### Compressed resources

resource_dasm transparently decompresses resources that are marked by the resource manager as compressed.

The resource manager compression scheme was never officially documented by Apple or made public, so the implementation of these decompressors is based on guesswork and reverse-engineering ResEdit and other classic Mac OS code. In summary, resources are decompressed by executing 68K or PowerPC code from a dcmp or ncmp resource, which is either contained in the same file as the compressed resource or in the System file. There are two different formats of compressed resources and two corresponding formats of 68K decompressors; resource_dasm implements support for both formats.

resource_dasm has built-in emulators to run decompressors, but these emulators don't (yet) implement the entire CPU and will fail if a decompressor contains rare opcodes. All four 68K decompressors built into the Mac OS System file (and included with resource_dasm) work properly, as well as Ben Mickaelian's self-modifying decompressor that was used in some After Dark modules and a fairly simple decompressor that may have originally been part of FutureBASIC.

There are probably other decompressors out there that I haven't seen which may not work. If you see "warning: failed to decompress resource" when using resource_dasm, please create a GitHub issue and upload the exported compressed resource (.bin file) that caused the failure, and all the dcmp and ncmp resources from the same source file.

### Using resource_dasm as a library

Run `sudo make install` to copy the header files and library to the relevant paths after building.

You can then `#include <resource_file/ResourceFile.hh>` and create `ResourceFile` objects in your own projects to read and decode resource fork data. Make sure to link with `-lresource_file` as well. There is not much documentation for this library beyond what's in the header file, but usage of the `ResourceFile` class should be fairly straightforward.

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

### gamma_zee_render

gamma_zee_render draws maps of Gamma Zee's mazes. Run it like `./gamma_zee_render <game-filename> <levels-filename>`.

### harry_render

harry_render decodes the levels from Harry the Handsome Executive and draws maps of them. Just put harry_render in the same folder as all the game's data files and run it from there. You'll have to manually supply a clut file, though - run it like `./harry_render --clut-file=System_clut_9.bin` (for example).

### hypercard_dasm

hypercard_dasm decodes HyperCard stacks, producing text files with the metadata and scripts for the stack and each background, card, and part (button or field). It also draws images of the cards with overlays showing where all the buttons and fields are, primarily to aid in digital spelunking of some early Cyan games (e.g. Cosmic Osmo, The Manhole) that use lots of invisible buttons. Run hypercard_dasm with no arguments for usage information.

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
