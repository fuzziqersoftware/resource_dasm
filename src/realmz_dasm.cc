#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <filesystem>
#include <phosg/Arguments.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <phosg/Tools.hh>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ImageSaver.hh"
#include "IndexFormats/Formats.hh"
#include "RealmzGlobalData.hh"
#include "RealmzSaveData.hh"
#include "RealmzScenarioData.hh"

using namespace std;
using namespace phosg;
using namespace ResourceDASM;

int disassemble_scenario(
    const RealmzScenarioData& scen,
    const string& out_dir,
    const ImageSaver* image_saver,
    bool show_unused_tile_ids,
    bool generate_maps_as_json,
    bool show_random_rects) {

  // Make necessary directories for output
  std::filesystem::create_directories(out_dir);
  if (image_saver) {
    std::filesystem::create_directories(std::format("{}/media", out_dir));
  }

  // Disassemble scenario text
  {
    string filename = std::format("{}/script.txt", out_dir);
    auto f = fopen_unique(filename, "wt");

    fwritex(f.get(), scen.disassemble_global_metadata());
    phosg::log_info_f("... {} (global metadata)", filename);

    fwritex(f.get(), scen.disassemble_scenario_metadata());
    phosg::log_info_f("... {} (scenario metadata)", filename);

    fwritex(f.get(), scen.disassemble_restrictions());
    phosg::log_info_f("... {} (restrictions)", filename);

    fwritex(f.get(), scen.disassemble_solids());
    phosg::log_info_f("... {} (solids)", filename);

    for (auto it : scen.land_type_to_tileset_definition) {
      if (!it.first.starts_with("custom")) {
        continue; // skip default tilesets
      }
      fwritex(f.get(), scen.global.disassemble_tileset_definition(it.second, it.first.c_str()));
      phosg::log_info_f("... {} ({} land tileset)", filename, it.first);
    }

    fwritex(f.get(), scen.disassemble_all_monsters());
    phosg::log_info_f("... {} (monsters)", filename);

    fwritex(f.get(), scen.disassemble_all_battles());
    phosg::log_info_f("... {} (battles)", filename);

    fwritex(f.get(), scen.disassemble_all_custom_item_definitions());
    phosg::log_info_f("... {} (items)", filename);

    fwritex(f.get(), scen.disassemble_all_shops());
    phosg::log_info_f("... {} (shops)", filename);

    fwritex(f.get(), scen.disassemble_all_treasures());
    phosg::log_info_f("... {} (treasures)", filename);

    fwritex(f.get(), scen.disassemble_all_party_maps());
    phosg::log_info_f("... {} (party maps)", filename);

    fwritex(f.get(), scen.disassemble_all_simple_encounters());
    phosg::log_info_f("... {} (simple encounters)", filename);

    fwritex(f.get(), scen.disassemble_all_complex_encounters());
    phosg::log_info_f("... {} (complex encounters)", filename);

    fwritex(f.get(), scen.disassemble_all_rogue_encounters());
    phosg::log_info_f("... {} (rogue encounters)", filename);

    fwritex(f.get(), scen.disassemble_all_time_encounters());
    phosg::log_info_f("... {} (time encounters)", filename);

    fwritex(f.get(), scen.disassemble_all_level_aps_and_rrs(true));
    phosg::log_info_f("... {} (dungeon APs and RRs)", filename);

    fwritex(f.get(), scen.disassemble_all_level_aps_and_rrs(false));
    phosg::log_info_f("... {} (land APs and RRs)", filename);

    fwritex(f.get(), scen.disassemble_all_xaps());
    phosg::log_info_f("... {} (extra APs)", filename);
  }

  if (!image_saver) {
    return 0;
  }

  // Save media
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_PICT)) {
    auto decoded = scen.scenario_rsf.decode_PICT(id);
    string filename = std::format("{}/media/picture_{}", out_dir, id);
    filename = image_saver->save_image(decoded.image, filename);
    phosg::log_info_f("... {}", filename);
  }
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
    auto decoded = scen.scenario_rsf.decode_cicn(id);
    string filename = std::format("{}/media/icon_{}", out_dir, id);
    filename = image_saver->save_image(decoded.image, filename);
    phosg::log_info_f("... {}", filename);
  }
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_snd)) {
    auto decoded = scen.scenario_rsf.decode_snd(id);
    string filename = std::format("{}/media/snd_{}.wav", out_dir, id);
    save_file(filename, decoded.data);
    phosg::log_info_f("... {}", filename);
  }
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_TEXT)) {
    try {
      if (!scen.scenario_rsf.resource_exists(RESOURCE_TYPE_styl, id)) {
        throw runtime_error("TEXT resource has no corresponding styl resource");
      }
      string filename = std::format("{}/media/text_{}.rtf", out_dir, id);
      save_file(filename, scen.scenario_rsf.decode_styl(id));
      phosg::log_info_f("... {}", filename);
    } catch (const runtime_error& e) {
      string filename = std::format("{}/media/text_{}.txt", out_dir, id);
      save_file(filename, scen.scenario_rsf.decode_TEXT(id));
      phosg::log_info_f("*** {} (style rendering failed: {})", filename, e.what());
    }
  }

  // Generate custom tileset legends
  for (auto it : scen.land_type_to_tileset_definition) {
    if (!it.first.starts_with("custom")) {
      continue; // skip default tilesets
    }
    string filename = std::format("{}/tileset_{}_legend", out_dir, it.first);
    int16_t resource_id = scen.global.pict_resource_id_for_land_type(it.first);
    if (!scen.scenario_rsf.resource_exists(RESOURCE_TYPE_PICT, resource_id)) {
      phosg::log_info_f("### {} FAILED: PICT {} is missing", filename, resource_id);
    } else {
      ImageRGBA8888N positive_pattern = scen.scenario_rsf.decode_PICT(resource_id).image;
      ImageRGB888 legend = scen.global.generate_tileset_definition_legend(it.second, positive_pattern);
      filename = image_saver->save_image(legend, filename);
      phosg::log_info_f("... {}", filename);
    }
  }

  // Generate dungeon maps
  for (size_t z = 0; z < scen.dungeon_maps.size(); z++) {
    string filename = std::format("{}/dungeon_{}", out_dir, z);
    if (generate_maps_as_json) {
      filename += ".json";
      string s = scen.generate_dungeon_map_json(z);
      save_file(filename, s);
      phosg::log_info_f("... {}", filename);
    } else {
      ImageRGB888 map = scen.generate_dungeon_map(z, 0, 0, 90, 90, show_random_rects);
      filename = image_saver->save_image(map, filename);
      phosg::log_info_f("... {}", filename);
    }
  }

  // Generate land maps
  unordered_set<int16_t> used_negative_tiles;
  unordered_map<string, unordered_set<uint8_t>> used_positive_tiles;
  for (size_t z = 0; z < scen.land_maps.size(); z++) {
    string filename = std::format("{}/land_{}", out_dir, z);
    try {
      if (generate_maps_as_json) {
        filename += ".json";
        string s = scen.generate_land_map_json(z);
        save_file(filename, s);
        phosg::log_info_f("... {}", filename);
      } else {
        ImageRGB888 map = scen.generate_land_map(
            z, 0, 0, 90, 90, show_random_rects, -1, -1, nullptr, nullptr, nullptr, nullptr, &used_negative_tiles, &used_positive_tiles);
        filename = image_saver->save_image(map, filename);
        phosg::log_info_f("... {}", filename);
      }
    } catch (const exception& e) {
      phosg::log_info_f("### {} FAILED: {}", filename, e.what());
    }
  }

  // Generate party maps
  for (size_t z = 0; z < scen.party_maps.size(); z++) {
    string filename = std::format("{}/map_{}", out_dir, z);
    try {
      ImageRGB888 map = scen.render_party_map(z);
      filename = image_saver->save_image(map, filename);
      phosg::log_info_f("... {}", filename);
    } catch (const exception& e) {
      phosg::log_info_f("### {} FAILED: {}", filename, e.what());
    }
  }

  // Generate connected land map
  for (auto layout_component : scen.layout.get_connected_components()) {
    if (layout_component.num_valid_levels() < 2) {
      continue;
    }
    string filename = std::format("{}/land_connected", out_dir);
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 16; x++) {
        if (layout_component.layout[y][x] != -1) {
          filename += std::format("_{}", layout_component.layout[y][x]);
        }
      }
    }

    ImageRGB888 connected_map = scen.generate_layout_map(layout_component, show_random_rects);
    filename = image_saver->save_image(connected_map, filename);
    phosg::log_info_f("... {}", filename);
  }

  // Find unused land tiles
  if (show_unused_tile_ids) {
    for (const auto& it : used_positive_tiles) {
      for (uint8_t z = 0; z < 200; z++) {
        if (!it.second.count(z)) {
          phosg::log_info_f(">>> unused positive tile: {}-{} (x={}, y={} in positive pattern)",
              it.first, z, static_cast<uint8_t>(z % 20),
              static_cast<uint8_t>(z / 20));
        }
      }
    }
    for (int16_t z : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
      if (!used_negative_tiles.count(z)) {
        phosg::log_info_f(">>> unused negative tile: {}", z);
      }
    }
  }

  return 0;
}

