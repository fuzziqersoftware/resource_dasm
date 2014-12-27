#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <unordered_map>
#include <vector>

#include "realmz_lib.hh"
#include "util.hh"

using namespace std;

int main(int argc, char* argv[]) {

  printf("fuzziqer software realmz scenario disassembler\n\n");

  if (argc < 3) {
    printf("usage: %s scenario_dir out_dir\n", argv[0]);
    return 1;
  }

  string scenario_dir = argv[1];
  string out_dir = argv[2];

  string scenario_name;
  {
    size_t where = scenario_dir.rfind('/');
    if (where == string::npos)
      scenario_name = scenario_dir;
    else
      scenario_name = scenario_dir.substr(where + 1);
  }

  printf("scenario directory: %s\n", scenario_dir.c_str());
  printf("disassembly directory: %s\n", out_dir.c_str());

  // find all the files
  string scenario_metadata_name = scenario_dir + "/" + scenario_name;
  string global_metadata_name = first_file_that_exists({
      (scenario_dir + "/global"),
      (scenario_dir + "/Global")});
  string dungeon_map_index_name = first_file_that_exists({
      (scenario_dir + "/data_dl"),
      (scenario_dir + "/Data DL"),
      (scenario_dir + "/DATA DL")});
  string land_map_index_name = first_file_that_exists({
      (scenario_dir + "/data_ld"),
      (scenario_dir + "/Data LD"),
      (scenario_dir + "/DATA LD")});
  string string_index_name = first_file_that_exists({
      (scenario_dir + "/data_sd2"),
      (scenario_dir + "/Data SD2"),
      (scenario_dir + "/DATA SD2")});
  string ecodes_index_name = first_file_that_exists({
      (scenario_dir + "/data_edcd"),
      (scenario_dir + "/Data EDCD"),
      (scenario_dir + "/DATA EDCD")});
  string land_ap_index_name = first_file_that_exists({
      (scenario_dir + "/data_dd"),
      (scenario_dir + "/Data DD"),
      (scenario_dir + "/DATA DD")});
  string dungeon_ap_index_name = first_file_that_exists({
      (scenario_dir + "/data_ddd"),
      (scenario_dir + "/Data DDD"),
      (scenario_dir + "/DATA DDD")});
  string extra_ap_index_name = first_file_that_exists({
      (scenario_dir + "/data_ed3"),
      (scenario_dir + "/Data ED3"),
      (scenario_dir + "/DATA ED3")});
  string land_metadata_index_name = first_file_that_exists({
      (scenario_dir + "/data_rd"),
      (scenario_dir + "/Data RD"),
      (scenario_dir + "/DATA RD")});
  string dungeon_metadata_index_name = first_file_that_exists({
      (scenario_dir + "/data_rdd"),
      (scenario_dir + "/Data RDD"),
      (scenario_dir + "/DATA RDD")});
  string simple_encounter_index_name = first_file_that_exists({
      (scenario_dir + "/data_ed"),
      (scenario_dir + "/Data ED"),
      (scenario_dir + "/DATA ED")});
  string complex_encounter_index_name = first_file_that_exists({
      (scenario_dir + "/data_ed2"),
      (scenario_dir + "/Data ED2"),
      (scenario_dir + "/DATA ED2")});
  string treasure_index_name = first_file_that_exists({
      (scenario_dir + "/data_td"),
      (scenario_dir + "/Data TD"),
      (scenario_dir + "/DATA TD")});
  string rogue_encounter_index_name = first_file_that_exists({
      (scenario_dir + "/data_td2"),
      (scenario_dir + "/Data TD2"),
      (scenario_dir + "/DATA TD2")});
  string time_encounter_index_name = first_file_that_exists({
      (scenario_dir + "/data_td3"),
      (scenario_dir + "/Data TD3"),
      (scenario_dir + "/DATA TD3")});
  string scenario_resources_name = first_file_that_exists({
      (scenario_dir + "/scenario.rsf"),
      (scenario_dir + "/Scenario.rsf"),
      (scenario_dir + "/SCENARIO.RSF"),
      (scenario_dir + "/scenario/rsrc"),
      (scenario_dir + "/Scenario/rsrc"),
      (scenario_dir + "/SCENARIO/rsrc"),
      (scenario_dir + "/scenario/..namedfork/rsrc"),
      (scenario_dir + "/Scenario/..namedfork/rsrc"),
      (scenario_dir + "/SCENARIO/..namedfork/rsrc")});
  string the_family_jewels_name = first_file_that_exists({
      "the_family_jewels.rsf",
      "The Family Jewels.rsf",
      "THE FAMILY JEWELS.RSF",
      "the_family_jewels/rsrc",
      "The Family Jewels/rsrc",
      "THE FAMILY JEWELS/rsrc",
      "the_family_jewels/..namedfork/rsrc",
      "The Family Jewels/..namedfork/rsrc",
      "THE FAMILY JEWELS/..namedfork/rsrc"});

  // load images
  populate_image_caches(the_family_jewels_name);

  // load everything else
  printf("loading dungeon map index\n");
  vector<map_data> dungeon_maps = load_dungeon_map_index(dungeon_map_index_name);
  printf("loading land map index\n");
  vector<map_data> land_maps = load_land_map_index(land_map_index_name);
  printf("loading string index\n");
  vector<string> strings = load_string_index(string_index_name);
  printf("loading ecodes index\n");
  vector<ecodes> ecodes = load_ecodes_index(ecodes_index_name);
  printf("loading dungeon action point index\n");
  vector<vector<ap_info>> dungeon_aps = load_ap_index(dungeon_ap_index_name);
  printf("loading land action point index\n");
  vector<vector<ap_info>> land_aps = load_ap_index(land_ap_index_name);
  printf("loading extra action point index\n");
  vector<ap_info> xaps = load_xap_index(extra_ap_index_name);
  printf("loading dungeon map metadata index\n");
  vector<map_metadata> dungeon_metadata = load_map_metadata_index(dungeon_metadata_index_name);
  printf("loading land map metadata index\n");
  vector<map_metadata> land_metadata = load_map_metadata_index(land_metadata_index_name);
  printf("loading simple encounter index\n");
  vector<simple_encounter> simple_encs = load_simple_encounter_index(simple_encounter_index_name);
  printf("loading complex encounter index\n");
  vector<complex_encounter> complex_encs = load_complex_encounter_index(complex_encounter_index_name);
  printf("loading treasure index\n");
  vector<treasure> treasures = load_treasure_index(treasure_index_name);
  printf("loading rogue encounter index\n");
  vector<rogue_encounter> rogue_encs = load_rogue_encounter_index(rogue_encounter_index_name);
  printf("loading time encounter index\n");
  vector<time_encounter> time_encs = load_time_encounter_index(time_encounter_index_name);
  printf("loading global metadata\n");
  global_metadata global = load_global_metadata(global_metadata_name);
  printf("loading scenario metadata\n");
  scenario_metadata scen_metadata = load_scenario_metadata(scenario_metadata_name);
  printf("loading picture resources\n");
  unordered_map<int16_t, Image> picts = get_picts(scenario_resources_name);
  printf("loading icon resources\n");
  unordered_map<int16_t, Image> cicns = get_cicns(scenario_resources_name);
  printf("loading sound resources\n");
  unordered_map<int16_t, vector<uint8_t>> snds = get_snds(scenario_resources_name);
  printf("loading text resources\n");
  unordered_map<int16_t, string> texts = get_texts(scenario_resources_name);

  // load layout separately because it doesn't have to exist
  land_layout layout;
  {
    string fname = first_file_that_exists({
        (scenario_dir + "/layout"),
        (scenario_dir + "/Layout")});
    if (!fname.empty())
      layout = load_land_layout(fname);
    else
      printf("note: this scenario has no land layout information\n");
  }

  // if custom tilesets exist for this scenario, load them
  unordered_map<int, tileset_definition> custom_tilesets;
  for (int x = 1; x < 4; x++) {
    string fname = first_file_that_exists({
        string_printf("%s/data_custom_%d_bd", scenario_dir.c_str(), x),
        string_printf("%s/Data Custom %d BD", scenario_dir.c_str(), x),
        string_printf("%s/DATA CUSTOM %d BD", scenario_dir.c_str(), x)});
    if (!fname.empty()) {
      printf("loading custom tileset %d definition\n", x);
      custom_tilesets.emplace(x, load_tileset_definition(fname));
      populate_custom_tileset_configuration(string_printf("custom_%d", x),
          custom_tilesets[x]);
    }
  }

  // make necessary directories for output
  {
    string filename = string_printf("%s/media", out_dir.c_str());
    mkdir(filename.c_str(), 0755);
  }

  // disassemble scenario text
  {
    string filename = string_printf("%s/script.txt", out_dir.c_str());
    FILE* f = fopen_or_throw(filename.c_str(), "wt");

    // global metadata
    string data = disassemble_globals(global);
    fwrite(data.data(), data.size(), 1, f);
    printf("... %s (global metadata)\n", filename.c_str());

    // treasures
    data = disassemble_all_treasures(treasures);
    fwrite(data.data(), data.size(), 1, f);
    printf("... %s (treasures)\n", filename.c_str());

    // simple encounters
    data = disassemble_all_simple_encounters(simple_encs, ecodes, strings);
    fwrite(data.data(), data.size(), 1, f);
    printf("... %s (simple encounters)\n", filename.c_str());

    // complex encounters
    data = disassemble_all_complex_encounters(complex_encs, ecodes, strings);
    fwrite(data.data(), data.size(), 1, f);
    printf("... %s (complex encounters)\n", filename.c_str());

    // rogue encounters
    data = disassemble_all_rogue_encounters(rogue_encs, ecodes, strings);
    fwrite(data.data(), data.size(), 1, f);
    printf("... %s (rogue encounters)\n", filename.c_str());

    // time encounters
    data = disassemble_all_time_encounters(time_encs);
    fwrite(data.data(), data.size(), 1, f);
    printf("... %s (time encounters)\n", filename.c_str());

    // dungeon APs
    data = disassemble_all_aps(dungeon_aps, ecodes, strings, 1);
    fwrite(data.data(), data.size(), 1, f);
    printf("... %s (dungeon APs)\n", filename.c_str());

    // land APs
    data = disassemble_all_aps(land_aps, ecodes, strings, 0);
    fwrite(data.data(), data.size(), 1, f);
    printf("... %s (land APs)\n", filename.c_str());

    // extra APs
    data = disassemble_level_aps(-1, xaps, ecodes, strings, 0);
    fwrite(data.data(), data.size(), 1, f);
    printf("... %s (extra APs)\n", filename.c_str());

    fclose(f);
  }

  // save media
  for (const auto& it : picts) {
    string filename = string_printf("%s/media/picture_%d.bmp", out_dir.c_str(), it.first);
    it.second.Save(filename.c_str(), Image::WindowsBitmap);
    printf("... %s\n", filename.c_str());
  }
  for (const auto& it : cicns) {
    string filename = string_printf("%s/media/icon_%d.bmp", out_dir.c_str(), it.first);
    it.second.Save(filename.c_str(), Image::WindowsBitmap);
    printf("... %s\n", filename.c_str());
  }
  for (const auto& it : snds) {
    string filename = string_printf("%s/media/snd_%d.wav", out_dir.c_str(), it.first);
    FILE* f = fopen(filename.c_str(), "wb");
    fwrite(it.second.data(), it.second.size(), 1, f);
    fclose(f);
    printf("... %s\n", filename.c_str());
  }
  for (const auto& it : texts) {
    string filename = string_printf("%s/media/text_%d.txt", out_dir.c_str(), it.first);
    FILE* f = fopen(filename.c_str(), "wb");
    fwrite(it.second.data(), it.second.size(), 1, f);
    fclose(f);
    printf("... %s\n", filename.c_str());
  }

  // generate custom tileset legends
  for (auto it : custom_tilesets) {
    //try {
      string filename = string_printf("%s/tileset_custom_%d_legend.bmp",
          out_dir.c_str(), it.first);
      Image legend = generate_tileset_definition_legend(it.second,
          string_printf("custom_%d", it.first), scenario_resources_name);
      legend.Save(filename.c_str(), Image::WindowsBitmap);
      printf("... %s\n", filename.c_str());
    //} catch (const runtime_error& e) {
    //  printf("warning: can\'t generate legend for custom tileset %d (%s)\n",
    //      it.first, e.what());
    //}
  }

  // generate dungeon maps
  for (size_t x = 0; x < dungeon_maps.size(); x++) {
    string filename = string_printf("%s/dungeon_%d.bmp", out_dir.c_str(), x);
    Image map = generate_dungeon_map(dungeon_maps[x], dungeon_metadata[x],
        dungeon_aps[x], x);
    map.Save(filename.c_str(), Image::WindowsBitmap);
    printf("... %s\n", filename.c_str());

    string filename_2x = string_printf("%s/dungeon_%d_2x.bmp", out_dir.c_str(), x);
    Image map_2x = generate_dungeon_map_2x(dungeon_maps[x], dungeon_metadata[x],
        dungeon_aps[x], x);
    map_2x.Save(filename_2x.c_str(), Image::WindowsBitmap);
    printf("... %s\n", filename_2x.c_str());
  }

  // generate land maps
  unordered_map<int16_t, string> level_id_to_filename;
  for (size_t x = 0; x < land_maps.size(); x++) {

    level_neighbors n;
    try {
      n = get_level_neighbors(layout, x);
    } catch (const runtime_error& e) {
      printf("warning: can\'t get neighbors for level! (%s)\n", e.what());
    }

    int16_t start_x = -1, start_y = -1;
    if (x == (size_t)scen_metadata.start_level) {
      start_x = scen_metadata.start_x;
      start_y = scen_metadata.start_y;
    }

    try {
      string filename = string_printf("%s/land_%d.bmp", out_dir.c_str(), x);
      Image map = generate_land_map(land_maps[x], land_metadata[x], land_aps[x],
          x, n, start_x, start_y, scenario_resources_name);
      map.Save(filename.c_str(), Image::WindowsBitmap);
      level_id_to_filename[x] = filename;
      printf("... %s\n", filename.c_str());

    } catch (const out_of_range& e) {
      printf("error: can\'t render with selected tileset (%s)\n", e.what());
    } catch (const runtime_error& e) {
      printf("error: can\'t render with selected tileset (%s)\n", e.what());
    }
  }

  // generate connected land map
  for (auto layout_component : get_connected_components(layout)) {
    try {
      string filename = string_printf("%s/land_connected", out_dir.c_str());
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 16; x++)
          if (layout_component.layout[y][x] != -1)
            filename += string_printf("_%d", layout_component.layout[y][x]);
      filename += ".bmp";

      Image connected_map = generate_layout_map(layout_component,
          level_id_to_filename);
      connected_map.Save(filename.c_str(), Image::WindowsBitmap);
      printf("... %s\n", filename.c_str());
    } catch (const runtime_error& e) {
      printf("warning: can\'t generate connected land map: %s\n", e.what());
    }
  }

  return 0;
}
