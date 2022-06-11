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
  be_uint16_t sound_id;
  be_uint16_t time_per_move;
  be_uint16_t solid_type; // 0 = not solid, 1 = solid to 1-box chars, 2 = solid
  be_uint16_t is_shore;
  be_uint16_t is_need_boat; // 1 = is boat, 2 = need boat
  be_uint16_t is_path;
  be_uint16_t blocks_los;
  be_uint16_t need_fly_float;
  be_uint16_t special_type; // 1 = trees, 2 = desert, 3 = shrooms, 4 = swamp, 5 = snow
  int16_t unknown5;
  be_int16_t battle_expansion[9];
  int16_t unknown6;
} __attribute__((packed));

struct TileSetDefinition {
  TileDefinition tiles[201];
  be_uint16_t base_tile_id;
} __attribute__((packed));

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
