#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <unordered_map>
#include <vector>

#include "realmz_lib.hh"

using namespace std;

int main(int argc, char* argv[]) {

  if (argc < 3) {
    printf("usage: %s scenario_dir out_dir\n", argv[0]);
    return 1;
  }

  string scenario_dir = argv[1];
  string out_dir = argv[2];

  string dungeon_map_index_name = first_file_that_exists(
      (scenario_dir + "/data_dl").c_str(),
      (scenario_dir + "/Data DL").c_str(),
      (scenario_dir + "/DATA DL").c_str(), NULL);
  string land_map_index_name = first_file_that_exists(
      (scenario_dir + "/data_ld").c_str(),
      (scenario_dir + "/Data LD").c_str(),
      (scenario_dir + "/DATA LD").c_str(), NULL);
  string string_index_name = first_file_that_exists(
      (scenario_dir + "/data_sd2").c_str(),
      (scenario_dir + "/Data SD2").c_str(),
      (scenario_dir + "/DATA SD2").c_str(), NULL);
  string ecodes_index_name = first_file_that_exists(
      (scenario_dir + "/data_edcd").c_str(),
      (scenario_dir + "/Data EDCD").c_str(),
      (scenario_dir + "/DATA EDCD").c_str(), NULL);
  string land_ap_index_name = first_file_that_exists(
      (scenario_dir + "/data_dd").c_str(),
      (scenario_dir + "/Data DD").c_str(),
      (scenario_dir + "/DATA DD").c_str(), NULL);
  string dungeon_ap_index_name = first_file_that_exists(
      (scenario_dir + "/data_ddd").c_str(),
      (scenario_dir + "/Data DDD").c_str(),
      (scenario_dir + "/DATA DDD").c_str(), NULL);
  string extra_ap_index_name = first_file_that_exists(
      (scenario_dir + "/data_ed3").c_str(),
      (scenario_dir + "/Data ED3").c_str(),
      (scenario_dir + "/DATA ED3").c_str(), NULL);
  string land_metadata_index_name = first_file_that_exists(
      (scenario_dir + "/data_rd").c_str(),
      (scenario_dir + "/Data RD").c_str(),
      (scenario_dir + "/DATA RD").c_str(), NULL);
  string dungeon_metadata_index_name = first_file_that_exists(
      (scenario_dir + "/data_rdd").c_str(),
      (scenario_dir + "/Data RDD").c_str(),
      (scenario_dir + "/DATA RDD").c_str(), NULL);

  vector<map_data> dungeon_maps = load_dungeon_map_index(dungeon_map_index_name);
  vector<map_data> land_maps = load_land_map_index(land_map_index_name);
  vector<string> strings = load_string_index(string_index_name);
  vector<ecodes> ecodes = load_ecodes_index(ecodes_index_name);
  vector<vector<ap_info>> land_aps = load_ap_index(land_ap_index_name);
  vector<vector<ap_info>> dungeon_aps = load_ap_index(dungeon_ap_index_name);
  vector<ap_info> xaps = load_xap_index(extra_ap_index_name);
  vector<map_metadata> land_metadata = load_map_metadata_index(land_metadata_index_name);
  vector<map_metadata> dungeon_metadata = load_map_metadata_index(dungeon_metadata_index_name);

  {
    string filename = string_printf("%s/dungeon-ap.txt", out_dir.c_str());
    FILE* f = fopen_or_throw(filename.c_str(), "wt");
    string data = disassemble_all_aps(dungeon_aps, ecodes, &strings);
    fwrite(data.data(), data.size(), 1, f);
    fclose(f);
    printf("... %s\n", filename.c_str());
  }

  {
    string filename = string_printf("%s/land-ap.txt", out_dir.c_str());
    FILE* f = fopen_or_throw(filename.c_str(), "wt");
    string data = disassemble_all_aps(land_aps, ecodes, &strings);
    fwrite(data.data(), data.size(), 1, f);
    fclose(f);
    printf("... %s\n", filename.c_str());
  }

  {
    string filename = string_printf("%s/extra-ap.txt", out_dir.c_str());
    FILE* f = fopen_or_throw(filename.c_str(), "wt");
    string data = disassemble_level_aps(0, xaps, ecodes, &strings);
    fwrite(data.data(), data.size(), 1, f);
    fclose(f);
    printf("... %s\n", filename.c_str());
  }

  for (size_t x = 0; x < dungeon_maps.size(); x++) {
    string filename = string_printf("%s/dungeon-%d.bmp", out_dir.c_str(), x);
    Image map = generate_dungeon_map(dungeon_maps[x], dungeon_metadata[x],
        dungeon_aps[x], x);
    map.Save(filename.c_str(), Image::WindowsBitmap);
    printf("... %s\n", filename.c_str());
  }

  for (size_t x = 0; x < land_maps.size(); x++) {
    try {
      string filename = string_printf("%s/land-%d.bmp", out_dir.c_str(), x);
      Image map = generate_land_map(land_maps[x], land_metadata[x], land_aps[x], x);
      map.Save(filename.c_str(), Image::WindowsBitmap);
      printf("... %s\n", filename.c_str());

    } catch (const out_of_range& e) {
      printf("... warning: can\'t render with selected tileset! rendering all known tilesets\n");
      for (const auto it : all_land_types()) {
        string filename = string_printf("%s/land-%d-%s.bmp", out_dir.c_str(), x, it.c_str());
        land_metadata[x].land_type = it;
        Image map = generate_land_map(land_maps[x], land_metadata[x], land_aps[x], x);
        map.Save(filename.c_str(), Image::WindowsBitmap);
        printf("... %s\n", filename.c_str());
      }
    }
  }

  return 0;
}
