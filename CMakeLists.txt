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

add_library(resource_file src/IndexFormats/ResourceFork.cc src/IndexFormats/Mohawk.cc src/IndexFormats/HIRF.cc src/QuickDrawFormats.cc src/QuickDrawEngine.cc src/ResourceFile.cc src/SystemTemplates.cc src/Decompressors/System.cc src/Decompressors/System01.cc src/Decompressors/System2.cc src/Decompressors/System3.cc src/AudioCodecs.cc src/MemoryContext.cc src/InterruptManager.cc src/M68KEmulator.cc src/PEFFFile.cc src/DOLFile.cc src/PPC32Emulator.cc src/TrapInfo.cc)

foreach(ExecutableName IN ITEMS resource_dasm dc_dasm ferazel_render hypercard_dasm infotron_render gamma_zee_render mshines_render pop2_render sc2k_render step_on_it_render render_bits thezone_render)
  add_executable(${ExecutableName} src/${ExecutableName}.cc)
  target_link_libraries(${ExecutableName} resource_file phosg)
endforeach()

add_executable(realmz_dasm src/realmz_dasm.cc src/RealmzGlobalData.cc src/RealmzScenarioData.cc)
target_link_libraries(realmz_dasm resource_file phosg)

foreach(ExecutableName IN ITEMS bt_render harry_render)
  add_executable(${ExecutableName} src/${ExecutableName}.cc src/AmbrosiaSprites.cc)
  target_link_libraries(${ExecutableName} resource_file phosg)
endforeach()

add_executable(macski_decomp src/macski_decomp.cc)
target_link_libraries(macski_decomp phosg)



# Installation configuration

install(TARGETS resource_dasm DESTINATION bin)

install(TARGETS resource_file DESTINATION lib)

file(GLOB Headers ${CMAKE_SOURCE_DIR}/src/*.hh)
install(FILES ${Headers} DESTINATION include/resource_file)
file(GLOB DecompressorsHeaders ${CMAKE_SOURCE_DIR}/src/Decompressors/*.hh)
install(FILES ${DecompressorsHeaders} DESTINATION include/resource_file/Decompressors)
file(GLOB IndexFormatsHeaders ${CMAKE_SOURCE_DIR}/src/IndexFormats/*.hh)
install(FILES ${IndexFormatsHeaders} DESTINATION include/resource_file/IndexFormats)
