#pragma once

#include <string>
#include <utility>
#include <phosg/Strings.hh>

#include "../ResourceFile.hh"



ResourceFile parse_resource_fork(const std::string& data);
ResourceFile parse_resource_fork(StringReader& data);
std::string serialize_resource_fork(const ResourceFile& rf);

ResourceFile parse_mohawk(const std::string& data);

ResourceFile parse_hirf(const std::string& data);

ResourceFile parse_dc_data(const std::string& data);

ResourceFile parse_cbag(const std::string& data);

std::pair<StringReader, ResourceFile> parse_macbinary(const std::string& data);
ResourceFile parse_macbinary_resource_fork(const std::string& data);
