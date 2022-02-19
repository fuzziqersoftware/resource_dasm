#pragma once

#include <string>

#include "../ResourceFile.hh"



ResourceFile parse_resource_fork(const std::string& data);
std::string serialize_resource_fork(const ResourceFile& rf);
