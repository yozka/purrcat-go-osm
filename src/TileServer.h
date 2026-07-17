#pragma once

#include "RegionService.h"
#include "TileRenderer.h"

#include <filesystem>
#include <memory>
#include <string>

namespace purrcat::osm {

class TileServer {
public:
	TileServer(std::filesystem::path dataRoot, int port);

	void run();

private:
	int m_port = 8080;
	std::unique_ptr<RegionService> m_regions;
	std::unique_ptr<TileRenderer> m_renderer;
};

} // namespace purrcat::osm
