#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <memory>
#include <vector>

#include "ResourceFile.hh"

namespace ResourceDASM {

using namespace phosg;

const ResourceFile::TemplateEntryList& get_system_template(uint32_t type);

} // namespace ResourceDASM
