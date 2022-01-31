#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <unordered_map>
#include <vector>

#include "IndexFormats/ResourceFork.hh"
#include "RealmzGlobalData.hh"
#include "RealmzScenarioData.hh"

using namespace std;



int disassemble_scenario(const string& data_dir, const string& scenario_dir,
    const string& out_dir) {

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
    string filename = string_printf("%s/media", out_dir.c_str());
    mkdir(filename.c_str(), 0755);
  }

  // Disassemble scenario text
  {
    string filename = string_printf("%s/script.txt", out_dir.c_str());
    auto f = fopen_unique(filename.c_str(), "wt");

    // global metadata
    fwritex(f.get(), scen.disassemble_globals());
    fprintf(stderr, "... %s (global metadata)\n", filename.c_str());
 
    // treasures
    fwritex(f.get(), scen.disassemble_all_treasures());
    fprintf(stderr, "... %s (treasures)\n", filename.c_str());

    // party maps
    fwritex(f.get(), scen.disassemble_all_party_maps());
    fprintf(stderr, "... %s (party_maps)\n", filename.c_str());

    // simple encounters
    fwritex(f.get(), scen.disassemble_all_simple_encounters());
    fprintf(stderr, "... %s (simple encounters)\n", filename.c_str());

    // complex encounters
    fwritex(f.get(), scen.disassemble_all_complex_encounters());
    fprintf(stderr, "... %s (complex encounters)\n", filename.c_str());

    // rogue encounters
    fwritex(f.get(), scen.disassemble_all_rogue_encounters());
    fprintf(stderr, "... %s (rogue encounters)\n", filename.c_str());

    // time encounters
    fwritex(f.get(), scen.disassemble_all_time_encounters());
    fprintf(stderr, "... %s (time encounters)\n", filename.c_str());

    // dungeon APs
    fwritex(f.get(), scen.disassemble_all_level_aps(true));
    fprintf(stderr, "... %s (dungeon APs)\n", filename.c_str());

    // land APs
    fwritex(f.get(), scen.disassemble_all_level_aps(false));
    fprintf(stderr, "... %s (land APs)\n", filename.c_str());

    // extra APs
    fwritex(f.get(), scen.disassemble_all_xaps());
    fprintf(stderr, "... %s (extra APs)\n", filename.c_str());
  }

  // Save media
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_PICT)) {
    auto decoded = scen.scenario_rsf.decode_PICT(id);
    string filename = string_printf("%s/media/picture_%d.bmp", out_dir.c_str(), id);
    decoded.image.save(filename.c_str(), Image::WindowsBitmap);
    fprintf(stderr, "... %s\n", filename.c_str());
  }
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
    auto decoded = scen.scenario_rsf.decode_cicn(id);
    string filename = string_printf("%s/media/icon_%d.bmp", out_dir.c_str(), id);
    decoded.image.save(filename.c_str(), Image::WindowsBitmap);
    fprintf(stderr, "... %s\n", filename.c_str());
  }
  for (int16_t id : scen.scenario_rsf.all_resources_of_type(RESOURCE_TYPE_snd)) {
    auto decoded = scen.scenario_rsf.decode_snd(id);
    string filename = string_printf("%s/media/snd_%d.wav", out_dir.c_str(), id);
    save_file(filename.c_str(), decoded.data);
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
    string filename = string_printf("%s/tileset_%s_legend.bmp",
        out_dir.c_str(), it.first.c_str());
    int16_t resource_id = resource_id_for_land_type(it.first);
    if (!scen.scenario_rsf.resource_exists(RESOURCE_TYPE_PICT, resource_id)) {
      fprintf(stderr, "### %s FAILED: PICT %hd is missing\n", filename.c_str(), resource_id);
    } else {
      Image positive_pattern = scen.scenario_rsf.decode_PICT(resource_id).image;
      Image legend = generate_tileset_definition_legend(it.second, positive_pattern);
      legend.save(filename.c_str(), Image::WindowsBitmap);
      fprintf(stderr, "... %s\n", filename.c_str());
    }
  }

  // Generate dungeon maps
  for (size_t z = 0; z < scen.dungeon_maps.size(); z++) {
    string filename = string_printf("%s/dungeon_%zu.bmp", out_dir.c_str(), z);
    Image map = scen.generate_dungeon_map(z, 0, 0, 90, 90);
    map.save(filename.c_str(), Image::WindowsBitmap);
    fprintf(stderr, "... %s\n", filename.c_str());
  }

  // Generate land maps
  unordered_map<int16_t, string> level_id_to_filename;
  for (size_t z = 0; z < scen.land_maps.size(); z++) {
    string filename = string_printf("%s/land_%zu.bmp", out_dir.c_str(), z);
    try {
      Image map = scen.generate_land_map(z, 0, 0, 90, 90);
      map.save(filename.c_str(), Image::WindowsBitmap);
      fprintf(stderr, "... %s\n", filename.c_str());
      level_id_to_filename[z] = filename;
    } catch (const exception& e) {
      fprintf(stderr, "### %s FAILED: %s\n", filename.c_str(), e.what());
    }
  }

  // Generate party maps
  for (size_t z = 0; z < scen.party_maps.size(); z++) {
    string filename = string_printf("%s/map_%zu.bmp", out_dir.c_str(), z);
    try {
      Image map = scen.render_party_map(z);
      map.save(filename.c_str(), Image::WindowsBitmap);
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
          filename += string_printf("_%d", layout_component.layout[y][x]);
        }
      }
    }
    filename += ".bmp";

    Image connected_map = scen.generate_layout_map(layout_component,
        level_id_to_filename);
    connected_map.save(filename.c_str(), Image::WindowsBitmap);
    fprintf(stderr, "... %s\n", filename.c_str());
  }

  return 0;
}



