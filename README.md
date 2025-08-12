# resource_dasm <img align="right" src="s-resource_dasm.png" />

This project contains multiple tools for reverse-engineering applications and games. Most of these tools are targeted at classic Mac OS (pre-OSX); a few are targeted at Nintendo GameCube games.

The tools in this project are:
* General tools
  * **resource_dasm**: A utility for working with classic Mac OS resources. It can read resources from classic Mac OS resource forks, AppleSingle/AppleDouble files, MacBinary files, Mohawk archives, or HIRF/RMF/IREZ/HSB archives, and convert the resources to modern formats and/or export them verbatim. It can also create and modify resource forks.
  * **libresource_file**: A library implementing most of resource_dasm's functionality.
  * **m68kdasm**: A 68K, PowerPC, x86, and SH-4 binary assembler and disassembler. m68kdasm can also disassemble some common executable formats.
  * **m68kexec**: A 68K, PowerPC, x86, and SH-4 CPU emulator and debugger.
  * **render_bits**: Renders raw data in a variety of color formats, including indexed formats. Useful for finding embedded images or understanding 2-dimensional arrays in unknown file formats.
  * **replace_clut**: Remaps an existing image from one indexed color space to another.
  * **assemble_images**: Combines multiple images into one. Useful for dealing with games that split large images into multiple smaller images due to format restrictions.
  * **dupe_finder**: Finds duplicate resources across multiple resource files.
