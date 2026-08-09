#pragma once
#include "xtm/Terrain.hpp"
#include <string>

namespace xtm::io {

void write_gdal(const std::string& path, const TerrainView& view);

}