int disassemble_saved_game(const RealmzSaveData& save, const string& out_dir, const ImageSaver* image_saver) {
  // Make necessary directories for output
  std::filesystem::create_directories(out_dir);

  // Disassemble scenario text
  {
    string filename = std::format("{}/script.txt", out_dir);
    auto f = fopen_unique(filename, "wt");

    fwritex(f.get(), save.disassemble_game_state());
    phosg::log_info_f("... {} (game state)", filename);

    fwritex(f.get(), save.disassemble_all_shops());
    phosg::log_info_f("... {} (shops)", filename);

    fwritex(f.get(), save.disassemble_all_simple_encounters());
    phosg::log_info_f("... {} (simple encounters)", filename);

    fwritex(f.get(), save.disassemble_all_complex_encounters());
    phosg::log_info_f("... {} (complex encounters)", filename);

    fwritex(f.get(), save.disassemble_all_rogue_encounters());
    phosg::log_info_f("... {} (rogue encounters)", filename);

    fwritex(f.get(), save.disassemble_all_time_encounters());
    phosg::log_info_f("... {} (time encounters)", filename);

    fwritex(f.get(), save.disassemble_all_land_level_states());
    phosg::log_info_f("... {} (dungeon APs and RRs)", filename);

    fwritex(f.get(), save.disassemble_all_dungeon_level_states());
    phosg::log_info_f("... {} (land APs and RRs)", filename);
  }

  // Generate dungeon maps
  for (size_t z = 0; z < save.dungeon_level_states.size(); z++) {
    string filename = std::format("{}/dungeon_{}", out_dir, z);
    try {
      ImageRGB888 map = save.generate_dungeon_map(z, 0, 0, 90, 90);
      filename = image_saver->save_image(map, filename);
      phosg::log_info_f("... {}", filename);
    } catch (const exception& e) {
      phosg::log_info_f("### {} FAILED: {}", filename, e.what());
    }
  }

  // Generate land maps
  for (size_t z = 0; z < save.land_level_states.size(); z++) {
    string filename = std::format("{}/land_{}", out_dir, z);
    try {
      ImageRGB888 map = save.generate_land_map(z, 0, 0, 90, 90);
      filename = image_saver->save_image(map, filename);
      phosg::log_info_f("... {}", filename);
    } catch (const exception& e) {
      phosg::log_info_f("### {} FAILED: {}", filename, e.what());
    }
  }

  // Generate connected land map
  for (auto layout_component : save.scenario.layout.get_connected_components()) {
    if (layout_component.num_valid_levels() < 2) {
      continue;
    }
    string filename = std::format("{}/land_connected", out_dir);
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 16; x++) {
        if (layout_component.layout[y][x] != -1) {
          filename += std::format("_{}", layout_component.layout[y][x]);
        }
      }
    }

    ImageRGB888 connected_map = save.generate_layout_map(layout_component);
    filename = image_saver->save_image(connected_map, filename);
    phosg::log_info_f("... {}", filename);
  }

  return 0;
}