int disassemble_global_data(const string& data_dir, const string& out_dir) {
  RealmzGlobalData global(data_dir);

  // Make necessary directories for output
  {
    mkdir(out_dir.c_str(), 0755);
    string filename = string_printf("%s/media", out_dir.c_str());
    mkdir(filename.c_str(), 0755);
  }

  // Save media
  // TODO: factor this out somehow with scenario media exporting code
  for (int16_t id : global.global_rsf.all_resources_of_type(RESOURCE_TYPE_PICT)) {
    auto decoded = global.global_rsf.decode_PICT(id);
    string filename = string_printf("%s/media/picture_%d.bmp", out_dir.c_str(), id);
    decoded.image.save(filename.c_str(), Image::WindowsBitmap);
    fprintf(stderr, "... %s\n", filename.c_str());
  }
  for (int16_t id : global.global_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
    auto decoded = global.global_rsf.decode_cicn(id);
    string filename = string_printf("%s/media/icon_%d.bmp", out_dir.c_str(), id);
    decoded.image.save(filename.c_str(), Image::WindowsBitmap);
    fprintf(stderr, "... %s\n", filename.c_str());
  }
  for (int16_t id : global.portraits_rsf.all_resources_of_type(RESOURCE_TYPE_cicn)) {
    auto decoded = global.portraits_rsf.decode_cicn(id);
    string filename = string_printf("%s/media/portrait_%d.bmp", out_dir.c_str(), id);
    decoded.image.save(filename.c_str(), Image::WindowsBitmap);
    fprintf(stderr, "... %s\n", filename.c_str());
  }
  for (int16_t id : global.global_rsf.all_resources_of_type(RESOURCE_TYPE_snd)) {
    string filename = string_printf("%s/media/snd_%d.wav", out_dir.c_str(), id);
    save_file(filename.c_str(), global.global_rsf.decode_snd(id).data);
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

  // Generate custom tileset legends
  for (auto it : global.land_type_to_tileset_definition) {
    string filename = string_printf("%s/tileset_%s_legend.bmp",
        out_dir.c_str(), it.first.c_str());
    int16_t resource_id = resource_id_for_land_type(it.first);
    Image positive_pattern = global.global_rsf.decode_PICT(resource_id).image;
    Image legend = generate_tileset_definition_legend(it.second, positive_pattern);
    legend.save(filename.c_str(), Image::WindowsBitmap);
    fprintf(stderr, "... %s\n", filename.c_str());
  }

  return 0;
}



int main(int argc, char* argv[]) {
  if (argc < 3 || argc > 4) {
    fprintf(stderr, "Usage: %s data_dir [scenario_dir] out_dir\n", argv[0]);
    return 1;
  }

  if (argc == 4) {
    return disassemble_scenario(argv[1], argv[2], argv[3]);
  } else {
    return disassemble_global_data(argv[1], argv[2]);
  }
}
