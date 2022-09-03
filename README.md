# resource_dasm <img align="right" src="s-resource_dasm.png" />

This project contains multiple tools for reverse-engineering classic Mac OS applications and games.

The tools in this project are:
* General tools
  * **resource_dasm**: a utility for working with classic Mac OS resources. It can read resources from classic Mac OS resource forks, Mohawk archives, or HIRF/RMF/IREZ/HSB archives, and convert the resources to modern formats and/or export them verbatim. It can also create and modify resource forks.
  * **libresource_file**: a library implementing most of resource_dasm's functionality.
  * **m68kdasm**: a 68K, PowerPC, and x86 binary disassembler. m68kdasm can also disassemble some common executable formats.
  * **m68kexec**: a 68K, PowerPC, and x86 CPU emulator and debugger.
  * **render_bits**: a raw data renderer, useful for figuring out embedded images or 2-D arrays in unknown file formats.
* Decompressors/dearchivers for specific formats
  * **hypercard_dasm**: disassembles HyperCard stacks and draws card images.
  * **macski_decomp**: decompresses the COOK/CO2K/RUN4 encodings used by MacSki.
  * **flashback_decomp**: decompresses the LZSS compression used in Flashback resources.
  * **render_sprite**: renders sprites from a variety of custom formats (see below).
* Game map generators
  * **ferazel_render**: generates maps from Ferazel's Wand world files.
  * **gamma_zee_render**: generates maps of Gamma Zee mazes.
  * **harry_render**: generates maps from Harry the Handsome Executive world files.
  * **infotron_render**: generates maps from Infotron levels files.
  * **lemmings_render**: generates maps from Lemmings and Oh No! More Lemmings levels and graphics files.
  * **mshines_render**: generates maps from Monkey Shines world files.
  * **realmz_dasm**: generates maps from Realmz scenarios and disassembles the scenario scripts into readable assembly-like syntax.

## Building

