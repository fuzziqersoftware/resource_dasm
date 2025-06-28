#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <filesystem>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <unordered_map>
#include <vector>

#include "ImageSaver.hh"
#include "IndexFormats/Formats.hh"
#include "RealmzGlobalData.hh"
#include "RealmzScenarioData.hh"

using namespace std;
using namespace phosg;
using namespace ResourceDASM;

int disassemble_scenario(
    const string& data_dir,
    const string& scenario_dir,
    const string& out_dir,
    const ImageSaver* image_saver,
    bool show_unused_tile_ids,
    bool generate_maps_as_json) {

  string scenario_name;
  {
    size_t where = scenario_dir.rfind('/');
    if (where == string::npos) {
      scenario_name = scenario_dir;
    } else {
      scenario_name = scenario_dir.substr(where + 1);
    }
  }

  RealmzGlobalData global(data_dir);
  RealmzScenarioData scen(global, scenario_dir, scenario_name);

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
    fwrite_fmt(stderr, "... {} (global metadata)\n", filename);

    fwritex(f.get(), scen.disassemble_scenario_metadata());
    fwrite_fmt(stderr, "... {} (scenario metadata)\n", filename);

    fwritex(f.get(), scen.disassemble_restrictions());
    fwrite_fmt(stderr, "... {} (restrictions)\n", filename);

    fwritex(f.get(), scen.disassemble_solids());
    fwrite_fmt(stderr, "... {} (solids)\n", filename);

    for (auto it : scen.land_type_to_tileset_definition) {
      if (!it.first.starts_with("custom")) {
        continue; // skip default tilesets
      }
      fwritex(f.get(), scen.global.disassemble_tileset_definition(it.second, it.first.c_str()));
      fwrite_fmt(stderr, "... {} ({} land tileset)\n", filename, it.first);
    }

    fwritex(f.get(), scen.disassemble_all_monsters());
    fwrite_fmt(stderr, "... {} (monsters)\n", filename);

    fwritex(f.get(), scen.disassemble_all_battles());
    fwrite_fmt(stderr, "... {} (battles)\n", filename);

    fwritex(f.get(), scen.disassemble_all_custom_item_definitions());
    fwrite_fmt(stderr, "... {} (items)\n", filename);

    fwritex(f.get(), scen.disassemble_all_shops());
    fwrite_fmt(stderr, "... {} (shops)\n", filename);

    fwritex(f.get(), scen.disassemble_all_treasures());
    fwrite_fmt(stderr, "... {} (treasures)\n", filename);

    fwritex(f.get(), scen.disassemble_all_party_maps());
    fwrite_fmt(stderr, "... {} (party_maps)\n", filename);

    fwritex(f.get(), scen.disassemble_all_simple_encounters());
    fwrite_fmt(stderr, "... {} (simple encounters)\n", filename);

    fwritex(f.get(), scen.disassemble_all_complex_encounters());
    fwrite_fmt(stderr, "... {} (complex encounters)\n", filename);

    fwritex(f.get(), scen.disassemble_all_rogue_encounters());
    fwrite_fmt(stderr, "... {} (rogue encounters)\n", filename);

    fwritex(f.get(), scen.disassemble_all_time_encounters());
    fwrite_fmt(stderr, "... {} (time encounters)\n", filename);

    fwritex(f.get(), scen.disassemble_all_level_aps_and_rrs(true));
    fwrite_fmt(stderr, "... {} (dungeon APs and RRs)\n", filename);

    fwritex(f.get(), scen.disassemble_all_level_aps_and_rrs(false));
    fwrite_fmt(stderr, "... {} (land APs and RRs)\n", filename);

    fwritex(f.get(), scen.disassemble_all_xaps());
    fwrite_fmt(stderr, "... {} (extra APs)\n", filename);
  }

  if (!image_saver) {
    return 0;
  }

  // Save media
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_PICT)) {
    auto decoded = scen.scenario_rsf.decode_PICT(id);
    string filename = std::format("{}/media/picture_{}", out_dir, id);
    filename = image_saver->save_image(decoded.image, filename);
    fwrite_fmt(stderr, "... {}\n", filename);
  }
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
    auto decoded = scen.scenario_rsf.decode_cicn(id);
    string filename = std::format("{}/media/icon_{}", out_dir, id);
    filename = image_saver->save_image(decoded.image, filename);
    fwrite_fmt(stderr, "... {}\n", filename);
  }
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_snd)) {
    auto decoded = scen.scenario_rsf.decode_snd(id);
    string filename = std::format("{}/media/snd_{}.wav", out_dir, id);
    save_file(filename, decoded.data);
    fwrite_fmt(stderr, "... {}\n", filename);
  }
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_TEXT)) {
    try {
      if (!scen.scenario_rsf.resource_exists(RESOURCE_TYPE_styl, id)) {
        throw runtime_error("TEXT resource has no corresponding styl resource");
      }
      string filename = std::format("{}/media/text_{}.rtf", out_dir, id);
      save_file(filename, scen.scenario_rsf.decode_styl(id));
      fwrite_fmt(stderr, "... {}\n", filename);
    } catch (const runtime_error& e) {
      string filename = std::format("{}/media/text_{}.txt", out_dir, id);
      save_file(filename, scen.scenario_rsf.decode_TEXT(id));
      fwrite_fmt(stderr, "*** {} (style rendering failed: {})\n", filename, e.what());
    }
  }

  // Generate custom tileset legends
  for (auto it : scen.land_type_to_tileset_definition) {
    if (!it.first.starts_with("custom")) {
      continue; // skip default tilesets
    }
    string filename = std::format("{}/tileset_{}_legend",
        out_dir, it.first);
    int16_t resource_id = scen.global.pict_resource_id_for_land_type(it.first);
    if (!scen.scenario_rsf.resource_exists(RESOURCE_TYPE_PICT, resource_id)) {
      fwrite_fmt(stderr, "### {} FAILED: PICT {} is missing\n", filename, resource_id);
    } else {
      Image positive_pattern = scen.scenario_rsf.decode_PICT(resource_id).image;
      Image legend = scen.global.generate_tileset_definition_legend(it.second, positive_pattern);
      filename = image_saver->save_image(legend, filename);
      fwrite_fmt(stderr, "... {}\n", filename);
    }
  }

  // Generate dungeon maps
  for (size_t z = 0; z < scen.dungeon_maps.size(); z++) {
    string filename = std::format("{}/dungeon_{}", out_dir, z);
    if (generate_maps_as_json) {
      filename += ".json";
      string s = scen.generate_dungeon_map_json(z);
      save_file(filename, s);
      fwrite_fmt(stderr, "... {}\n", filename);
    } else {
      Image map = scen.generate_dungeon_map(z, 0, 0, 90, 90);
      filename = image_saver->save_image(map, filename);
      fwrite_fmt(stderr, "... {}\n", filename);
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
        fwrite_fmt(stderr, "... {}\n", filename);
      } else {
        Image map = scen.generate_land_map(
            z, 0, 0, 90, 90, &used_negative_tiles, &used_positive_tiles);
        filename = image_saver->save_image(map, filename);
        fwrite_fmt(stderr, "... {}\n", filename);
      }
    } catch (const exception& e) {
      fwrite_fmt(stderr, "### {} FAILED: {}\n", filename, e.what());
    }
  }

  // Generate party maps
  for (size_t z = 0; z < scen.party_maps.size(); z++) {
    string filename = std::format("{}/map_{}", out_dir, z);
    try {
      Image map = scen.render_party_map(z);
      filename = image_saver->save_image(map, filename);
      fwrite_fmt(stderr, "... {}\n", filename);
    } catch (const exception& e) {
      fwrite_fmt(stderr, "### {} FAILED: {}\n", filename, e.what());
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

    Image connected_map = scen.generate_layout_map(layout_component);
    filename = image_saver->save_image(connected_map, filename);
    fwrite_fmt(stderr, "... {}\n", filename);
  }

  // Find unused land tiles
  if (show_unused_tile_ids) {
    for (const auto& it : used_positive_tiles) {
      for (uint8_t z = 0; z < 200; z++) {
        if (!it.second.count(z)) {
          fwrite_fmt(stderr, ">>> unused positive tile: {}-{} (x={}, y={} in positive pattern)\n",
              it.first, z, static_cast<uint8_t>(z % 20),
              static_cast<uint8_t>(z / 20));
        }
      }
    }
    for (int16_t z : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
      if (!used_negative_tiles.count(z)) {
        fwrite_fmt(stderr, ">>> unused negative tile: {}\n", z);
      }
    }
  }

  return 0;
}

