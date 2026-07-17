#pragma once

#include <cmath>
#include <string>

namespace purrcat::osm {

// ~20 km at mid-latitudes (1° lat ≈ 111 km)
inline constexpr double kRegionStepDeg = 20.0 / 111.0;

struct BBox {
	double minLat = 0;
	double minLon = 0;
	double maxLat = 0;
	double maxLon = 0;
};

struct RegionId {
	int x = 0;
	int y = 0;

	bool operator==(const RegionId &o) const { return x == o.x && y == o.y; }

	std::string key() const { return std::to_string(x) + "_" + std::to_string(y); }

	BBox bbox() const
	{
		BBox b;
		b.minLon = x * kRegionStepDeg;
		b.minLat = y * kRegionStepDeg;
		b.maxLon = (x + 1) * kRegionStepDeg;
		b.maxLat = (y + 1) * kRegionStepDeg;
		return b;
	}

	static RegionId fromLatLon(double lat, double lon)
	{
		RegionId id;
		id.x = static_cast<int>(std::floor(lon / kRegionStepDeg));
		id.y = static_cast<int>(std::floor(lat / kRegionStepDeg));
		return id;
	}
};

} // namespace purrcat::osm
