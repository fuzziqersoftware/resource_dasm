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

include_directories("/usr/local/include")
link_directories("/usr/local/lib")



# Library and executable definitions

add_library(resource_file
  src/AudioCodecs.cc
  src/Decompressors/System01.cc
  src/Decompressors/System2.cc
  src/Decompressors/System3.cc
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
  src/IndexFormats/DCData.cc
  src/IndexFormats/HIRF.cc
  src/IndexFormats/Mohawk.cc
  src/IndexFormats/ResourceFork.cc
  src/Lookups.cc
  src/LowMemoryGlobals.cc
  src/QuickDrawEngine.cc
  src/QuickDrawFormats.cc
  src/ResourceCompression.cc
  src/ResourceFile.cc
  src/SystemDecompressors.cc
  src/SystemTemplates.cc
  src/TextCodecs.cc
  src/TrapInfo.cc
)
target_link_libraries(resource_file phosg)

add_executable(render_sprite
  src/SpriteDecoders/Ambrosia-btSP-HrSp.cc
  src/SpriteDecoders/DarkCastle-DC2.cc
  src/SpriteDecoders/DarkCastle-PPCT-PSCR.cc
  src/SpriteDecoders/Factory-1img-4img-8img.cc
  src/SpriteDecoders/Greebles-GSIF.cc
  src/SpriteDecoders/Lemmings-PrinceOfPersia-SHPD.cc
  src/SpriteDecoders/PrinceOfPersia2-SHAP.cc
  src/SpriteDecoders/SimCity2000-SPRT.cc
  src/SpriteDecoders/Spectre-shap.cc
  src/SpriteDecoders/StepOnIt-sssf.cc
  src/SpriteDecoders/SwampGas-PPic.cc
  src/SpriteDecoders/TheZone-Spri.cc
  src/render_sprite.cc
)
target_link_libraries(render_sprite resource_file phosg)

foreach(ExecutableName IN ITEMS resource_dasm m68kexec ferazel_render hypercard_dasm infotron_render gamma_zee_render mshines_render render_bits)
  add_executable(${ExecutableName} src/${ExecutableName}.cc)
  target_link_libraries(${ExecutableName} resource_file phosg)
endforeach()

add_executable(m68kdasm src/m68kdasm.cc)
target_link_libraries(m68kdasm resource_file phosg pthread)

add_executable(lemmings_render src/lemmings_render.cc src/SpriteDecoders/Lemmings-PrinceOfPersia-SHPD.cc)
target_link_libraries(lemmings_render resource_file phosg)

add_executable(realmz_dasm src/realmz_dasm.cc src/RealmzGlobalData.cc src/RealmzScenarioData.cc)
target_link_libraries(realmz_dasm resource_file phosg)

add_executable(harry_render src/harry_render.cc src/SpriteDecoders/Ambrosia-btSP-HrSp.cc)
target_link_libraries(harry_render resource_file phosg)

add_executable(flashback_decomp src/flashback_decomp.cc)
target_link_libraries(flashback_decomp phosg)

add_executable(macski_decomp src/macski_decomp.cc)
target_link_libraries(macski_decomp phosg)

add_executable(dupe_finder
  src/dupe_finder.cc
)
target_link_libraries(dupe_finder resource_file phosg)



# Installation configuration

install(TARGETS resource_dasm DESTINATION bin)
install(TARGETS m68kdasm DESTINATION bin)
install(TARGETS m68kexec DESTINATION bin)

install(TARGETS resource_file DESTINATION lib)

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