int disassemble_global_data(RealmzGlobalData& global, const string& out_dir, const ImageSaver* image_saver) {
  // Make necessary directories for output
  std::filesystem::create_directories(out_dir);
  if (image_saver) {
    std::filesystem::create_directories(std::format("{}/media", out_dir));
  }

  // Disassemble non-media data
  {
    string filename;

    for (auto it : global.land_type_to_tileset_definition) {
      filename = std::format("{}/tileset_{}.txt", out_dir, it.first);
      save_file(filename, global.disassemble_tileset_definition(it.second, it.first.c_str()));
      phosg::log_info_f("... {}", filename);
    }

    filename = std::format("{}/castes.txt", out_dir);
    save_file(filename, global.disassemble_all_caste_definitions());
    phosg::log_info_f("... {}", filename);

    filename = std::format("{}/races.txt", out_dir);
    save_file(filename, global.disassemble_all_race_definitions());
    phosg::log_info_f("... {}", filename);

    filename = std::format("{}/items.txt", out_dir);
    save_file(filename, global.disassemble_all_item_definitions());
    phosg::log_info_f("... {}", filename);

    filename = std::format("{}/spells.txt", out_dir);
    save_file(filename, global.disassemble_all_spell_definitions());
    phosg::log_info_f("... {}", filename);
  }

  if (!image_saver) {
    return 0;
  }

  // Save media
  // TODO: factor this out somehow with scenario media exporting code
  for (int16_t id : global.global_rsf.all_resources_of_type(RESOURCE_TYPE_PICT)) {
    auto decoded = global.global_rsf.decode_PICT(id);
    string filename = std::format("{}/media/picture_{}", out_dir, id);
    filename = image_saver->save_image(decoded.image, filename);
    phosg::log_info_f("... {}", filename);
  }
  for (int16_t id : global.global_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
    auto decoded = global.global_rsf.decode_cicn(id);
    string filename = std::format("{}/media/icon_{}", out_dir, id);
    filename = image_saver->save_image(decoded.image, filename);
    phosg::log_info_f("... {}", filename);
  }
  for (int16_t id : global.portraits_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
    auto decoded = global.portraits_rsf.decode_cicn(id);
    string filename = std::format("{}/media/portrait_{}", out_dir, id);
    filename = image_saver->save_image(decoded.image, filename);
    phosg::log_info_f("... {}", filename);
  }
  for (int16_t id : global.tacticals_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
    auto decoded = global.tacticals_rsf.decode_cicn(id);
    string filename = std::format("{}/media/battle_icon_{}", out_dir, id);
    filename = image_saver->save_image(decoded.image, filename);
    phosg::log_info_f("... {}", filename);
  }
  for (int16_t id : global.global_rsf.all_resources_of_type(RESOURCE_TYPE_snd)) {
    string filename = std::format("{}/media/snd_{}.wav", out_dir, id);
    save_file(filename, global.global_rsf.decode_snd(id).data);
    phosg::log_info_f("... {}", filename);
  }
  for (int16_t id : global.global_rsf.all_resources_of_type(RESOURCE_TYPE_TEXT)) {
    try {
      if (!global.global_rsf.resource_exists(RESOURCE_TYPE_styl, id)) {
        throw runtime_error("TEXT resource has no corresponding styl resource");
      }
      string filename = std::format("{}/media/text_{}.rtf", out_dir, id);
      save_file(filename, global.global_rsf.decode_styl(id));
      phosg::log_info_f("... {}", filename);
    } catch (const runtime_error& e) {
      string filename = std::format("{}/media/text_{}.txt", out_dir, id);
      save_file(filename, global.global_rsf.decode_TEXT(id));
      phosg::log_info_f("*** {} (style rendering failed: {})", filename, e.what());
    }
  }

  // Generate tileset legends
  for (auto it : global.land_type_to_tileset_definition) {
    string filename = std::format("{}/tileset_{}_legend",
        out_dir, it.first);
    int16_t resource_id = global.pict_resource_id_for_land_type(it.first);
    ImageRGBA8888N positive_pattern = global.global_rsf.decode_PICT(resource_id).image;
    ImageRGB888 legend = global.generate_tileset_definition_legend(it.second, positive_pattern);
    filename = image_saver->save_image(legend, filename);
    phosg::log_info_f("... {}", filename);
  }

  return 0;
}

