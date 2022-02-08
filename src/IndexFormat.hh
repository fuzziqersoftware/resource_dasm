#pragma once

enum class IndexFormat {
  None = 0, // For ResourceFiles constructed in memory
  ResourceFork,
  Mohawk,
  HIRF,
  DCData,
};