int disassemble_global_data(const string& data_dir, const string& out_dir, const ImageSaver* image_saver) {

  RealmzGlobalData global(data_dir);

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
      fwrite_fmt(stderr, "... {}\n", filename);
    }

    filename = std::format("{}/castes.txt", out_dir);
    save_file(filename, global.disassemble_all_caste_definitions());
    fwrite_fmt(stderr, "... {}\n", filename);

    filename = std::format("{}/races.txt", out_dir);
    save_file(filename, global.disassemble_all_race_definitions());
    fwrite_fmt(stderr, "... {}\n", filename);

    filename = std::format("{}/items.txt", out_dir);
    save_file(filename, global.disassemble_all_item_definitions());
    fwrite_fmt(stderr, "... {}\n", filename);

    filename = std::format("{}/spells.txt", out_dir);
    save_file(filename, global.disassemble_all_spell_definitions());
    fwrite_fmt(stderr, "... {}\n", filename);
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
    fwrite_fmt(stderr, "... {}\n", filename);
  }
  for (int16_t id : global.global_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
    auto decoded = global.global_rsf.decode_cicn(id);
    string filename = std::format("{}/media/icon_{}", out_dir, id);
    filename = image_saver->save_image(decoded.image, filename);
    fwrite_fmt(stderr, "... {}\n", filename);
  }
  for (int16_t id : global.portraits_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
    auto decoded = global.portraits_rsf.decode_cicn(id);
    string filename = std::format("{}/media/portrait_{}", out_dir, id);
    filename = image_saver->save_image(decoded.image, filename);
    fwrite_fmt(stderr, "... {}\n", filename);
  }
  for (int16_t id : global.tacticals_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
    auto decoded = global.tacticals_rsf.decode_cicn(id);
    string filename = std::format("{}/media/battle_icon_{}", out_dir, id);
    filename = image_saver->save_image(decoded.image, filename);
    fwrite_fmt(stderr, "... {}\n", filename);
  }
  for (int16_t id : global.global_rsf.all_resources_of_type(RESOURCE_TYPE_snd)) {
    string filename = std::format("{}/media/snd_{}.wav", out_dir, id);
    save_file(filename, global.global_rsf.decode_snd(id).data);
    fwrite_fmt(stderr, "... {}\n", filename);
  }
  for (int16_t id : global.global_rsf.all_resources_of_type(RESOURCE_TYPE_TEXT)) {
    try {
      if (!global.global_rsf.resource_exists(RESOURCE_TYPE_styl, id)) {
        throw runtime_error("TEXT resource has no corresponding styl resource");
      }
      string filename = std::format("{}/media/text_{}.rtf", out_dir, id);
      save_file(filename, global.global_rsf.decode_styl(id));
      fwrite_fmt(stderr, "... {}\n", filename);
    } catch (const runtime_error& e) {
      string filename = std::format("{}/media/text_{}.txt", out_dir, id);
      save_file(filename, global.global_rsf.decode_TEXT(id));
      fwrite_fmt(stderr, "*** {} (style rendering failed: {})\n", filename, e.what());
    }
  }

  // Generate tileset legends
  for (auto it : global.land_type_to_tileset_definition) {
    string filename = std::format("{}/tileset_{}_legend",
        out_dir, it.first);
    int16_t resource_id = global.pict_resource_id_for_land_type(it.first);
    Image positive_pattern = global.global_rsf.decode_PICT(resource_id).image;
    Image legend = global.generate_tileset_definition_legend(it.second, positive_pattern);
    filename = image_saver->save_image(legend, filename);
    fwrite_fmt(stderr, "... {}\n", filename);
  }

  return 0;
}

