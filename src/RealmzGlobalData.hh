#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Image.hh>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

struct ItemInfo {
  std::string unidentified_name;
  std::string name;
  std::string description;
};

struct RealmzGlobalData {
  explicit RealmzGlobalData(const std::string& dir);
  ~RealmzGlobalData() = default;

  void load_default_tilesets();
  static std::unordered_map<uint16_t, ItemInfo> parse_item_info(ResourceFile& rsf);
  static std::unordered_map<uint16_t, std::string> parse_spell_names(ResourceFile& rsf);

  const ItemInfo& info_for_item(uint16_t id) const;
  const std::string& name_for_spell(uint16_t id) const;

  std::string dir;
  ResourceFile global_rsf;
  ResourceFile portraits_rsf;
  ResourceFile names_rsf;
  ResourceFile data_id_rsf;

  std::vector<std::string> race_names;
  std::vector<std::string> caste_names;
  std::unordered_map<uint16_t, ItemInfo> item_info;
  std::unordered_map<uint16_t, std::string> spell_names;

  std::unordered_map<std::string, TileSetDefinition> land_type_to_tileset_definition;
};

std::string first_file_that_exists(const std::vector<std::string>& names);
int16_t resource_id_for_land_type(const std::string& land_type);
Image generate_tileset_definition_legend(
    const TileSetDefinition& ts, const Image& positive_pattern);
