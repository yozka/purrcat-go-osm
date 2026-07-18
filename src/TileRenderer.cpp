#include "TileRenderer.h"

#include "json.hpp"

#include <blend2d/blend2d.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <numbers>
#include <utility>
#include <vector>

namespace purrcat::osm {
namespace {

constexpr int kTileSize = 256;

inline bool isSupportedZoom(int z)
{
	return z == 16 || z == 18;
}

BBox tileLatLonBBox(int z, int x, int y)
{
	const double n = std::pow(2.0, z);
	const double lonMin = x / n * 360.0 - 180.0;
	const double lonMax = (x + 1) / n * 360.0 - 180.0;
	const auto latRad = [](double ty, double nn) {
		const double m = std::numbers::pi - 2.0 * std::numbers::pi * ty / nn;
		return std::atan(std::sinh(m)) * 180.0 / std::numbers::pi;
	};
	const double latMax = latRad(y, n);
	const double latMin = latRad(y + 1, n);
	return {latMin, lonMin, latMax, lonMax};
}

void lonLatToPixel(double lon, double lat, int z, int tileX, int tileY, double &px, double &py)
{
	const double n = std::pow(2.0, z);
	const double x = (lon + 180.0) / 360.0 * n;
	const double latRad = lat * std::numbers::pi / 180.0;
	const double y = (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / std::numbers::pi) / 2.0 * n;
	px = (x - tileX) * kTileSize;
	py = (y - tileY) * kTileSize;
}

FeatureKind classifyTags(const nlohmann::json &tags)
{
	if (!tags.is_object())
		return FeatureKind::Road;
	if (tags.contains("building"))
		return FeatureKind::Building;
	if (tags.contains("natural") && tags["natural"] == "coastline")
		return FeatureKind::Coast;

	// Linear channels are stroked. Area waterways (riverbank, …) are filled.
	if (tags.contains("waterway")) {
		const std::string v = tags["waterway"].is_string()
			? tags["waterway"].get<std::string>()
			: std::string{};
		const bool area = (tags.contains("area") && tags["area"] == "yes")
			|| v == "riverbank" || v == "dock" || v == "boatyard";
		if (area)
			return FeatureKind::Water;
		return FeatureKind::Waterway;
	}

	// All natural=water areas (lake, pond, river polygon, …) are filled.
	if (tags.contains("natural") && tags["natural"] == "water")
		return FeatureKind::Water;

	if (tags.contains("landuse")) {
		const std::string v = tags["landuse"].get<std::string>();
		if (v == "grass" || v == "forest" || v == "meadow" || v == "recreation_ground" || v == "village_green")
			return FeatureKind::Green;
	}
	if (tags.contains("leisure")) {
		const std::string v = tags["leisure"].get<std::string>();
		if (v == "park" || v == "garden" || v == "nature_reserve")
			return FeatureKind::Green;
	}
	if (tags.contains("natural")) {
		const std::string v = tags["natural"].get<std::string>();
		if (v == "wood" || v == "scrub")
			return FeatureKind::Green;
	}
	if (tags.contains("highway"))
		return FeatureKind::Road;
	return FeatureKind::Road;
}

bool samePoint(const LonLat &a, const LonLat &b)
{
	constexpr double kEps = 1e-7;
	return std::abs(a.lon - b.lon) < kEps && std::abs(a.lat - b.lat) < kEps;
}

bool isGeometricallyClosed(const std::vector<LonLat> &ring)
{
	if (ring.size() < 3)
		return false;
	return samePoint(ring.front(), ring.back());
}

std::vector<LonLat> parseGeometryRing(const nlohmann::json &geom)
{
	std::vector<LonLat> ring;
	if (!geom.is_array())
		return ring;
	ring.reserve(geom.size());
	for (const auto &g : geom) {
		if (!g.contains("lat") || !g.contains("lon"))
			continue;
		ring.push_back({g["lon"].get<double>(), g["lat"].get<double>()});
	}
	return ring;
}

/// Merge open multipolygon outer pieces into closed rings by matching endpoints.
std::vector<std::vector<LonLat>> stitchRings(std::vector<std::vector<LonLat>> parts)
{
	std::vector<std::vector<LonLat>> closed;
	std::vector<std::vector<LonLat>> open;

	for (auto &part : parts) {
		if (part.size() < 2)
			continue;
		if (isGeometricallyClosed(part)) {
			closed.push_back(std::move(part));
		} else {
			open.push_back(std::move(part));
		}
	}

	bool merged = true;
	while (merged && !open.empty()) {
		merged = false;
		for (size_t i = 0; i < open.size() && !merged; ++i) {
			for (size_t j = i + 1; j < open.size(); ++j) {
				auto &a = open[i];
				auto &b = open[j];
				if (samePoint(a.back(), b.front())) {
					a.insert(a.end(), b.begin() + 1, b.end());
					open.erase(open.begin() + static_cast<std::ptrdiff_t>(j));
					merged = true;
					break;
				}
				if (samePoint(a.back(), b.back())) {
					a.insert(a.end(), b.rbegin() + 1, b.rend());
					open.erase(open.begin() + static_cast<std::ptrdiff_t>(j));
					merged = true;
					break;
				}
				if (samePoint(a.front(), b.back())) {
					b.insert(b.end(), a.begin() + 1, a.end());
					open[i] = std::move(b);
					open.erase(open.begin() + static_cast<std::ptrdiff_t>(j));
					merged = true;
					break;
				}
				if (samePoint(a.front(), b.front())) {
					std::vector<LonLat> rev(a.rbegin(), a.rend());
					rev.insert(rev.end(), b.begin() + 1, b.end());
					open[i] = std::move(rev);
					open.erase(open.begin() + static_cast<std::ptrdiff_t>(j));
					merged = true;
					break;
				}
			}
		}
		if (merged && isGeometricallyClosed(open[0])) {
			// fall through — check all after loop iteration
		}
		for (auto it = open.begin(); it != open.end();) {
			if (isGeometricallyClosed(*it)) {
				closed.push_back(std::move(*it));
				it = open.erase(it);
			} else {
				++it;
			}
		}
	}

	return closed;
}

bool intersects(const BBox &a, const BBox &b)
{
	return !(a.maxLon < b.minLon || a.minLon > b.maxLon || a.maxLat < b.minLat || a.minLat > b.maxLat);
}

BBox ringBBox(const std::vector<LonLat> &ring)
{
	BBox b{90, 180, -90, -180};
	for (const auto &p : ring) {
		b.minLat = std::min(b.minLat, p.lat);
		b.maxLat = std::max(b.maxLat, p.lat);
		b.minLon = std::min(b.minLon, p.lon);
		b.maxLon = std::max(b.maxLon, p.lon);
	}
	return b;
}

BLPath ringToPath(const std::vector<LonLat> &ring, int z, int tileX, int tileY, bool close)
{
	BLPath path;
	if (ring.empty())
		return path;

	double px = 0, py = 0;
	lonLatToPixel(ring[0].lon, ring[0].lat, z, tileX, tileY, px, py);
	path.move_to(px, py);
	for (size_t i = 1; i < ring.size(); ++i) {
		lonLatToPixel(ring[i].lon, ring[i].lat, z, tileX, tileY, px, py);
		path.line_to(px, py);
	}
	if (close)
		path.close();
	return path;
}

std::vector<std::uint8_t> encodePng(const BLImage &img)
{
	BLImageCodec codec;
	if (codec.find_by_name("PNG") != BL_SUCCESS)
		return {};

	BLArray<std::uint8_t> encoded;
	if (img.write_to_data(encoded, codec) != BL_SUCCESS)
		return {};

	return {encoded.data(), encoded.data() + encoded.size()};
}

} // namespace

TileRenderer::TileRenderer(RegionService &regions)
	: m_regions(regions)
{
}

std::vector<std::uint8_t> TileRenderer::renderOrLoad(int z, int x, int y)
{
	if (!isSupportedZoom(z))
		return {};

	BBox tileBBox = tileLatLonBBox(z, x, y);
	const double midLat = (tileBBox.minLat + tileBBox.maxLat) * 0.5;
	const double midLon = (tileBBox.minLon + tileBBox.maxLon) * 0.5;
	const RegionId region = RegionId::fromLatLon(midLat, midLon);

	// Never cache blank tiles when region pack is not ready (was the main demo bug).
	if (!m_regions.hasRegionPack(region))
		return {};

	const auto path = m_regions.tilePath(z, x, y);
	if (std::filesystem::exists(path)) {
		std::ifstream in(path, std::ios::binary);
		return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}

	const auto features = loadFeaturesForTile(z, x, y, tileBBox);
	auto png = rasterize(z, x, y, features);
	if (png.empty())
		return {};

	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
	return png;
}

std::vector<Feature> TileRenderer::loadFeaturesForTile(int z, int x, int y, BBox &tileBBox) const
{
	tileBBox = tileLatLonBBox(z, x, y);
	const double midLat = (tileBBox.minLat + tileBBox.maxLat) * 0.5;
	const double midLon = (tileBBox.minLon + tileBBox.maxLon) * 0.5;
	const RegionId region = RegionId::fromLatLon(midLat, midLon);
	if (!m_regions.hasRegionPack(region))
		return {};

	const std::string packText = m_regions.loadPackJson(region);
	if (packText.empty())
		return {};

	nlohmann::json pack = nlohmann::json::parse(packText, nullptr, false);
	if (pack.is_discarded())
		return {};

	const nlohmann::json *osm = &pack;
	if (pack.contains("osm"))
		osm = &pack["osm"];
	if (!osm->contains("elements") || !(*osm)["elements"].is_array())
		return {};

	std::vector<Feature> features;
	features.reserve((*osm)["elements"].size());

	auto pushAreaOrLine = [&](Feature f) {
		if (f.ring.size() < 2)
			return;
		const bool areaKind = (f.kind == FeatureKind::Building || f.kind == FeatureKind::Water || f.kind == FeatureKind::Green);
		f.closed = areaKind && isGeometricallyClosed(f.ring);
		// Open waterway/coast/road stay as lines. Open "area" tags without closed geom → skip fill.
		if (areaKind && !f.closed)
			return;
		if (!intersects(ringBBox(f.ring), tileBBox))
			return;
		features.push_back(std::move(f));
	};

	for (const auto &el : (*osm)["elements"]) {
		if (!el.contains("type"))
			continue;

		if (el["type"] == "way") {
			if (!el.contains("geometry") || !el["geometry"].is_array())
				continue;
			Feature f;
			f.kind = el.contains("tags") ? classifyTags(el["tags"]) : FeatureKind::Road;
			f.ring = parseGeometryRing(el["geometry"]);
			pushAreaOrLine(std::move(f));
			continue;
		}

		// Multipolygon water (Danube etc.): assemble outer member ways into closed rings.
		if (el["type"] == "relation") {
			if (!el.contains("tags") || !el.contains("members") || !el["members"].is_array())
				continue;
			const FeatureKind kind = classifyTags(el["tags"]);
			if (kind != FeatureKind::Water && kind != FeatureKind::Green)
				continue;

			std::vector<std::vector<LonLat>> parts;
			for (const auto &m : el["members"]) {
				if (!m.contains("type") || m["type"] != "way")
					continue;
				const std::string role = m.contains("role") && m["role"].is_string()
					? m["role"].get<std::string>()
					: std::string{};
				if (role == "inner")
					continue; // holes ignored for now
				if (!m.contains("geometry"))
					continue;
				auto ring = parseGeometryRing(m["geometry"]);
				if (ring.size() >= 2)
					parts.push_back(std::move(ring));
			}

			for (auto &ring : stitchRings(std::move(parts))) {
				Feature f;
				f.kind = kind;
				f.ring = std::move(ring);
				f.closed = true;
				if (!intersects(ringBBox(f.ring), tileBBox))
					continue;
				features.push_back(std::move(f));
			}
		}
	}
	return features;
}

std::vector<std::uint8_t> TileRenderer::rasterize(int z, int x, int y, const std::vector<Feature> &features) const
{
	BLImage img(kTileSize, kTileSize, BL_FORMAT_PRGB32);
	BLContext ctx(img);
	if (!ctx.is_valid())
		return {};

	ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
	ctx.set_fill_rule(BL_FILL_RULE_NON_ZERO);
	ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
	ctx.set_stroke_join(BL_STROKE_JOIN_ROUND);

	// Cat-style warm ground
	ctx.fill_all(BLRgba32(246, 236, 220, 255));

	// Green / water / buildings first (closed rings only)
	for (const auto &f : features) {
		if (!f.closed)
			continue;
		if (f.kind != FeatureKind::Green && f.kind != FeatureKind::Water && f.kind != FeatureKind::Building)
			continue;
		if (f.ring.size() < 3)
			continue;

		BLRgba32 color(220, 150, 120, 230);
		if (f.kind == FeatureKind::Green)
			color = BLRgba32(120, 180, 110, 220);
		else if (f.kind == FeatureKind::Water)
			color = BLRgba32(90, 160, 210, 230);

		const BLPath path = ringToPath(f.ring, z, x, y, true);
		ctx.fill_path(path, color);
	}

	// Roads / coast / waterways on top
	for (const auto &f : features) {
		if (f.kind != FeatureKind::Road && f.kind != FeatureKind::Coast && f.kind != FeatureKind::Waterway)
			continue;
		if (f.ring.size() < 2)
			continue;

		double thickness = (z >= 18 ? 2.0 : 1.0);
		BLRgba32 color(90, 70, 55, 255);
		if (f.kind == FeatureKind::Coast) {
			thickness = 2.0;
			color = BLRgba32(40, 90, 140, 255);
		} else if (f.kind == FeatureKind::Waterway) {
			thickness = (z >= 18 ? 2.5 : 1.5);
			color = BLRgba32(90, 160, 210, 255);
		}

		const BLPath path = ringToPath(f.ring, z, x, y, false);
		ctx.set_stroke_width(thickness * 2.0 + 1.0);
		ctx.stroke_path(path, color);
	}

	ctx.end();
	return encodePng(img);
}

} // namespace purrcat::osm