static void print_usage() {
  fwrite_fmt(stderr, "\
Usage: realmz_dasm [options] data_dir [scenario_dir] out_dir [options]\n\
\n" IMAGE_SAVER_HELP);
}

int main(int argc, char* argv[]) {
  string data_dir;
  string scenario_dir;
  string out_dir;
  ImageSaver image_saver;
  bool show_unused_tile_ids = false;
  bool generate_maps_as_json = false;
  bool script_only = false;
  for (int x = 1; x < argc; x++) {
    if (image_saver.process_cli_arg(argv[x])) {
      // Nothing
    } else if (!strcmp(argv[x], "--show-unused-tiles")) {
      show_unused_tile_ids = true;
    } else if (!strcmp(argv[x], "--maps-as-json")) {
      generate_maps_as_json = true;
    } else if (!strcmp(argv[x], "--script-only")) {
      script_only = true;
    } else if (data_dir.empty()) {
      data_dir = argv[x];
    } else if (scenario_dir.empty()) {
      scenario_dir = argv[x];
    } else if (out_dir.empty()) {
      out_dir = argv[x];
    } else {
      fwrite_fmt(stderr, "excess argument: {}\n", argv[x]);
      print_usage();
      return 2;
    }
  }

  if (out_dir.empty()) {
    // Use <scenario_dir> as <out_dir> when <out_dir> is empty
    swap(scenario_dir, out_dir);
  }
  if (data_dir.empty() || out_dir.empty()) {
    print_usage();
    return 2;
  }

  if (!scenario_dir.empty()) {
    return disassemble_scenario(data_dir, scenario_dir, out_dir, script_only ? nullptr : &image_saver, show_unused_tile_ids, generate_maps_as_json);
  } else {
    return disassemble_global_data(data_dir, out_dir, script_only ? nullptr : &image_saver);
  }
}
