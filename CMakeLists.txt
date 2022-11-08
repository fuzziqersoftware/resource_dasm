cmake_minimum_required(VERSION 3.10)



# Project setup

project(resource_dasm)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
if (MSVC)
  add_compile_options(/W4 /WX)
else()
  add_compile_options(-Wall -Wextra -Werror -Wno-strict-aliasing)
endif()

set(LOCAL_INCLUDE_DIR "/usr/local/include")
set(LOCAL_LIB_DIR "/usr/local/lib")
list(APPEND CMAKE_PREFIX_PATH ${LOCAL_LIB_DIR})
include_directories(${LOCAL_INCLUDE_DIR})
link_directories(${LOCAL_LIB_DIR})

# phosg library in custom location?
if (PHOSG_DIR)
  # (requires symlink "phosg" -> "src" in phosg directory)
  include_directories(${PHOSG_DIR})
  link_directories(${PHOSG_DIR})
else()
  find_package(phosg REQUIRED)
endif()



# Library and executable definitions

add_library(resource_file
  src/AudioCodecs.cc
  src/Cli.cc
  src/DataCodecs/Bungie.cc
  src/DataCodecs/DinoParkTycoon-LZSS-RLE.cc
  src/DataCodecs/MacSki-RUN4-COOK-CO2K.cc
  src/DataCodecs/PackBits.cc
  src/DataCodecs/Presage-LZSS.cc
  src/DataCodecs/SoundMusicSys-LZSS.cc
  src/Emulators/EmulatorBase.cc
  src/Emulators/InterruptManager.cc
  src/Emulators/M68KEmulator.cc
  src/Emulators/MemoryContext.cc
  src/Emulators/PPC32Emulator.cc
  src/Emulators/X86Emulator.cc
  src/ExecutableFormats/DOLFile.cc
  src/ExecutableFormats/ELFFile.cc
  src/ExecutableFormats/PEFFile.cc
  src/ExecutableFormats/PEFile.cc
  src/ExecutableFormats/RELFile.cc
  src/IndexFormats/CBag.cc
  src/IndexFormats/DCData.cc
  src/IndexFormats/HIRF.cc
  src/IndexFormats/MacBinary.cc
  src/IndexFormats/Mohawk.cc
  src/IndexFormats/ResourceFork.cc
  src/Lookups.cc
  src/LowMemoryGlobals.cc
  src/QuickDrawEngine.cc
  src/QuickDrawFormats.cc
  src/ResourceCompression.cc
  src/ResourceDecompressors/System01.cc
  src/ResourceDecompressors/System2.cc
  src/ResourceDecompressors/System3.cc
  src/ResourceFile.cc
  src/ResourceIDs.cc
  src/SpriteDecoders/Ambrosia-btSP-HrSp-SprD.cc
  src/SpriteDecoders/Blobbo-BTMP-PMP8.cc
  src/SpriteDecoders/Bungie-256.cc
  src/SpriteDecoders/DarkCastle-DC2.cc
  src/SpriteDecoders/DarkCastle-PPCT-PSCR.cc
  src/SpriteDecoders/DinoParkTycoon-BMap-XMap-XBig.cc
  src/SpriteDecoders/Factory-1img-4img-8img.cc
  src/SpriteDecoders/Greebles-GSIF.cc
  src/SpriteDecoders/Lemmings-PrinceOfPersia-SHPD.cc
  src/SpriteDecoders/MECC-Imag.cc
  src/SpriteDecoders/Presage.cc
  src/SpriteDecoders/PrinceOfPersia2-SHAP.cc
  src/SpriteDecoders/SimCity2000-SPRT.cc
  src/SpriteDecoders/Spectre-shap.cc
  src/SpriteDecoders/StepOnIt-sssf.cc
  src/SpriteDecoders/SwampGas-PPic.cc
  src/SpriteDecoders/TheZone-Spri.cc
  src/SystemDecompressors.cc
  src/SystemTemplates.cc
  src/TextCodecs.cc
  src/TrapInfo.cc
)
target_link_libraries(resource_file phosg)

add_executable(vrfs_dump src/vrfs_dump.cc)
target_link_libraries(vrfs_dump phosg)

foreach(ExecutableName IN ITEMS resource_dasm m68kdasm blobbo_render bugs_bannis_render decode_data dupe_finder ferazel_render gamma_zee_render harry_render hypercard_dasm infotron_render lemmings_render m68kexec mshines_render render_bits render_sprite replace_clut)
  add_executable(${ExecutableName} src/${ExecutableName}.cc)
  target_link_libraries(${ExecutableName} resource_file)
endforeach()

add_executable(realmz_dasm src/realmz_dasm.cc src/RealmzGlobalData.cc src/RealmzScenarioData.cc)
target_link_libraries(realmz_dasm resource_file)

add_executable(icon_dearchiver src/icon_dearchiver.cc)
target_link_libraries(icon_dearchiver resource_file z)



# Installation configuration

install(TARGETS resource_dasm DESTINATION bin)
install(TARGETS m68kdasm DESTINATION bin)
install(TARGETS m68kexec DESTINATION bin)

install(TARGETS resource_file EXPORT resource_file DESTINATION lib)

file(GLOB Headers ${CMAKE_SOURCE_DIR}/src/*.hh)
install(FILES ${Headers} DESTINATION include/resource_file)
file(GLOB DecompressorsHeaders ${CMAKE_SOURCE_DIR}/src/Decompressors/*.hh)
install(FILES ${DecompressorsHeaders} DESTINATION include/resource_file/Decompressors)
file(GLOB IndexFormatsHeaders ${CMAKE_SOURCE_DIR}/src/IndexFormats/*.hh)
install(FILES ${IndexFormatsHeaders} DESTINATION include/resource_file/IndexFormats)
file(GLOB ExecutableFormatsHeaders ${CMAKE_SOURCE_DIR}/src/ExecutableFormats/*.hh)
install(FILES ${ExecutableFormatsHeaders} DESTINATION include/resource_file/ExecutableFormats)
file(GLOB EmulatorsHeaders ${CMAKE_SOURCE_DIR}/src/Emulators/*.hh)
install(FILES ${EmulatorsHeaders} DESTINATION include/resource_file/Emulators)

install(FILES resource_file-config.cmake DESTINATION lib)
install(EXPORT resource_file DESTINATION lib)
