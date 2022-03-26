#pragma once

enum class IndexFormat {
  NONE = 0, // For ResourceFiles constructed in memory
  RESOURCE_FORK,
  MOHAWK,
  HIRF,
  DC_DATA,
};