static void print_usage() {
  fwrite_fmt(stderr, "\
To disassemble shared media:\n\
  realmz_dasm [OPTIONS] DATA-DIR OUT-DIR\n\
To disassemble a scenario:\n\
  realmz_dasm [OPTIONS] DATA-DIR SCENARIO-DIR OUT-DIR\n\
To disassemble a saved game:\n\
  realmz_dasm [OPTIONS] DATA-DIR SCENARIO-DIR SAVE-DIR OUT-DIR\n\
To disassemble all scenarios:\n\
  realmz_dasm [OPTIONS] --parallel=N DATA-DIR OUT-DIR\n\
\n\
Arguments:\n\
  DATA-DIR: The path to the Data Files directory, or the path to the base\n\
      Realmz directory if --parallelism is given.\n\
  SCENARIO-DIR: The path to the scenario directory to disassemble.\n\
  SAVE-DIR: The path to the saved game to disassemble.\n\
  OUT-DIR: Where to write the output files.\n\
\n\
Options:\n\
  --show-unused-tiles: Output a list of tile IDs that don\'t appear in any map.\n\
      Note that this does not necessarily mean those tiles are unused; they\n\
      could be used by AP scripts when tiles are updated.\n\
  --maps-as-json: Output tile maps as JSON lists of tile IDs.\n\
  --script-only: Don\'t generate any image files; just generate script.txt.\n\
  --parallel=N: Set parallelism for disassembling multiple scenarios. If N=0,\n\
      use as many threads as there are CPU cores in the system. If this option\n\
      is given, DATA-DIR should point to the base Realmz directory (with Data\n\
      Files and Scenarios subdirectories) instead of the Data Files directory.\n\
\n" IMAGE_SAVER_HELP);
}