* Install Netpbm (http://netpbm.sourceforge.net/). This is only needed for converting PICT resources that resource_dasm can't decode by itself - if you don't care about PICTs, you can skip this step. Also, this is a runtime dependency only; you can install it later if you find that you need it, and you won't have to rebuild resource_dasm.
* Install CMake.
* Build and install phosg (https://github.com/fuzziqersoftware/phosg).
* Run `cmake .`, then `make`.

This project should build properly on sufficiently recent versions of macOS and Linux.

## Using resource_dasm

resource_dasm is a disassembler for classic Mac OS resource forks. It extracts resources from the resource fork of any file and converts many classic Mac OS resource formats (images, sounds, text, etc.) into modern formats.

**Examples:**

Export all resources from a specific file and convert them to modern formats (output is written to the <filename>.out directory by default):
`./resource_dasm files/Tesserae`

Export all resources from all files in a folder, writing the output files into a parallel folder structure in the current directory:
`./resource_dasm "files/Apeiron ƒ/" ./apeiron.out`

Export a specific resource from a specific file, in both modern and original formats:
`./resource_dasm "files/MacSki 1.7/MacSki Sounds" ./macski.out --target-type=snd --target-id=1023 --save-raw=yes`

Export a PowerPC application's resources and disassemble its code:
`./resource_dasm "files/Adventures of Billy" ./billy.out`
`./m68kdasm --pef "files/Adventures of Billy" ./billy.out/dasm.txt`

Export all resources from a Mohawk archive:
`./resource_dasm files/Riven/Data/a_Data.MHK ./riven_data_a.out --index-format=mohawk`

Due to copying files across different types of filesystems, you might have a file's resource fork in the data fork of a separate file instead. To export resources from such a file:
`./resource_dasm "windows/Realmz/Data Files/Portraits.rsf" ./portraits.out --data-fork`

Create a new resource file, with a few TEXT and clut resources:
`./resource_dasm --create --add-resource=TEXT:128@file128.txt --add-resource=TEXT:129@file129.txt --add-resource=clut:2000@clut.bin output.rsrc`

Add a resource to an existing resource file:
`./resource_dasm file.rsrc --add-resource=TEXT:128@file128.txt output.rsrc`

Delete a resource from an existing resource file:
`./resource_dasm file.rsrc --delete-resource=TEXT:128 output.rsrc`

This isn't all resource_dasm can do. Run it without any arguments (or look at `print_usage()` in src/resource_dasm.cc) for a full description of all the options.

### Capabilities

resource_dasm can convert these resource types:

    Type   | Output format                                           | Notes
    ------------------------------------------------------------------------
    Text resources
      bstr | .txt (one file per string)                              | *3
      card | .txt                                                    |
      finf | .txt (description of contents)                          |
      FCMT | .txt                                                    | *3
      FONT | .txt (description) and .bmp (one image per glyph)       |
      MACS | .txt                                                    | *3
      minf | .txt                                                    | *3
      NFNT | .txt (description) and .bmp (one image per glyph)       |
      PSAP | .txt                                                    |
      sfnt | .ttf (TrueType font)                                    |
      STR  | .txt                                                    | *3
      STR# | .txt (one file per string)                              | *3
      styl | .rtf                                                    | *4
      TEXT | .txt                                                    | *3
      wstr | .txt                                                    |
    ------------------------------------------------------------------------
    Image and color resources
      actb | .bmp (24-bit)                                           | *8
      acur | .txt (list of cursor frame IDs)                         |
      cctb | .bmp (24-bit)                                           | *8
      cicn | .bmp (32-bit and monochrome)                            |
      clut | .bmp (24-bit)                                           | *8
      crsr | .bmp (32-bit and monochrome)                            | *1
      CTBL | .bmp (24-bit)                                           |
      CURS | .bmp (32-bit)                                           | *1
      dctb | .bmp (24-bit)                                           | *8
      fctb | .bmp (24-bit)                                           | *8
      icl4 | .bmp (24 or 32-bit)                                     | *0
      icl8 | .bmp (24 or 32-bit) and .icns                           | *0
      icm# | .bmp (32-bit)                                           |
      icm4 | .bmp (24 or 32-bit)                                     | *0
      icm8 | .bmp (24 or 32-bit)                                     | *0
      ICN# | .bmp (32-bit)                                           |
      icns | .icns                                                   |
      ICON | .bmp (24-bit)                                           |
      ics# | .bmp (32-bit)                                           |
      ics4 | .bmp (24 or 32-bit)                                     | *0
      ics8 | .bmp (24 or 32-bit)                                     | *0
      kcs# | .bmp (32-bit)                                           |
      kcs4 | .bmp (24 or 32-bit)                                     | *0
      kcs8 | .bmp (24 or 32-bit)                                     | *0
      PAT  | .bmp (24-bit; pattern and 8x8 tiling)                   |
      PAT# | .bmp (24-bit; pattern and 8x8 tiling for each pattern)  |
      PICT | .bmp (24-bit) or other format                           | *2
      pltt | .bmp (24-bit)                                           | *8
      ppat | .bmp (24-bit; pattern, 8x8, monochrome, monochrome 8x8) |
      ppt# | .bmp (24-bit; 4 images as above for each pattern)       |
      SICN | .bmp (24-bit, one per icon)                             |
      wctb | .bmp (24-bit)                                           | *8
    ------------------------------------------------------------------------
    Sound and sequence resources
      ALIS | .txt (description of contents)                          |
      cmid | .midi                                                   |
      csnd | .wav or .mp3                                            | *5
      ecmi | .midi                                                   |
      emid | .midi                                                   |
      esnd | .wav or .mp3                                            | *5
      ESnd | .wav or .mp3                                            | *5
      INST | .json                                                   | *6
      MADH | .madh (PlayerPRO module)                                |
      MADI | .madi (PlayerPRO module)                                |
      MIDI | .midi                                                   |
      Midi | .midi                                                   |
      midi | .midi                                                   |
      SMSD | .wav                                                    | *A
      snd  | .wav or .mp3                                            | *5
      SONG | .json (smssynth)                                        | *6
      SOUN | .wav                                                    | *A
      Tune | .midi                                                   | *7
      Ysnd | .wav                                                    |
    ------------------------------------------------------------------------
    Code resources
      ADBS | .txt (68K assembly)                                     | *C
      CDEF | .txt (68K assembly)                                     | *C
      cdek | .txt (PPC32 assembly and header description)            |
      cdev | .txt (68K assembly)                                     | *C
      cfrg | .txt (description of code fragments)                    | *D
      citt | .txt (68K assembly)                                     | *C
      clok | .txt (68K assembly)                                     | *C
      cmtb | .txt (68K assembly)                                     | *C
      cmu! | .txt (68K assembly)                                     | *C
      CODE | .txt (68K assembly or import table description)         | *B *C
      code | .txt (68K assembly)                                     | *C
      dcmp | .txt (68K assembly)                                     | *C
      dcod | .txt (PPC32 assembly and header description)            |
      dem  | .txt (68K assembly)                                     | *C
      drvr | .txt (68K assembly)                                     | *C
      DRVR | .txt (68K assembly)                                     | *C
      enet | .txt (68K assembly)                                     | *C
      epch | .txt (PPC32 assembly)                                   |
      expt | .txt (PPC32 assembly)                                   |
      fovr | .txt (PPC32 assembly and header description)            |
      gcko | .txt (68K assembly)                                     | *C
      gdef | .txt (68K assembly)                                     | *C
      GDEF | .txt (68K assembly)                                     | *C
      gnld | .txt (68K assembly)                                     | *C
      INIT | .txt (68K assembly)                                     | *C
      krnl | .txt (PPC32 assembly)                                   |
      LDEF | .txt (68K assembly)                                     | *C
      lmgr | .txt (68K assembly)                                     | *C
      lodr | .txt (68K assembly)                                     | *C
      ltlk | .txt (68K assembly)                                     | *C
      MBDF | .txt (68K assembly)                                     | *C
      MDEF | .txt (68K assembly)                                     | *C
      ncmp | .txt (PPC32 assembly and header description)            |
      ndmc | .txt (PPC32 assembly and header description)            |
      ndrv | .txt (PPC32 assembly and header description)            |
      nift | .txt (PPC32 assembly and header description)            |
      nitt | .txt (PPC32 assembly and header description)            |
      nlib | .txt (PPC32 assembly and header description)            |
      nsnd | .txt (PPC32 assembly and header description)            |
      nsrd | .txt (PPC32 assembly)                                   |
      ntrb | .txt (PPC32 assembly and header description)            |
      osl  | .txt (68K assembly)                                     | *C
      otdr | .txt (68K assembly)                                     | *C
      otlm | .txt (68K assembly)                                     | *C
      PACK | .txt (68K assembly)                                     | *C
      pnll | .txt (68K assembly)                                     | *C
      ppct | .txt (PPC32 assembly and header description)            |
      proc | .txt (68K assembly)                                     | *C
      PTCH | .txt (68K assembly)                                     | *C
      ptch | .txt (68K assembly)                                     | *C
      pthg | .txt (68K or PPC32 assembly and header description)     | *C
      qtcm | .txt (PPC32 assembly and header description)            |
      ROvr | .txt (68K assembly)                                     | *C
      scal | .txt (PPC32 assembly and header description)            |
      scod | .txt (68K assembly)                                     | *C
      SERD | .txt (68K assembly)                                     | *C
      sfvr | .txt (PPC32 assembly and header description)            |
      shal | .txt (68K assembly)                                     | *C
      sift | .txt (68K assembly)                                     | *C
      SMOD | .txt (68K assembly)                                     | *C
      snth | .txt (68K assembly)                                     | *C
      tdig | .txt (68K assembly)                                     | *C
      tokn | .txt (68K assembly)                                     | *C
      vdig | .txt (68K or PPC32 assembly and header description)     | *C
      wart | .txt (68K assembly)                                     | *C
      WDEF | .txt (68K assembly)                                     | *C
      XCMD | .txt (68K assembly)                                     | *C
      XFCN | .txt (68K assembly)                                     | *C
    ------------------------------------------------------------------------
    Miscellaneous resources
      ALRT | .txt (alert parameters)                                 |
      APPL | .txt (description of contents)                          |
      BNDL | .txt (description of contents)                          |
      CMDK | .txt (list of keys)                                     |
      CMNU | .txt (description of menu)                              |
      cmnu | .txt (description of menu)                              |
      CNTL | .txt (description of control)                           |
      CTY# | .txt (description of cities)                            |
      DITL | .txt (dialog parameters)                                |
      DLOG | .txt (dialog parameters)                                |
      errs | .txt (description of error ranges)                      |
      FBTN | .txt (description of buttons)                           |
      FDIR | .txt (description of contents)                          |
      fld# | .txt (description of folders)                           |
      FREF | .txt (description of file references)                   |
      FRSV | .txt (list of font IDs)                                 |
      FWID | .txt (font parameters)                                  |
      GNRL | .txt (description of contents)                          |
      hwin | .txt (description of help window)                       |
      icmt | .txt (icon reference and comment)                       |
      inbb | .txt (description of contents)                          |
      indm | .txt (description of contents)                          |
      infs | .txt (description of contents)                          |
      inpk | .txt (description of contents)                          |
      inra | .txt (description of contents)                          |
      insc | .txt (description of contents)                          |
      ITL1 | .txt (short dates flag value)                           |
      itlb | .txt (internationalization parameters)                  |
      itlc | .txt (internationalization parameters)                  |
      itlk | .txt (keyboard mappings)                                |
      KBDN | .txt (keyboard name)                                    |
      LAYO | .txt (description of layout)                            |
      MBAR | .txt (list of menu IDs)                                 |
      mcky | .txt (threshold values)                                 |
      MENU | .txt (description of menu)                              |
      nrct | .txt (rectangle boundaries)                             |
      PAPA | .txt (printer parameters)                               |
      PICK | .txt (picker parameters)                                |
      ppcc | .txt (description of contents)                          |
      PRC0 | .txt (description of contents)                          |
      PRC3 | .txt (description of contents)                          |
      qrsc | .txt (description of queries)                           |
      resf | .txt (list of fonts)                                    |
      RMAP | .txt (type mapping and list of ID exceptions)           |
      ROv# | .txt (list of overridden resource IDs)                  |
      RVEW | .txt (description of contents)                          |
      scrn | .txt (screen device parameters)                         |
      sect | .txt (description of contents)                          |
      SIGN | .txt (description of contents)                          |
      SIZE | .txt (description of parameters)                        |
      TMPL | .txt (description of format)                            |
      TOOL | .txt (description of contents)                          |
      vers | .txt (version flags and strings)                        |
      WIND | .txt (window parameters)                                |

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
    *5: If the data contained in the snd is in MP3 format, exports it as a .mp3
        file. Otherwise, exports it as an uncompressed WAV file, even if the
        resource's data is compressed. resource_dasm can decompress IMA 4:1,
        MACE 3:1, MACE 6:1, A-law, and mu-law (ulaw) compression.
    *6: JSON files from SoundMusicSys SONG resources can be played with smssynth
        (http://www.github.com/fuzziqersoftware/gctools). The JSON file refers
        to the instrument sounds and MIDI sequence by filename and does not
        include directory names, so if you want to play these, you'll have to
        manually put the sounds and MIDI files in the same directory as the JSON
        file if you're using --filename-format.
    *7: Tune decoding is experimental and will likely produce unplayable MIDIs.
    *8: For color table resources, the raw data is always saved even if it is
        decoded properly, since the original data contains 16-bit values for
        each channel and the output BMP file has less-precise 8-bit channels.
    *A: These resources appear to have a fixed format, with a constant sample
        rate, sample width and channel count. You may have to adjust these
        parameters in the output if it turns out that these are configurable.
    *B: The disassembler attempts to find exported functions by parsing the jump
        table in the CODE 0 resource, but if this resource is missing or not in
        the expected format, it skips this step and does not fail. Generally, if
        any "export_X:" labels appear in the disassembly, then export resolution
        succeeded and all of the labels should be correct (otherwise they will
        all be missing).
    *C: Some coprocessor and floating-point opcodes (F-class) are not
        implemented and will disassemble with the comment "// unimplemented".
    *D: Most PowerPC applications have their executable code in the data fork.
        To disassemble it, use the --disassemble-pef option (example above).

If resource_dasm fails to convert a resource, or doesn't know how to, it will attempt to decode the resource using the corresponding TMPL (template) resource if it exists. If there's no appropriate TMPL, the TMPL is corrupt, or the TMPL can't decode the resource, resource_dasm will produce the resource's raw data instead.

Most of the decoder implementations in resource_dasm are based on reverse-engineering existing software and pawing through the dregs of old documentation, so some rarer types of resources probably won't work yet. However, I want this project to be as complete as possible, so if you have a resource that you think should be decodable but resource_dasm can't decode it, send it to me (perhaps by attaching to a GitHub issue) and I'll try my best to make resource_dasm understand it.

#### Compressed resources

resource_dasm transparently decompresses resources that are marked by the resource manager as compressed.

The resource manager compression scheme was never officially documented by Apple or made public, so the implementation of these decompressors is based on reverse-engineering ResEdit and other classic Mac OS code. In summary, resources are decompressed by executing 68K or PowerPC code from a dcmp or ncmp resource, which is looked up at runtime in the chain of open resource files like most other resources are. (In practice, the relevant dcmp/ncmp is usually contained in either the same file as the compressed resource or in the System file.) There are two different formats of compressed resources and two corresponding formats of 68K decompressors; resource_dasm implements support for both formats.

resource_dasm contains native implementations of all four decompressors built into the Mac OS System file. Specifically:
* dcmp 0 (DonnDecompress) works
* dcmp 1 (a variant of DonnDecompress) is not tested
* dcmp 2 (GreggyDecompress) is not tested
* dcmp 3 (an LZSS-like scheme) works

resource_dasm has built-in 68K and PowerPC emulators to run non-default decompressors. These emulators can also run the default decompressors, which are included with resource_dasm. Current status of emulated decompressors:
* System dcmp 0 works
* System dcmp 1 is not tested
* System dcmp 2 is not tested
* System dcmp 3 works
* Ben Mickaelian's self-modifying dcmp 128 from After Dark works
* FutureBASIC (probably) dcmp 200 works

There may be other decompressors out there that I haven't seen, which may not work. If you see "warning: failed to decompress resource" when using resource_dasm, please create a GitHub issue and upload the exported compressed resource (.bin file) that caused the failure, and all the dcmp and ncmp resources from the same source file.

### Using resource_dasm as a library

Run `sudo make install` to copy the header files and library to the relevant paths after building. After installation, you can `#include <resource_file/IndexFormats/ResourceFork.hh>` (for example) and link with `-lresource_file`. There is no documentation for this library beyond what's written in the header files.

The library contains the following useful functions and classes:
* AudioCodecs.hh: MACE3/6, IMA4, mu-law and A-law audio decoders
* Decompressors/System.hh: Decompressors for standard resource compression formats
* Emulators/M68KEmulator.hh: 68000 CPU emulator and disassembler
* Emulators/PPC32Emulator.hh: PowerPC CPU emulator, assembler, and disassembler
* Emulators/X86Emulator.hh: x86 CPU emulator and disassembler
* ExecutableFormats/...: Parsers for various executable formats
* IndexFormats/ResourceFork.hh: Parser and serializer for Classic Mac OS resource forks
* IndexFormats/HIRF.hh: Parser for HIRF/IREZ/HSB/RMF archives
* IndexFormats/Mohawk.hh: Parser for Mohawk archives
* IndexFormats/DCData.hh: Parser for DC Data archives
* LowMemoryGlobals.hh: Structure definition and field lookup for Classic Mac OS low-memory global variables
* ResourceFile.hh: Loaded representation of a resource archive, with decoding functions for many types
* TrapInfo.hh: Index of Classic Mac OS 68K system calls

## Using m68kdasm

Using m68kdasm is fairly straightforward. Run `m68kdasm --help` for a full list of options.

Currently m68kdasm can disassemble these types of data:
* Raw 68K, PowerPC, or x86 code
* PEF (Classic Mac OS PowerPC application) files
* DOL (Nintendo Gamecube application) files
* REL (Nintendo Gamecube library) files
* PE (Windows EXE/DLL/etc.) files
* ELF files

Some of these formats support CPU architectures that m68kdasm does not support; if it encounters one of these, it prints the code segments as data segments instead.

## Using m68kexec

m68kexec is a CPU emulator and debugger for the Motorola 68000, 32-bit PowerPC, and x86 architectures. I often use it to help understand what some archaic code is trying to do, or to compare the behavior of code that I've transcribed to a modern language with the original code's behavior. For use cases like this, you generally will want to set up one or more input regions containing the data you're testing with, and one or more output regions for the emulated code to write to.

Perhaps this is best explained by example. This command is used to execute the encryption context generation function from Phantasy Star Online Blue Burst, to compare it with [the same function as implemented in newserv](https://github.com/fuzziqersoftware/newserv/blob/342f819f50cbde25816c1cd7f72c5ec0f3369994/src/PSOEncryption.cc#L288):

    ./m68kexec --x86 --trace \
        --mem=A0000000/2AC43585C46A6366188889BCE3DB88B15C2B3C751DB6757147E7E9390598275CC79547B2E5C00DD145002816B59C067C
        --mem=A1000000:1048 \
        --load-pe=files/windows/pso/psobb.exe \
        --pc=00763FD0 \
        --reg=ecx:A1000000 \
        --push=00000030 \
        --push=A0000000 \
        --push=FFFFFFFF \
        --breakpoint=FFFFFFFF

The `--mem` options set up the input regions; the A0000000 region contains the encryption seed (0x30 bytes) and the A1000000 region will contain the generated encryption context when the function returns. The `--load-pe` option loads the code to be executed and `--pc` tells the emulator where to start. (By default, it will start at the entrypoint defined in the executable, if any is given; here, we want to call a specific function instead.) The `--reg` option sets the `this` pointer in the function to the space we allocated for it. The `--push` options set the function's arguments and return address. It will return to FFFFFFFF, which has no allocated memory, but we've also set a `--breakpoint` at that address which will stop emulation just before an exception is thrown.

Since we used `--trace`, the emulator prints the registers' state after every opcode, so we can trace through its behavior and compare it with our external implementation of the same function. When the function returns and triggers the breakpoint, we can use `r A1000000 1048` in the shell to see the data that it generated, and also compare that to our external function's result.

## Using the other tools

### render_bits

render_bits is useful to answer the question "might this random-looking binary data actually be an image or 2-D array?" Give it a color format and some binary data, and it will produce a full-color BMP file that you can look at with your favorite image viewer or editor. You can also give a color table (.bin file produced by resource_dasm from a clut resource) if you think the input is indexed color data. If the output looks like garbage, play around with the width and color format until you figure out the right parameters.

Run render_bits without any options for usage information.

### Decompressors/dearchivers for specific formats

* For HyperCard stacks: `hypercard_dasm stack_file [output_dir]`, or just `hypercard_dasm` to see all options
* For MacSki compressed resources: `macski_decomp < infile > outfile`, or use directly with resource_dasm like `resource_dasm --external-preprocessor=./macski_decomp input_filename ...`
* For Flashback compressed resources: `flashback_decomp < infile > outfile`, or use directly with resource_dasm like `resource_dasm --external-preprocessor=./flashback_decomp input_filename ...`

### render_sprite

render_sprite can render several custom game sprite formats. For some formats listed below, you'll have to provide a color table resource in addition to the sprite resource. A .bin file produced by resource_dasm from a clut, pltt, or CTBL resource will suffice; usually these can be found in the same file as the sprite resources or in the game application. Run render_sprite with no arguments for usage information.

Supported formats:

    Game                         | Type | CLI option              | Notes
    ---------------------------------------------------------------------
    Beyond Dark Castle           | PPCT | --ppct=PPCT_file.bin    |
    Beyond Dark Castle           | PSCR | --pscr-v2=PSCR_file.bin |
    Bubble Trouble               | btSP | --btsp=btSP_file.bin    | *0
    Dark Castle (color)          | DC2  | --dc2=DC2_file.bin      | *4
    Dark Castle (monochrome)     | PPCT | --ppct=PPCT_file.bin    |
    Dark Castle (monochrome)     | PSCR | --pscr-v1=PSCR_file.bin |
    Factory                      | 1img | --1img=1img_file.bin    |
    Factory                      | 4img | --4img=4img_file.bin    | *0
    Factory                      | 8img | --8img=8img_file.bin    | *0
    Greebles                     | GSIF | --gsif=GSIF_file.bin    | *0
    Harry the Handsome Executive | HrSp | --hrsp=HrSp_file.bin    | *0 *1
    Lemmings                     | SHPD | --shpd-coll-v1=file     | *5
    Oh No! More Lemmings         | SHPD | --shpd-coll-v2=file     | *5
    Prince of Persia             | SHPD | --shpd-coll-p=file      | *5
    Prince of Persia 2           | SHAP | --shap=SHAP_file.bin    | *0
    SimCity 2000                 | SPRT | --sprt=SPRT_file.bin    | *0 *2
    Spectre                      | shap | --shap-3d=shap_file.bin | *6
    Step On It!                  | sssf | --sssf=sssf_file.bin    | *0 *2
    Swamp Gas                    | PPic | --ppic=PPic_file.bin    | *2 *3
    TheZone                      | Spri | --spri=Spri_file.bin    | *0

    Notes:
    *0: A color table is required to render these sprites.
    *1: The game doesn't contain any cluts. You can use a 256-color clut from
        the Mac OS System file instead.
    *2: These sprite formats contain multiple images, so render_sprite will
        produce multiple .bmp files.
    *3: Resources of this type can contain embedded color tables; if you're
        rendering a color PPic that doesn't have a color table, you'll have to
        provide one via a command-line option. If the resource (or individual
        images therein) contain their own color tables or are monochrome, no
        color table is required on the command line, and any provided color
        table via the command line will be ignored.
    *4: You can get DC2 sprites from the DC Data file with
        `resource_dasm --index-format=dc-data "DC Data"`.
    *5: The graphics files contain resources that refer to segments of the data
        fork in the same file. So, this option expects the original Graphics or
        BW Graphics or Persia file (with both data and resource forks present),
        not an already-extracted resource. If you give a clut on the command
        line, render_sprite will decode the graphics as color; otherwise it will
        decode them as monochrome.
    *6: shap resources contain 3D models and 2D top-down projections of them.
        When given a shap resource, render_sprite produces an STL file and an
        OBJ file for the 3D model, and an SVG file for the 2D top-down view.

### Game map generators

* For Ferazel's Wand maps: `ferazel_render` in the directory with the data files, or `ferazel_render --help` to see all the options (there are many!)
* For Gamma Zee maps: `gamma_zee_render gamma_zee_application levels_filename`
* For Harry the Handsome Executive maps: `harry_render --clut-file=clut.bin`, or just `harry_render` to see all the options (there are many!)
* For Infotron maps: `infotron_render` in the Info Datafiles directory
* For Lemmings (Mac version) maps: `lemmings_render --clut-file=clut.bin`, or `lemmings_render --help` to see all the options
* For Monkey Shines maps: `mshines_render world_file [output_directory]`
* For Oh No! More Lemmings (Mac version) maps: Use `lemmings_render` as for original Lemmings, but also use the `--v2` option
* For Realmz maps and scripts: `realmz_dasm global_data_dir [scenario_dir] out_dir` (if scenario_dir is not given, disassembles the shared data instead)
