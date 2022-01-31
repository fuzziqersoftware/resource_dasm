#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Image.hh>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "ResourceFile.hh"



std::unordered_map<int16_t, Image> rsf_picts(ResourceFile& rf);
std::unordered_map<int16_t, ResourceFile::DecodedColorIconResource> rsf_cicns(ResourceFile& rf);
std::unordered_map<int16_t, std::string> rsf_snds(ResourceFile& rf);
std::unordered_map<int16_t, std::pair<std::string, bool>> rsf_texts(ResourceFile& rf);



struct TileDefinition {
  uint16_t sound_id;
  uint16_t time_per_move;
  uint16_t solid_type; // 0 = not solid, 1 = solid to 1-box chars, 2 = solid
  uint16_t is_shore;
  uint16_t is_need_boat; // 1 = is boat, 2 = need boat
  uint16_t is_path;
  uint16_t blocks_los;
  uint16_t need_fly_float;
  uint16_t special_type; // 1 = trees, 2 = desert, 3 = shrooms, 4 = swamp, 5 = snow
  int16_t unknown5;
  int16_t battle_expansion[9];
  int16_t unknown6;
  void byteswap();
};

struct TileSetDefinition {
  TileDefinition tiles[201];
  uint16_t base_tile_id;
  void byteswap();
};

TileSetDefinition load_tileset_definition(const std::string& filename);

struct RealmzGlobalData {
  explicit RealmzGlobalData(const std::string& dir);
  ~RealmzGlobalData() = default;

  void load_default_tilesets();

  std::string dir;
  ResourceFile global_rsf;
  ResourceFile portraits_rsf;
  std::unordered_map<std::string, TileSetDefinition> land_type_to_tileset_definition;
};

std::string first_file_that_exists(const std::vector<std::string>& names);
int16_t resource_id_for_land_type(const std::string& land_type);
Image generate_tileset_definition_legend(
    const TileSetDefinition& ts, const Image& positive_pattern);
