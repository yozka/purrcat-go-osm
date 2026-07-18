#pragma once

#include "RegionId.h"
#include "RegionService.h"

#include <cstdint>
#include <string>
#include <vector>

namespace purrcat::osm {

struct LonLat {
	double lon = 0;
	double lat = 0;
};

enum class FeatureKind {
	Road,
	Building,
	Water,     // filled areas: natural=water, riverbank, …
	Waterway,  // stroked lines: river, stream, canal, …
	Green,
	Coast
};

struct Feature {
	FeatureKind kind = FeatureKind::Road;
	std::vector<LonLat> ring; // open or closed polyline / polygon ring
	bool closed = false;
};

class TileRenderer {
public:
	explicit TileRenderer(RegionService &regions);

	/// Returns PNG bytes. Empty on failure.
	std::vector<std::uint8_t> renderOrLoad(int z, int x, int y);

private:
	std::vector<Feature> loadFeaturesForTile(int z, int x, int y, BBox &tileBBox) const;
	std::vector<std::uint8_t> rasterize(int z, int x, int y, const std::vector<Feature> &features) const;

	RegionService &m_regions;
};

} // namespace purrcat::osm
