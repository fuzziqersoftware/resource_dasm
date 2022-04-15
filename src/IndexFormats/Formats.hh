#pragma once

#include <string>

#include "../ResourceFile.hh"



ResourceFile parse_resource_fork(const std::string& data);
std::string serialize_resource_fork(const ResourceFile& rf);

ResourceFile parse_mohawk(const std::string& data);

ResourceFile parse_hirf(const std::string& data);

ResourceFile parse_dc_data(const std::string& data);