* Tools for specific formats
  * **render_text**: Renders text using bitmap fonts from FONT or NFNT resources.
  * **hypercard_dasm**: Disassembles HyperCard stacks and draws card images.
  * **decode_data**: Decodes some custom compression formats (see below).
  * **render_sprite**: Renders sprites from a variety of custom formats (see below).
  * **icon_unarchiver**: Exports icons from an Icon Archiver archive to .icns (see below).
  * **vrfsdump**: Extracts the contents of VRFS archives from Blobbo.
  * **gcmdump**: Extracts all files in a GCM file (GameCube disc image) or TGC file (embedded GameCube disc image).
  * **gcmasm**: Generates a GCM image from a directory tree.
  * **gvmdump**: Extracts all files in a GVM archive (from Phantasy Star Online) to the current directory, and converts the GVR textures to Windows BMP files. Also can decode individual GVR files outside of a GVM archive.
  * **rcfdump**: Extracts all files in a RCF archive (from The Simpsons: Hit and Run) to the current directory.
  * **smsdumpbanks**: Extracts the contents of JAudio instrument and waveform banks in AAF, BX, or BAA format (from Super Mario Sunshine, Luigi's Mansion, Pikmin, and other games). See "Using smssynth" for more information.
  * **smssynth**: Synthesizes and debugs music sequences in BMS format (from Super Mario Sunshine, Luigi's Mansion, Pikmin, and other games) or MIDI format (from classic Macintosh games). See "Using smssynth" for more information.
  * **modsynth**: Synthesizes and debugs music sequences in Protracker/Soundtracker MOD format.
* Game map generators
  * **blobbo_render**: Generates maps from Blobbo levels.
  * **bugs_bannis_render**: Generates maps from Bugs Bannis levels.
  * **ferazel_render**: Generates maps from Ferazel's Wand world files.
  * **gamma_zee_render**: Generates maps of Gamma Zee mazes.
  * **harry_render**: Generates maps from Harry the Handsome Executive world files.
  * **infotron_render**: Generates maps from Infotron levels files.
  * **lemmings_render**: Generates maps from Lemmings and Oh No! More Lemmings levels and graphics files.
  * **mshines_render**: Generates maps from Monkey Shines world files.
  * **realmz_dasm**: Generates maps from Realmz scenarios and disassembles the scenario scripts into readable assembly-like syntax.

## Building

* Install required dependencies:
  * Install zlib, if you somehow don't have it already. (macOS and most Linuxes come with it preinstalled, but some Linuxes like Raspbian may not. If your Linux doesn't have it, you can `apt-get install zlib1g-dev`.)
  * Install CMake.
  * Build and install phosg (https://github.com/fuzziqersoftware/phosg).
* Install optional dependencies:
  * Install Netpbm (http://netpbm.sourceforge.net/). This is only needed for converting PICT resources that resource_dasm can't decode by itself - if you don't care about PICTs, you can skip this step. Also, this is a runtime dependency only; you can install it later if you find that you need it, and you won't have to rebuild resource_dasm.
  * Install SDL3. This is only needed for modsynth and smssynth to be able to play songs live; without SDL, they will still build and can still generate WAV files.
* Run `cmake .`, then `make`.
* If you're building another project that depends on resource_dasm, run `sudo make install`.

This project should build properly on sufficiently recent versions of macOS and Linux.

## Using resource_dasm

resource_dasm is a disassembler for classic Mac OS resource forks. It extracts resources from the resource fork of any file and converts many classic Mac OS resource formats (images, sounds, text, etc.) into modern formats.

**Examples:**

* Export all resources from a specific file and convert them to modern formats (output is written to the \<filename\>.out directory by default): `./resource_dasm files/Tesserae`
* Export all resources from all files in a folder, writing the output files into a parallel folder structure in the current directory: `./resource_dasm "files/Apeiron ƒ/" ./apeiron.out`
* Export a specific resource from a specific file, in both modern and original formats: `./resource_dasm "files/MacSki 1.7/MacSki Sounds" ./macski.out --target-type=snd --target-id=1023 --save-raw=yes`
* Export a PowerPC application's resources and disassemble its code: `./resource_dasm "files/Adventures of Billy" ./billy.out && ./m68kdasm "files/Adventures of Billy" ./billy.out/dasm.txt`
* Export all resources from a Mohawk archive: `./resource_dasm files/Riven/Data/a_Data.MHK ./riven_data_a.out --index-format=mohawk`
* Due to copying files across different types of filesystems, you might have a file's resource fork in the data fork of a separate file instead. To export resources from such a file: `./resource_dasm "windows/Realmz/Data Files/Portraits.rsf" ./portraits.out --data-fork`
* Create a new resource file, with a few TEXT and clut resources: `./resource_dasm --create --add-resource=TEXT:128@file128.txt --add-resource=TEXT:129@file129.txt --add-resource=clut:2000@clut.bin output.rsrc`
* Add a resource to an existing resource file: `./resource_dasm file.rsrc --add-resource=TEXT:128@file128.txt output.rsrc`
* Delete a resource from an existing resource file: `./resource_dasm file.rsrc --delete-resource=TEXT:128 output.rsrc`

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
      FONT | .txt (description) and image (one image per glyph)      | *E
      lstr | .txt                                                    | *3
      MACS | .txt                                                    | *3
      minf | .txt                                                    | *3
      mstr | .txt                                                    | *3
      mst# | .txt (one file per string)                              | *3
      NFNT | .txt (description) and image (one image per glyph)      | *E
      PSAP | .txt                                                    |
      sfnt | .ttf (TrueType font)                                    |
      STR  | .txt                                                    | *3
      STR# | .txt (one file per string)                              | *3
      styl | .rtf                                                    | *4
      TEXT | .txt                                                    | *3
      TwCS | .txt (one file per string)                              |
      wstr | .txt                                                    |
    ------------------------------------------------------------------------
    Image and color resources
      actb | image (24-bit)                                          | *E *8
      acur | .txt (list of cursor frame IDs)                         |
      cctb | image (24-bit)                                          | *E *8
      cicn | image (32-bit and monochrome)                           | *E
      clut | image (24-bit)                                          | *E *8
      crsr | image (32-bit and monochrome)                           | *E *1
      CTBL | image (24-bit)                                          | *E
      CURS | image (32-bit)                                          | *E *1
      dctb | image (24-bit)                                          | *E *8
      fctb | image (24-bit)                                          | *E *8
      icl4 | image (24 or 32-bit) and .icns                          | *E *0
      icl8 | image (24 or 32-bit) and .icns                          | *E *0
      icm# | image (32-bit)                                          | *E
      icm4 | image (24 or 32-bit)                                    | *E *0
      icm8 | image (24 or 32-bit)                                    | *E *0
      ICN# | image (32-bit) and .icns                                | *E
      icns | image, .png, .jp2, .txt, .plist, .bin, etc.             | *E *9
      ICON | image (24-bit)                                          | *E
      ics# | image (32-bit) and .icns                                | *E
      ics4 | image (24 or 32-bit) and .icns                          | *E *0
      ics8 | image (24 or 32-bit) and .icns                          | *E *0
      kcs# | image (32-bit)                                          | *E
      kcs4 | image (24 or 32-bit)                                    | *E *0
      kcs8 | image (24 or 32-bit)                                    | *E *0
      PAT  | image (24-bit; pattern and 8x8 tiling)                  | *E
      PAT# | image (24-bit; pattern and 8x8 tiling for each pattern) | *E
      PICT | image (24-bit) or other format                          | *E *2
      pltt | image (24-bit)                                          | *E *8
      ppat | image (24-bit; color, color 8x8, mono, mono 8x8)        | *E
      ppt# | image (24-bit; 4 images as above for each pattern)      | *E
      SICN | image (24-bit, one per icon)                            | *E
      wctb | image (24-bit)                                          | *E *8
    ------------------------------------------------------------------------
    Sound and sequence resources
      .mod | .mod (ProTracker module)                                |
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
      adio | .txt (68K assembly)                                     | *C
      AINI | .txt (68K assembly)                                     | *C
      atlk | .txt (68K assembly)                                     | *C
      boot | .txt (68K assembly)                                     | *C
      CDEF | .txt (68K assembly)                                     | *C
      cdek | .txt (PPC32 assembly and header description)            |
      cdev | .txt (68K assembly)                                     | *C
      CDRV | .txt (68K assembly)                                     | *C
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
      dimg | .txt (68K assembly)                                     | *C
      drvr | .txt (68K assembly)                                     | *C
      DRVR | .txt (68K assembly)                                     | *C
      enet | .txt (68K assembly)                                     | *C
      epch | .txt (PPC32 assembly)                                   |
      expt | .txt (PPC32 assembly)                                   |
      FKEY | .txt (68K assembly)                                     | *C
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
      mntr | .txt (68K assembly)                                     | *C
      ncmp | .txt (PPC32 assembly and header description)            |
      ndlc | .txt (PPC32 assembly and header description)            |
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
      RSSC | .txt (68K assembly)                                     | *C
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
    MacApp resources
      68k! | .txt (description of memory config for 680x0)           |
      CMNU | .txt (description of menu)                              |
      cmnu | .txt (description of menu)                              |
      errs | .txt (description of error ranges)                      |
      mem! | .txt (description of memory config)                     |
      ppc! | .txt (description of memory config for PPC)             |
      res! | .txt (string list of always resident segments)          |
      seg! | .txt (string list of segments)                          |
      TxSt | .txt (description of text style)                        |
    ------------------------------------------------------------------------
    Miscellaneous resources
      ALRT | .txt (alert parameters)                                 |
      APPL | .txt (description of contents)                          |
      audt | .txt (description of contents)                          |
      BNDL | .txt (description of contents)                          |
      CMDK | .txt (list of keys)                                     |
      CNTL | .txt (description of control)                           |
      CTY# | .txt (description of cities)                            |
      dbex | .txt (description of contents)                          |
      DITL | .txt (dialog parameters)                                |
      DLOG | .txt (dialog parameters)                                |
      FBTN | .txt (description of buttons)                           |
      FDIR | .txt (description of contents)                          |
      fld# | .txt (description of folders)                           |
      flst | .txt (description of font family list)                  |
      fmap | .txt (description of finder icon mappings)              |
      FREF | .txt (description of file references)                   |
      FRSV | .txt (list of font IDs)                                 |
      FWID | .txt (font parameters)                                  |
      gbly | .txt (description of Gibbly aka. System Enabler)        |
      GNRL | .txt (description of contents)                          |
      hwin | .txt (description of help window)                       |
      icmt | .txt (icon reference and comment)                       |
      inbb | .txt (description of contents)                          |
      indm | .txt (description of contents)                          |
      infs | .txt (description of contents)                          |
      inpk | .txt (description of contents)                          |
      inra | .txt (description of contents)                          |
      insc | .txt (description of contents)                          |
      itl0 | .txt (international formatting information)             |
      ITL1 | .txt (short dates flag value)                           |
      itlb | .txt (internationalization parameters)                  |
      itlc | .txt (internationalization parameters)                  |
      itlk | .txt (keyboard mappings)                                |
      KBDN | .txt (keyboard name)                                    |
      LAYO | .txt (description of layout)                            |
      mach | .txt (description of contents)                          |
      MBAR | .txt (list of menu IDs)                                 |
      mcky | .txt (threshold values)                                 |
      MENU | .txt (description of menu)                              |
      mitq | .txt (description of queue sizes)                       |
      nrct | .txt (rectangle boundaries)                             |
      PAPA | .txt (printer parameters)                               |
      PICK | .txt (picker parameters)                                |
      ppcc | .txt (description of contents)                          |
      ppci | .txt (description of contents)                          |
      PRC0 | .txt (description of contents)                          |
      PRC3 | .txt (description of contents)                          |
      pslt | .txt (description of Nubus pseudo-slot lists)           |
      ptbl | .txt (description of patch table)                       |
      qrsc | .txt (description of queries)                           |
      RECT | .txt (description of the rectangle)                     |
      resf | .txt (list of fonts)                                    |
      RMAP | .txt (type mapping and list of ID exceptions)           |
      ROv# | .txt (list of overridden resource IDs)                  |
      rtt# | .txt (list of database result handlers)                 |
      RVEW | .txt (description of contents)                          |
      scrn | .txt (screen device parameters)                         |
      sect | .txt (description of contents)                          |
      SIGN | .txt (description of contents)                          |
      SIZE | .txt (description of parameters)                        |
      slut | .txt (description of mapping)                           |
      thn# | .txt (description of 'thng' mapping)                    |
      TMPL | .txt (description of format)                            |
      TOOL | .txt (description of contents)                          |
      vers | .txt (version flags and strings)                        |
      WIND | .txt (window parameters)                                |

    Notes:
    *0: Produces a 32-bit image if a corresponding monochrome resource exists
        (ICN# for icl4/8, icm# for icm4/8, ics# for ics4/8, kcs# for kcs4/8). If
        no monochrome resource exists, produces a 24-bit image instead. All
        color information in the original resource is reproduced in the output,
        even for fully-transparent pixels. If the icon was originally intended
        to be used with a nonstandard compositing mode, the colors of fully-
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
        result in a JPEG or PNG file rather than the format specified by
        --image-format (which is BMP by default). If the internal decoder fails,
        resource_dasm will fall back to a decoder that uses picttoppm, which is
        part of NetPBM. There is a rare failure mode in which picttoppm hangs
        forever; resource_dasm gives it 10 seconds to do its job before killing
        it and giving up. If picttoppm is not installed, fails to decode the
        PICT, or is killed due to a timeout, resource_dasm will prepend the
        necessary header and save the data as a PICT file instead.
    *3: Text is assumed to use the Mac OS Roman encoding. It is converted to
        UTF-8, and line endings (\r) are converted to Unix style (\n).
    *4: Some rare style options may not be translated correctly. styl resources
        provide styling information for the TEXT resource with the same ID, so
        such a resource must be present to properly decode a styl.
    *5: RMF archives can contain snd resources that are actually in MP3 format;
        in this case, the exported sound will be a .mp3 file. Otherwise, the
        exported sound is an uncompressed WAV file, even if the resource's data
        is compressed. resource_dasm can decompress IMA 4:1, MACE 3:1, MACE 6:1,
        A-law, and mu-law (ulaw) compression.
    *6: JSON files from SoundMusicSys SONG resources can be played with smssynth
        (http://www.github.com/fuzziqersoftware/gctools). The JSON file refers
        to the instrument sounds and MIDI sequence by filename and does not
        include directory names, so if you want to play these, you'll have to
        manually put the sounds and MIDI files in the same directory as the JSON
        file if you're using --filename-format.
    *7: Tune decoding is experimental and will likely produce unplayable MIDIs.
    *8: For color table resources, the raw data is always saved even if it is
        decoded properly, since the original data contains 16-bit values for
        each channel and the output image file has less-precise 8-bit channels.
    *9: icns resources are decoded into many different file types depending on
        the contents of the resource. For subfields that have split alpha
        channels (that is, the transparency data is in a different subfield),
        resource_dasm produces an original image and one with transparency
        applied. Some icns resources also contain metadata, which is exported as
        .bin, .txt, and .plist files, except for the Icon Composer version used
        to create the file, which is ignored. If you want the result in Icon
        Composer format, use --save-raw=yes and resource_dasm will save it as a
        .icns file.
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
        To disassemble it, use m68kdasm (example above).
    *E: The output image format can be specified using --image-format. The
        default output format is bmp (Windows bitmap); other supported formats
        are png and ppm.

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
* DataCodecs/Codecs.hh: Decompressors and compressors for some common and custom data formats
* Emulators/M68KEmulator.hh: 68000 CPU emulator and disassembler
* Emulators/PPC32Emulator.hh: PowerPC CPU emulator, assembler, and disassembler
* Emulators/SH4Emulator.hh: SuperH-4 assembler and disassembler (not actually an emulator yet)
* Emulators/X86Emulator.hh: x86 CPU emulator, assembler, and disassembler
* ExecutableFormats/...: Parsers for various executable formats
* IndexFormats/Formats.hh: Parsers and serializers for various resource archive formats
* Lookups.hh: Indexes of some constant values used in multiple resource types
* LowMemoryGlobals.hh: Structure definition and field lookup for Classic Mac OS low-memory global variables
* ResourceDecompressors/System.hh: Decompressors for standard resource compression formats
* ResourceFile.hh: Loaded representation of a resource archive, with decoding functions for many types
* ResourceTypes.hh: Constants representing many common resource types
* TextCodecs.hh: Mac OS Roman decoder
* TrapInfo.hh: Index of Classic Mac OS 68K system calls

## Using m68kdasm

Using m68kdasm is fairly straightforward. Run `m68kdasm --help` for a full list of options.

Currently m68kdasm can disassemble these types of data:
* Raw 68K, PowerPC, x86, or SH-4 binary code
* PEF (Classic Mac OS PowerPC executable) files
* DOL (Nintendo Gamecube executable) files
* REL (Nintendo Gamecube library) files
* PE (Windows EXE/DLL/etc.) files
* XBE (Microsoft Xbox executable) files
* ELF files

Some of these executable formats support CPU architectures that m68kdasm does not support; if it encounters one of these, it prints the code segments as data segments instead.

m68kdasm can also assemble PowerPC, x86, and SH-4 assembly into raw binary. (It does not support assembling M68K text into binary, but this will be implemented in the future.) The expected input syntax for each architecture matches the disassembly syntax; for PowerPC and SH-4, this is not the standard syntax used by most other tools.

## Using m68kexec

m68kexec is a CPU emulator and debugger for the Motorola 68000, 32-bit PowerPC, and x86 architectures. I often use it to help understand what some archaic code is trying to do, or to compare the behavior of code that I've transcribed to a modern language with the original code's behavior. For use cases like this, you generally will want to set up one or more input regions containing the data you're testing with, and one or more output regions for the emulated code to write to.

Perhaps this is best explained by example. This command is used to execute the encryption context generation function from Phantasy Star Online Blue Burst, to compare it with [the same function as implemented in newserv](https://github.com/fuzziqersoftware/newserv/blob/342f819f50cbde25816c1cd7f72c5ec0f3369994/src/PSOEncryption.cc#L288):

    ./m68kexec --x86 --trace \
        --mem=A0000000/2AC43585C46A6366188889BCE3DB88B15C2B3C751DB6757147E7E9390598275CC79547B2E5C00DD145002816B59C067C \
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

## Using smssynth

**smssynth** deals with BMS and MIDI music sequence programs. It can disassemble them, convert them into .wav files, or play them in realtime. The implementation is based on reverse-engineering multiple games and not on any official source code, so sometimes the output sounds a bit different from the actual in-game music.

### Usage for GameCube and Wii games

Before running smssynth, you may need to do the steps in the "Getting auxiliary files" section below. Also, for sequences that loop, smssynth will run forever unless you hit Ctrl+C or give a time limit.

Once you have the necessary files, you can find out what the available sequences are with the `--list` option, play sequences with the `--play` option, or produce WAV files from the sequences with the `--output-filename` option.

Here are some usage examples for GameCube games:
- List all the sequences in Luigi's Mansion: `smssynth --audiores-directory=luigis_mansion_extracted_data/AudioRes --list`
- Convert Bianco Hills (from Super Mario Sunshine) to 4-minute WAV, no Yoshi drums: `smssynth --audiores-directory=sms_extracted_data/AudioRes k_bianco.com --disable-track=15 --output-filename=k_bianco.com.wav --time-limit=240`
- Play Bianco Hills (from Super Mario Sunshine) in realtime, with Yoshi drums: `smssynth --audiores-directory=sms_extracted_data/AudioRes k_bianco.com --play`
- Play The Forest Navel (from Pikmin) in realtime: `smssynth --audiores-directory=pikmin_extracted_data/dataDir/SndData --play cave.jam`

### Usage for Classic Mac OS games

smssynth can also disassemble and play MIDI files from games that use SoundMusicSys (miniBAE). To play these sequences, provide a JSON environment file produced by resource_dasm from a SONG resource. Make sure not to move or rename any of the other files in the same directory as the JSON file, or it may not play properly - the JSON file refers to the instrument samples by filename. You can produce an appropriate JSON file by running resource_dasm like `resource_dasm "Creep Night Demo Music" ./creep_night.out`. This will produce a JSON file for each SONG resource contained in the input file.

After doing this, you can play the songs with (for example) `smssynth --json-environment="./creep_night.out/Creep Night Demo Music_SONG_1000_smssynth_env.json" --play`. The `--disassemble` and `--output-filename` options also work when using JSON files (like for JAudio/BMS), but `--list` does not.

### Compatibility

I've tested smssynth with the following GameCube games that use JAudio/BMS and assigned an approximate correctness value for each one:
- __Luigi's Mansion__: 60%. Most songs sound close to in-game audio, but a few instruments are clearly wrong and some effects are missing. I think this makes the staff roll sequence sound cooler, but I still intend to fix it.
- __Mario Kart: Double Dash!!__: 80%. All songs work; some volume effects appear to be missing so they sound a little different.
- __Pikmin__: 70%. The game uses track volume effects to change how songs sound based on what's happening in-game; smssynth doesn't do this, so the songs sound a little different from how they sound in-game but are easily recognizable.
- __Super Mario Sunshine__: 95%. Most songs sound perfect (exactly as they sound in-game); only a few are broken. Note that the game uses track 15 for Yoshi's drums; use `--disable-track=15` to silence them.
- __The Legend of Zelda: Twilight Princess__: <20%. Most songs don't play or sound terrible. Some are recognizable but don't sound like the in-game music.
- __Super Mario Galaxy__: <20%. Same as above.

Classic Mac OS games that use SoundMusicSys currently fare much better than JAudio games:
- __After Dark__: 100%
- __Castles - Siege and Conquest__: 100%
- __ClockWerx__: 100%
- __Creep Night Pinball__: 100%
- __DinoPark Tycoon__: 100%
- __Flashback__: 100%
- __Holiday Lemmings__: 100%
- __Lemmings__: 100%
- __Mario Teaches Typing__: 100%
- __Monopoly CD-ROM__: 100%, but the songs sound different than they sound in-game. This is because the original SoundMusicSys implementation drops some notes in e.g. _Free Parking_, but smssynth does not.
- __Odell Down Under__: 100%
- __Oh No! More Lemmings__: 100%
- __Prince of Persia__: 100%
- __Prince of Persia 2__: 100%. There are no SONG resources in this game; instead, use `resource_dasm --index-format=mohawk` to get the MIDI files from NISMIDI.dat and MIDISnd.dat and use the template JSON environment generated by resource_dasm from the game application. That is, provide both --json-environment and a MIDI file on the command line.
- __SimAnt__: 100%
- __SimCity 2000__: 100%
- __SimTown (demo)__: 100%. If the full version has more songs, they will probably work, but are not yet tested.
- __Snapdragon__: 100%
- __The Amazon Trail__: 100%
- __The Yukon Trail__: 100%
- __Troggle Trouble Math__: 100%
- __Ultimate Spin Doctor__: 100%
- __Widget Workshop__: 100%

### Getting auxiliary files from GameCube games

Luigi's Mansion should work without any modifications. Just point `--audiores-directory` at the directory extracted from the disc image.

#### Getting msound.aaf from Super Mario Sunshine

You'll have to copy msound.aaf into the AudioRes directory manually to use the Super Mario Sunshine tools. To do so:
- Get nintendo.szs from the disc image (use gcmdump or some other tool).
- Yaz0-decompress it (use yaz0dec, which is part of [szstools](http://amnoid.de/gc/)).
- Extract the contents of the archive (use rarcdump, which is also part of [szstools](http://amnoid.de/gc/)).
- Copy msound.aaf into the AudioRes directory.

#### Getting sequence.barc from Pikmin

You'll have to manually extract the BARC data from default.dol (it's embedded somewhere in there). Open up default.dol in a hex editor and search for the ASCII string "BARC----". Starting at the location where you found "BARC----", copy at least 0x400 bytes out of default.dol and save it as sequence.barc in the SndData/Seqs/ directory. Now you should be able to run smsdumpbanks and smssynth using the Pikmin sound data. `--audiores-directory` should point to the SndData directory from the Pikmin disc (with sequence.barc manually added).

#### Getting Banks directory from Mario Kart: Double Dash

After extracting the AudioRes directory, rename the Waves subdirectory to Banks.

#### Getting files from The Legend of Zelda: Twilight Princess

The sequences are stored in a compressed RARC file, and don't appear to be listed in the environment index. (This means `--list` won't work and you'll have to specify a sequence file manually.) To get the sequences:
- Decompress the sequence file using yaz0dec (from [szstools](http://amnoid.de/gc/))
- Extract the sequences using rarcdump (also from [szstools](http://amnoid.de/gc/))

#### Getting files from Super Mario Galaxy

Like Twilight Princess, the sequences are stored in a RARC archive, but this time each individual sequence is compressed, and the index is compressed too. Fortunately they're all Yaz0:

- Decompress the index file using yaz0dec (from [szstools](http://amnoid.de/gc/))
- Extract the sequences using rarcdump (also from [szstools](http://amnoid.de/gc/))
- Decompress the sequences using yaz0dec again for each one

## Using the other tools

### render_bits

render_bits is useful to answer the question "might this random-looking binary data actually be an image or 2-D array?" Give it a color format and some binary data, and it will produce a full-color BMP file that you can look at with your favorite image viewer or editor. You can also give a color table (.bin file produced by resource_dasm from a clut resource) if you think the input is indexed color data. If the output looks like garbage, play around with the width and color format until you figure out the right parameters.

Run render_bits without any options for usage information.

### render_text

render_text lets you see what actual text would look like when rendered with a bitmap font (FONT/NFNT resource). To use it, get a .bin file from a FONT or NFNT resource (e.g. with resource_dasm --save-raw). Then run render_text with no arguments to see how to use it.

### replace_clut

Sometimes in the course of reverse-engineering you'll end up with an image that has the right content and structure, but the colors are completely wrong. Chances are it was rendered with the wrong color table; to fix this, you can use replace_clut to map all of the image's pixels from one colorspace to another.

Run replace_clut without any options for usage information.

### assemble_images

Some games store large images split up into a set of smaller textures; assemble_images can programmatically combine them into a a single large image again. Run assemble_images without any options to see how to use it.

### dupe_finder

dupe_finder finds duplicate resources of the same type in one or several resource files.

Run dupe_finder without any options for usage information.

### Decompressors/dearchivers for specific formats

* For HyperCard stacks: `hypercard_dasm stack_file [output_dir]`, or just `hypercard_dasm` to see all options
* For Alessandro Levi Montalcini's Icon Archiver: `icon_dearchiver archive_file [output_dir]` unpacks the icons to .icns files.
* For VRFS files: `vrfs_dump VRFS_file [output_dir]`

### decode_data

decode_data can decode and decompress a few custom encoding formats used by various games. Specifically:

    Game/App/Library       | Encoding | CLI option        | Notes
    -------------------------------------------------------------
    DinoPark Tycoon        | LZSS     | --dinopark        | %0
    DinoPark Tycoon        | RLE      | --dinopark        |
    Flashback              | LZSS     | --presage         | %0
    MacSki                 | COOK     | --macski          |
    MacSki                 | CO2K     | --macski          |
    MacSki                 | RUN4     | --macski          |
    PackBits (compress)    | PackBits | --pack-bits       |
    PackBits (decompress)  | PackBits | --unpack-bits     |
    Pathways Into Darkness | Pathways | --unpack-pathways |
    SoundMusicSys          | LZSS     | --sms             | %0

    Notes:
    %0: Although these are all variants of LZSS (and are indeed very similar to
        each other), they are mutually incompatible formats.

decode_data can be used on its own to decompress data, or can be used as an external preprocessor via resource_dasm to transparently decompress some formats. For example, to use decode_data for MacSki resources, you can run a command like `resource_dasm --external-preprocessor="./decode_data --macski" input_filename ...`

### render_sprite

render_sprite can render several custom game sprite formats. For some formats listed below, you'll have to provide a color table resource in addition to the sprite resource. A .bin file produced by resource_dasm from a clut, pltt, or CTBL resource will suffice; usually these can be found in the same file as the sprite resources or in the game application. Run render_sprite with no arguments for usage information.

Supported formats:

    Game                         | Type | CLI option | Need color table | Notes
    ---------------------------------------------------------------------------
    Beyond Dark Castle           | PBLK | --PBLK     | No               |
    Beyond Dark Castle           | PPCT | --PPCT     | No               |
    Beyond Dark Castle           | PSCR | --PSCR-v2  | No               |
    Blobbo                       | BTMP | --BTMP     | No               |
    Blobbo                       | PMP8 | --PMP8     | Yes              | $9
    BodyScope                    | Imag | --Imag     | Yes              | $2 $3
    Bonkheads                    | Sprt | --Sprt     | Yes              |
    Bubble Trouble               | btSP | --btSP     | Yes              |
    Dark Castle (color)          | DC2  | --DC2      | No               | $4
    Dark Castle (monochrome)     | PPCT | --PPCT     | No               |
    Dark Castle (monochrome)     | PSCR | --PSCR-v1  | No               |
    DinoPark Tycoon              | BMap | --BMap     | No               |
    DinoPark Tycoon              | XBig | --XBig     | No               | $2
    DinoPark Tycoon              | XMap | --XMap     | Yes              | $2 $7
    Dr. Quandary                 | Imag | --Imag     | Sometimes        | $1 $2 $3
    Factory                      | 1img | --1img     | No               |
    Factory                      | 4img | --4img     | Yes              |
    Factory                      | 8img | --8img     | Yes              |
    Flashback                    | PPSS | --PPSS     | Yes              | $2 $8
    Fraction Munchers            | Imag | --Imag-fm  | Sometimes        | $1 $2 $3
    Greebles                     | GSIF | --GSIF     | Yes              |
    Harry the Handsome Executive | HrSp | --HrSp     | Yes              | $9
    Lemmings                     | SHPD | --SHPD-v1  | Sometimes        | $0 $1 $2 $5
    Marathon                     | .256 | --.256-m   | No               | $2
    Mario Teaches Typing         | Pak  | --Pak      | Sometimes        | $1 $2
    Mars Rising                  | btSP | --btSP     | Yes              |
    Number Munchers              | Imag | --Imag-fm  | Sometimes        | $1 $2 $3
    Odell Down Under             | Imag | --Imag     | Sometimes        | $1 $2 $3
    Oh No! More Lemmings         | SHPD | --SHPD-v2  | Sometimes        | $0 $1 $2 $5
    Pathways Into Darkness       | .256 | --.256-pd  | No               | $2
    Prince of Persia             | SHPD | --SHPD-p   | Sometimes        | $0 $1 $2 $5
    Prince of Persia 2           | SHAP | --SHAP     | Yes              |
    SimCity 2000                 | SPRT | --SPRT     | Yes              | $2
    SimTower                     |      |            | No               | $A
    Slithereens                  | SprD | --SprD     | Yes              | $2
    SnapDragon                   | Imag | --Imag     | Sometimes        | $1 $2 $3
    Spectre                      | shap | --shap     | No               | $6
    Step On It!                  | sssf | --sssf     | Yes              | $2
    Super Munchers               | Imag | --Imag-fm  | Sometimes        | $1 $2 $3
    Swamp Gas                    | PPic | --PPic     | Sometimes        | $0 $2 $3
    The Amazon Trail             | Imag | --Imag     | Sometimes        | $2 $3
    The Oregon Trail             | Imag | --Imag     | Sometimes        | $1 $2 $3
    TheZone                      | Spri | --Spri     | Yes              |
    Word Munchers                | Imag | --Imag-fm  | Sometimes        | $1 $2 $3

    Notes:
    $0: render_sprite can't tell from the contents of the resource whether it is
        color or monochrome, so it assumes the resource is color if you give a
        color table on the command line. If decoding fails with a color table,
        try decoding without one (or vice versa).
    $1: These games contain some color and some monochrome graphics. It should
        be obvious which are which (usually color graphics are in a separate
        file), but if not, you can give a clut anyway in these cases and
        render_sprite will ignore it if the image is monochrome.
    $2: These sprite formats contain multiple images, so render_sprite will
        produce multiple image files.
    $3: Resources of this type can contain embedded color tables; if you're
        rendering a color image that doesn't have a color table, you'll have to
        provide one via a command-line option. If the resource (or individual
        images therein) contain their own color tables or are monochrome, no
        color table is required on the command line, and any provided color
        table via the command line will be ignored.
    $4: You can get DC2 sprites from the DC Data file with
        `resource_dasm --index-format=dc-data "DC Data"`.
    $5: The graphics files contain resources that refer to segments of the data
        fork in the same file. So, this option expects the original Graphics or
        BW Graphics or Persia file (with both data and resource forks present),
        not an already-extracted resource.
    $6: shap resources contain 3D models and 2D top-down projections of them.
        When given a shap resource, render_sprite produces an STL file and an
        OBJ file for the 3D model, and an SVG file for the 2D top-down view.
    $7: Some XMap resources are stored inside CBag archives. You can extract
        them with `resource_dasm --index-format=cbag <CBAG_file.bin>`.
    $8: This game has only one clut and it's huge - far longer than the usual
        256 entries. It seems PPSS image sets are meant to be rendered with a
        subset of this clut, but I haven't been able to figure out (yet) how the
        game chooses what subset of it to use.
    $9: The game doesn't contain any color tables. You can use a 256-color clut
        resource from the Mac OS System file, or use the --default-clut option.
    $A: The game stores its sprites in normal PICT resources with an incorrect
        type. Use `resource_dasm --copy-handler=PICT:%89%E6%91%9C` to decode
        them instead of using render_sprite.

### icon_dearchiver

icon_dearchiver unpacks the icons in an Icon Archiver (by Alessandro Levi Montalcini) archive to .icns. Run it with no options for usage information.

### Game map generators

* For Blobbo maps: use resource_dasm to get the PMP8 and Blev resources from Blobbo, then use render_sprite to convert PMP8 128 into a .bmp file, then run `blobbo_render <Blev-file.bin> <PMP8-128.bmp>`
* For Ferazel's Wand maps: `ferazel_render` in the directory with the data files, or `ferazel_render --help` to see all the options (there are many!)
* For Gamma Zee maps: `gamma_zee_render gamma_zee_application levels_filename`
* For Harry the Handsome Executive maps: `harry_render --clut-file=clut.bin`, or just `harry_render` to see all the options (there are many!)
* For Infotron maps: `infotron_render` in the Info Datafiles directory
* For Lemmings (Mac version) maps: `lemmings_render --clut-file=clut.bin`, or `lemmings_render --help` to see all the options
* For Monkey Shines maps: `mshines_render world_file [output_directory]`
* For Oh No! More Lemmings (Mac version) maps: Use `lemmings_render` as for original Lemmings, but also use the `--v2` option
* For Realmz maps and scripts: `realmz_dasm global_data_dir [scenario_dir] out_dir` (if scenario_dir is not given, disassembles the shared data instead)
