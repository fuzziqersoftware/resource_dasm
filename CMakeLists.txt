cmake_minimum_required(VERSION 3.22)



# Project setup

project(resource_dasm)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
if (MSVC)
  add_compile_options(/W4 /WX)
else()
  add_compile_options(-Wall -Wextra -Werror -Wno-strict-aliasing -O2)
endif()

find_package(phosg REQUIRED PATHS ${CMAKE_INSTALL_FULL_LIBDIR})
find_package(SDL3)



# Library and executable definitions

add_library(resource_file
  src/AudioCodecs.cc
  src/Audio/AAFArchive.cc
  src/Audio/Constants.cc
  src/Audio/Instrument.cc
  src/Audio/MODSynthesizer.cc
  src/Audio/WAVFile.cc
  src/BitmapFontRenderer.cc
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
  src/Emulators/SH4Emulator.cc
  src/Emulators/X86Emulator.cc
  src/ExecutableFormats/DOLFile.cc
  src/ExecutableFormats/ELFFile.cc
  src/ExecutableFormats/PEFFile.cc
  src/ExecutableFormats/PEFile.cc
  src/ExecutableFormats/RELFile.cc
  src/ExecutableFormats/XBEFile.cc
  src/ImageSaver.cc
  src/IndexFormats/AppleSingle-AppleDouble.cc
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
target_include_directories(
  resource_file PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(resource_file phosg::phosg z)

add_library(resource_file_sdl src/Audio/SDLAudioStream.cc)
target_link_libraries(resource_file_sdl resource_file ${SDL3_LIBRARIES})

foreach(ExecutableName IN ITEMS gcmasm gcmdump gvmdump rcfdump vrfsdump)
  add_executable(${ExecutableName} src/${ExecutableName}.cc)
  target_link_libraries(${ExecutableName} phosg::phosg)
endforeach()
foreach(ExecutableName IN ITEMS smsdumpbanks smssynth modsynth)
  add_executable(${ExecutableName} src/Audio/${ExecutableName}.cc)
  target_link_libraries(${ExecutableName} phosg::phosg resource_file)
endforeach()

if(SDL3_FOUND)
message("SDL3 is available; enabling audio playback support in smssynth and modsynth")
  foreach(ExecutableName IN ITEMS smssynth modsynth)
    target_compile_definitions(${ExecutableName} PRIVATE SDL3_AVAILABLE)
    target_include_directories(${ExecutableName} PUBLIC ${SDL3_INCLUDE_DIRS})
    target_link_libraries(${ExecutableName} resource_file_sdl)
  endforeach()
else()
  message("SDL3 is not available; disabling audio playback support in smssynth and modsynth")
endif()

foreach(ExecutableName IN ITEMS resource_dasm m68kdasm blobbo_render bugs_bannis_render decode_data dupe_finder ferazel_render gamma_zee_render harry_render hypercard_dasm infotron_render lemmings_render m68kexec mshines_render render_bits render_sprite render_text replace_clut assemble_images icon_dearchiver)
  add_executable(${ExecutableName} src/${ExecutableName}.cc)
  target_link_libraries(${ExecutableName} resource_file)
endforeach()

add_executable(realmz_dasm src/realmz_dasm.cc src/RealmzGlobalData.cc src/RealmzScenarioData.cc)
target_link_libraries(realmz_dasm resource_file)



# Installation configuration

# Library package setup
install(
  TARGETS resource_file resource_file_sdl
  EXPORT resource_file
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Header files
file(GLOB Headers ${CMAKE_SOURCE_DIR}/src/*.hh)
install(FILES ${Headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/resource_file)
file(GLOB EmulatorsHeaders ${CMAKE_SOURCE_DIR}/src/Audio/*.hh)
install(FILES ${EmulatorsHeaders} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/resource_file/Audio)
file(GLOB DecompressorsHeaders ${CMAKE_SOURCE_DIR}/src/Decompressors/*.hh)
install(FILES ${DecompressorsHeaders} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/resource_file/Decompressors)
file(GLOB IndexFormatsHeaders ${CMAKE_SOURCE_DIR}/src/IndexFormats/*.hh)
install(FILES ${IndexFormatsHeaders} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/resource_file/IndexFormats)
file(GLOB ExecutableFormatsHeaders ${CMAKE_SOURCE_DIR}/src/ExecutableFormats/*.hh)
install(FILES ${ExecutableFormatsHeaders} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/resource_file/ExecutableFormats)
file(GLOB EmulatorsHeaders ${CMAKE_SOURCE_DIR}/src/Emulators/*.hh)
install(FILES ${EmulatorsHeaders} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/resource_file/Emulators)

# Export definition
install(
  EXPORT resource_file
  FILE resource_file.cmake
  NAMESPACE resource_file::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/resource_file
)

# CMake config files
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/resource_fileConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/resource_fileConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/resource_file
)
set(version 0.1)
set_property(TARGET resource_file PROPERTY VERSION ${version})
set_property(TARGET resource_file PROPERTY SOVERSION 0)
set_property(TARGET resource_file PROPERTY INTERFACE_resource_file_MAJOR_VERSION 0)
set_property(TARGET resource_file APPEND PROPERTY COMPATIBLE_INTERFACE_STRING resource_file_MAJOR_VERSION)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/resource_fileConfigVersion.cmake"
  VERSION "${version}"
  COMPATIBILITY AnyNewerVersion
)
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/resource_fileConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/resource_fileConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/resource_file
)

# Executables (separate from package definition)
install(TARGETS resource_dasm DESTINATION bin)
install(TARGETS m68kdasm DESTINATION bin)
install(TARGETS m68kexec DESTINATION bin)
install(TARGETS render_bits DESTINATION bin)
install(TARGETS replace_clut DESTINATION bin)
install(TARGETS assemble_images DESTINATION bin)