int main(int argc, char** argv) {
  string data_dir;
  string scenario_dir;
  string save_dir;
  string out_dir;
  ImageSaver image_saver;
  bool show_unused_tile_ids = false;
  bool generate_maps_as_json = false;
  bool script_only = false;
  bool show_random_rects = true;
  ssize_t parallelism = -1;
  for (int x = 1; x < argc; x++) {
    if (image_saver.process_cli_arg(argv[x])) {
      // Nothing
    } else if (!strcmp(argv[x], "--show-unused-tiles")) {
      show_unused_tile_ids = true;
    } else if (!strcmp(argv[x], "--maps-as-json")) {
      generate_maps_as_json = true;
    } else if (!strcmp(argv[x], "--script-only")) {
      script_only = true;
    } else if (!strcmp(argv[x], "--hide-random-rects")) {
      show_random_rects = false;
    } else if (!strncmp(argv[x], "--parallel=", 11)) {
      parallelism = stoll(&argv[x][11], nullptr, 0);
    } else if (data_dir.empty()) {
      data_dir = argv[x];
    } else if (scenario_dir.empty()) {
      scenario_dir = argv[x];
    } else if (save_dir.empty()) {
      save_dir = argv[x];
    } else if (out_dir.empty()) {
      out_dir = argv[x];
    } else {
      fwrite_fmt(stderr, "excess argument: {}\n", argv[x]);
      print_usage();
      return 2;
    }
  }

  if (data_dir.empty() || scenario_dir.empty()) {
    print_usage();
    return 2;
  }

  if (parallelism >= 0) {
    // Use scenario_dir as out_dir; out_dir must be empty
    if (!out_dir.empty() || !save_dir.empty()) {
      throw std::runtime_error("Neither SCENARIO-DIR nor SAVE-DIR may be specified if --parallel is specified");
    }
    out_dir = std::move(scenario_dir);
    if (parallelism == 0) {
      parallelism = std::thread::hardware_concurrency();
      phosg::log_info_f("Setting parallelism to {} to match system CPU count", parallelism);
    }

    std::string global_data_dir = std::format("{}/Data Files", data_dir);
    phosg::log_info_f("Loading shared resources from {}", global_data_dir);
    RealmzGlobalData global(global_data_dir);

    std::string scenarios_dir = std::format("{}/Scenarios", data_dir);
    phosg::log_info_f("Collecting scenarios from {}", scenarios_dir);
    std::vector<std::string> scenario_names;
    scenario_names.emplace_back(); // The empty string represents disassembling the global data
    for (const auto& item : std::filesystem::directory_iterator(scenarios_dir)) {
      if (!item.is_directory()) {
        phosg::log_info_f("Skipping {} (not a directory)", item.path().filename().string());
        continue;
      }
      std::string scen_file_path = std::format("{}/{}/{}", scenarios_dir, item.path().filename().string(), item.path().filename().string());
      if (!std::filesystem::is_regular_file(scen_file_path)) {
        phosg::log_info_f("Skipping {} (scenario named file is missing)", item.path().filename().string());
        continue;
      }

      scenario_names.emplace_back(item.path().filename().string());
      phosg::log_info_f("Found scenario {}", item.path().filename().string());
    }

    std::sort(scenario_names.begin(), scenario_names.end());

    auto disassemble_item = [&](const std::string& scen_name, size_t) -> bool {
      if (scen_name.empty()) {
        return disassemble_global_data(global, out_dir + "/Data Files", script_only ? nullptr : &image_saver);
      } else {
        phosg::log_info_f("Loading scenario: {}", scen_name);
        RealmzScenarioData scen(global, std::format("{}/{}", scenarios_dir, scen_name), scen_name);
        phosg::log_info_f("Disassembling scenario: {}", scen_name);
        std::string scen_out_dir = std::format("{}/{}", out_dir, scen_name);
        return disassemble_scenario(
            scen,
            scen_out_dir,
            script_only ? nullptr : &image_saver,
            show_unused_tile_ids,
            generate_maps_as_json,
            show_random_rects);
      }
    };

    phosg::parallel_range(scenario_names, disassemble_item, parallelism);

  } else if (save_dir.empty()) { // Disassembling global data
    // Use scenario_dir as out_dir when out_dir is empty (this is the global data case)
    RealmzGlobalData global(data_dir);
    return disassemble_global_data(global, scenario_dir, script_only ? nullptr : &image_saver);

  } else { // Disassembling a scenario or saved game
    size_t slash_pos = scenario_dir.rfind('/');
    string scenario_name = (slash_pos == string::npos) ? scenario_dir : scenario_dir.substr(slash_pos + 1);

    RealmzGlobalData global(data_dir);
    RealmzScenarioData scen(global, scenario_dir, scenario_name);

    if (out_dir.empty()) { // Disassembling a scenario
      // Use save_dir as out_dir when out_dir is empty
      return disassemble_scenario(
          scen,
          save_dir,
          script_only ? nullptr : &image_saver,
          show_unused_tile_ids,
          generate_maps_as_json,
          show_random_rects);
    } else {
      RealmzSaveData save(scen, save_dir);
      return disassemble_saved_game(save, out_dir, script_only ? nullptr : &image_saver);
    }
  }
}
