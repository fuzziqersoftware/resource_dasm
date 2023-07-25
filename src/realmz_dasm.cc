#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <unordered_map>
#include <vector>

#include "ImageSaver.hh"
#include "IndexFormats/Formats.hh"
#include "RealmzGlobalData.hh"
#include "RealmzScenarioData.hh"

using namespace std;

int disassemble_scenario(
    const string& data_dir,
    const string& scenario_dir,
    const string& out_dir,
    const ImageSaver* image_saver,
    bool show_unused_tile_ids,
    bool generate_maps_as_text) {

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
  {
    mkdir(out_dir.c_str(), 0755);
    if (image_saver) {
      string filename = string_printf("%s/media", out_dir.c_str());
      mkdir(filename.c_str(), 0755);
    }
  }

  // Disassemble scenario text
  {
    string filename = string_printf("%s/script.txt", out_dir.c_str());
    auto f = fopen_unique(filename.c_str(), "wt");

    fwritex(f.get(), scen.disassemble_global_metadata());
    fprintf(stderr, "... %s (global metadata)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_scenario_metadata());
    fprintf(stderr, "... %s (scenario metadata)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_restrictions());
    fprintf(stderr, "... %s (restrictions)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_solids());
    fprintf(stderr, "... %s (solids)\n", filename.c_str());

    for (auto it : scen.land_type_to_tileset_definition) {
      if (!starts_with(it.first, "custom")) {
        continue; // skip default tilesets
      }
      fwritex(f.get(), scen.global.disassemble_tileset_definition(it.second, it.first.c_str()));
      fprintf(stderr, "... %s (%s land tileset)\n", filename.c_str(), it.first.c_str());
    }

    fwritex(f.get(), scen.disassemble_all_monsters());
    fprintf(stderr, "... %s (monsters)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_all_battles());
    fprintf(stderr, "... %s (battles)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_all_custom_item_definitions());
    fprintf(stderr, "... %s (items)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_all_shops());
    fprintf(stderr, "... %s (shops)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_all_treasures());
    fprintf(stderr, "... %s (treasures)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_all_party_maps());
    fprintf(stderr, "... %s (party_maps)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_all_simple_encounters());
    fprintf(stderr, "... %s (simple encounters)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_all_complex_encounters());
    fprintf(stderr, "... %s (complex encounters)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_all_rogue_encounters());
    fprintf(stderr, "... %s (rogue encounters)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_all_time_encounters());
    fprintf(stderr, "... %s (time encounters)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_all_level_aps(true));
    fprintf(stderr, "... %s (dungeon APs)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_all_level_aps(false));
    fprintf(stderr, "... %s (land APs)\n", filename.c_str());

    fwritex(f.get(), scen.disassemble_all_xaps());
    fprintf(stderr, "... %s (extra APs)\n", filename.c_str());
  }

  if (!image_saver) {
    return 0;
  }

  // Save media
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_PICT)) {
    auto decoded = scen.scenario_rsf.decode_PICT(id);
    string filename = string_printf("%s/media/picture_%d", out_dir.c_str(), id);
    filename = image_saver->save_image(decoded.image, filename);
    fprintf(stderr, "... %s\n", filename.c_str());
  }
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
    auto decoded = scen.scenario_rsf.decode_cicn(id);
    string filename = string_printf("%s/media/icon_%d", out_dir.c_str(), id);
    filename = image_saver->save_image(decoded.image, filename);
    fprintf(stderr, "... %s\n", filename.c_str());
  }
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_snd)) {
    auto decoded = scen.scenario_rsf.decode_snd(id);
    string filename = string_printf("%s/media/snd_%d.wav", out_dir.c_str(), id);
    save_file(filename, decoded.data);
    fprintf(stderr, "... %s\n", filename.c_str());
  }
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_TEXT)) {
    try {
      if (!scen.scenario_rsf.resource_exists(RESOURCE_TYPE_styl, id)) {
        throw runtime_error("TEXT resource has no corresponding styl resource");
      }
      string filename = string_printf("%s/media/text_%d.rtf", out_dir.c_str(), id);
      save_file(filename, scen.scenario_rsf.decode_styl(id));
      fprintf(stderr, "... %s\n", filename.c_str());
    } catch (const runtime_error& e) {
      string filename = string_printf("%s/media/text_%d.txt", out_dir.c_str(), id);
      save_file(filename, scen.scenario_rsf.decode_TEXT(id));
      fprintf(stderr, "*** %s (style rendering failed: %s)\n", filename.c_str(), e.what());
    }
  }

  // Generate custom tileset legends
  for (auto it : scen.land_type_to_tileset_definition) {
    if (!starts_with(it.first, "custom")) {
      continue; // skip default tilesets
    }
    string filename = string_printf("%s/tileset_%s_legend",
        out_dir.c_str(), it.first.c_str());
    int16_t resource_id = scen.global.pict_resource_id_for_land_type(it.first);
    if (!scen.scenario_rsf.resource_exists(RESOURCE_TYPE_PICT, resource_id)) {
      fprintf(stderr, "### %s FAILED: PICT %hd is missing\n", filename.c_str(), resource_id);
    } else {
      Image positive_pattern = scen.scenario_rsf.decode_PICT(resource_id).image;
      Image legend = scen.global.generate_tileset_definition_legend(it.second, positive_pattern);
      filename = image_saver->save_image(legend, filename);
      fprintf(stderr, "... %s\n", filename.c_str());
    }
  }

  // Generate dungeon maps
  for (size_t z = 0; z < scen.dungeon_maps.size(); z++) {
    string filename = string_printf("%s/dungeon_%zu", out_dir.c_str(), z);
    if (generate_maps_as_text) {
      filename += ".txt";
      string s = scen.generate_dungeon_map_text(z);
      save_file(filename, s);
      fprintf(stderr, "... %s\n", filename.c_str());
    } else {
      Image map = scen.generate_dungeon_map(z, 0, 0, 90, 90);
      filename = image_saver->save_image(map, filename);
      fprintf(stderr, "... %s\n", filename.c_str());
    }
  }

  // Generate land maps
  unordered_set<int16_t> used_negative_tiles;
  unordered_map<string, unordered_set<uint8_t>> used_positive_tiles;
  for (size_t z = 0; z < scen.land_maps.size(); z++) {
    string filename = string_printf("%s/land_%zu", out_dir.c_str(), z);
    try {
      if (generate_maps_as_text) {
        filename += ".txt";
        string s = scen.generate_land_map_text(z);
        save_file(filename, s);
        fprintf(stderr, "... %s\n", filename.c_str());
      } else {
        Image map = scen.generate_land_map(
            z, 0, 0, 90, 90, &used_negative_tiles, &used_positive_tiles);
        filename = image_saver->save_image(map, filename);
        fprintf(stderr, "... %s\n", filename.c_str());
      }
    } catch (const exception& e) {
      fprintf(stderr, "### %s FAILED: %s\n", filename.c_str(), e.what());
    }
  }

  // Generate party maps
  for (size_t z = 0; z < scen.party_maps.size(); z++) {
    string filename = string_printf("%s/map_%zu", out_dir.c_str(), z);
    try {
      Image map = scen.render_party_map(z);
      filename = image_saver->save_image(map, filename);
      fprintf(stderr, "... %s\n", filename.c_str());
    } catch (const exception& e) {
      fprintf(stderr, "### %s FAILED: %s\n", filename.c_str(), e.what());
    }
  }

  // Generate connected land map
  for (auto layout_component : scen.layout.get_connected_components()) {
    if (layout_component.num_valid_levels() < 2) {
      continue;
    }
    string filename = string_printf("%s/land_connected", out_dir.c_str());
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 16; x++) {
        if (layout_component.layout[y][x] != -1) {
          filename += string_printf("_%d", layout_component.layout[y][x].load());
        }
      }
    }

    Image connected_map = scen.generate_layout_map(layout_component);
    filename = image_saver->save_image(connected_map, filename);
    fprintf(stderr, "... %s\n", filename.c_str());
  }

  // Find unused land tiles
  if (show_unused_tile_ids) {
    for (const auto& it : used_positive_tiles) {
      for (uint8_t z = 0; z < 200; z++) {
        if (!it.second.count(z)) {
          fprintf(stderr, ">>> unused positive tile: %s-%hhu (x=%hhu, y=%hhu in positive pattern)\n",
              it.first.c_str(), z, static_cast<uint8_t>(z % 20),
              static_cast<uint8_t>(z / 20));
        }
      }
    }
    for (int16_t z : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
      if (!used_negative_tiles.count(z)) {
        fprintf(stderr, ">>> unused negative tile: %hd\n", z);
      }
    }
  }

  return 0;
}

int disassemble_global_data(
    const string& data_dir, const string& out_dir, const ImageSaver* image_saver) {

  RealmzGlobalData global(data_dir);

  // Make necessary directories for output
  {
    mkdir(out_dir.c_str(), 0755);
    if (image_saver) {
      string filename = string_printf("%s/media", out_dir.c_str());
      mkdir(filename.c_str(), 0755);
    }
  }

  // Disassemble non-media data
  {
    string filename;

    for (auto it : global.land_type_to_tileset_definition) {
      filename = string_printf("%s/tileset_%s.txt", out_dir.c_str(), it.first.c_str());
      save_file(filename, global.disassemble_tileset_definition(it.second, it.first.c_str()));
      fprintf(stderr, "... %s\n", filename.c_str());
    }

    filename = string_printf("%s/castes.txt", out_dir.c_str());
    save_file(filename, global.disassemble_all_caste_definitions());
    fprintf(stderr, "... %s\n", filename.c_str());

    filename = string_printf("%s/races.txt", out_dir.c_str());
    save_file(filename, global.disassemble_all_race_definitions());
    fprintf(stderr, "... %s\n", filename.c_str());

    filename = string_printf("%s/items.txt", out_dir.c_str());
    save_file(filename, global.disassemble_all_item_definitions());
    fprintf(stderr, "... %s\n", filename.c_str());

    filename = string_printf("%s/spells.txt", out_dir.c_str());
    save_file(filename, global.disassemble_all_spell_definitions());
    fprintf(stderr, "... %s\n", filename.c_str());
  }

  if (!image_saver) {
    return 0;
  }

  // Save media
  // TODO: factor this out somehow with scenario media exporting code
  for (int16_t id : global.global_rsf.all_resources_of_type(RESOURCE_TYPE_PICT)) {
    auto decoded = global.global_rsf.decode_PICT(id);
    string filename = string_printf("%s/media/picture_%d", out_dir.c_str(), id);
    filename = image_saver->save_image(decoded.image, filename);
    fprintf(stderr, "... %s\n", filename.c_str());
  }
  for (int16_t id : global.global_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
    auto decoded = global.global_rsf.decode_cicn(id);
    string filename = string_printf("%s/media/icon_%d", out_dir.c_str(), id);
    filename = image_saver->save_image(decoded.image, filename);
    fprintf(stderr, "... %s\n", filename.c_str());
  }
  for (int16_t id : global.portraits_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
    auto decoded = global.portraits_rsf.decode_cicn(id);
    string filename = string_printf("%s/media/portrait_%d", out_dir.c_str(), id);
    filename = image_saver->save_image(decoded.image, filename);
    fprintf(stderr, "... %s\n", filename.c_str());
  }
  for (int16_t id : global.global_rsf.all_resources_of_type(RESOURCE_TYPE_snd)) {
    string filename = string_printf("%s/media/snd_%d.wav", out_dir.c_str(), id);
    save_file(filename, global.global_rsf.decode_snd(id).data);
    fprintf(stderr, "... %s\n", filename.c_str());
  }
  for (int16_t id : global.global_rsf.all_resources_of_type(RESOURCE_TYPE_TEXT)) {
    try {
      if (!global.global_rsf.resource_exists(RESOURCE_TYPE_styl, id)) {
        throw runtime_error("TEXT resource has no corresponding styl resource");
      }
      string filename = string_printf("%s/media/text_%d.rtf", out_dir.c_str(), id);
      save_file(filename, global.global_rsf.decode_styl(id));
      fprintf(stderr, "... %s\n", filename.c_str());
    } catch (const runtime_error& e) {
      string filename = string_printf("%s/media/text_%d.txt", out_dir.c_str(), id);
      save_file(filename, global.global_rsf.decode_TEXT(id));
      fprintf(stderr, "*** %s (style rendering failed: %s)\n", filename.c_str(), e.what());
    }
  }

  // Generate tileset legends
  for (auto it : global.land_type_to_tileset_definition) {
    string filename = string_printf("%s/tileset_%s_legend",
        out_dir.c_str(), it.first.c_str());
    int16_t resource_id = global.pict_resource_id_for_land_type(it.first);
    Image positive_pattern = global.global_rsf.decode_PICT(resource_id).image;
    Image legend = global.generate_tileset_definition_legend(it.second, positive_pattern);
    filename = image_saver->save_image(legend, filename);
    fprintf(stderr, "... %s\n", filename.c_str());
  }

  return 0;
}

static void print_usage() {
  fprintf(stderr, "\
Usage: realmz_dasm [options] data_dir [scenario_dir] out_dir [options]\n\
\n" IMAGE_SAVER_HELP);
}

int main(int argc, char* argv[]) {
  string data_dir;
  string scenario_dir;
  string out_dir;
  ImageSaver image_saver;
  bool show_unused_tile_ids = false;
  bool generate_maps_as_text = false;
  bool script_only = false;
  for (int x = 1; x < argc; x++) {
    if (image_saver.process_cli_arg(argv[x])) {
      // Nothing
    } else if (!strcmp(argv[x], "--show-unused-tiles")) {
      show_unused_tile_ids = true;
    } else if (!strcmp(argv[x], "--maps-as-text")) {
      generate_maps_as_text = true;
    } else if (!strcmp(argv[x], "--script-only")) {
      script_only = true;
    } else if (data_dir.empty()) {
      data_dir = argv[x];
    } else if (scenario_dir.empty()) {
      scenario_dir = argv[x];
    } else if (out_dir.empty()) {
      out_dir = argv[x];
    } else {
      fprintf(stderr, "excess argument: %s\n", argv[x]);
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
    return disassemble_scenario(data_dir, scenario_dir, out_dir, script_only ? nullptr : &image_saver, show_unused_tile_ids, generate_maps_as_text);
  } else {
    return disassemble_global_data(data_dir, out_dir, script_only ? nullptr : &image_saver);
  }
}
