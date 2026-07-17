#include "TileRenderer.h"

#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <numbers>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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
	if (tags.contains("natural") && tags["natural"] == "water")
		return FeatureKind::Water;
	if (tags.contains("waterway"))
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

void putPixel(std::vector<std::uint8_t> &img, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
{
	if (x < 0 || y < 0 || x >= kTileSize || y >= kTileSize)
		return;
	const size_t i = static_cast<size_t>((y * kTileSize + x) * 4);
	const float srcA = a / 255.f;
	const float dstA = img[i + 3] / 255.f;
	const float outA = srcA + dstA * (1.f - srcA);
	if (outA <= 0.f)
		return;
	img[i + 0] = static_cast<std::uint8_t>((r * srcA + img[i + 0] * dstA * (1.f - srcA)) / outA);
	img[i + 1] = static_cast<std::uint8_t>((g * srcA + img[i + 1] * dstA * (1.f - srcA)) / outA);
	img[i + 2] = static_cast<std::uint8_t>((b * srcA + img[i + 2] * dstA * (1.f - srcA)) / outA);
	img[i + 3] = static_cast<std::uint8_t>(outA * 255.f);
}

void drawLine(std::vector<std::uint8_t> &img, int x0, int y0, int x1, int y1,
	std::uint8_t r, std::uint8_t g, std::uint8_t b, int thickness)
{
	const int dx = std::abs(x1 - x0);
	const int dy = std::abs(y1 - y0);
	const int sx = x0 < x1 ? 1 : -1;
	const int sy = y0 < y1 ? 1 : -1;
	int err = dx - dy;
	for (;;) {
		for (int oy = -thickness; oy <= thickness; ++oy) {
			for (int ox = -thickness; ox <= thickness; ++ox) {
				if (ox * ox + oy * oy <= thickness * thickness)
					putPixel(img, x0 + ox, y0 + oy, r, g, b);
			}
		}
		if (x0 == x1 && y0 == y1)
			break;
		const int e2 = 2 * err;
		if (e2 > -dy) {
			err -= dy;
			x0 += sx;
		}
		if (e2 < dx) {
			err += dx;
			y0 += sy;
		}
	}
}

void fillPolygon(std::vector<std::uint8_t> &img, const std::vector<std::pair<double, double>> &pts,
	std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
	if (pts.size() < 3)
		return;
	double minY = pts[0].second, maxY = pts[0].second;
	for (const auto &p : pts) {
		minY = std::min(minY, p.second);
		maxY = std::max(maxY, p.second);
	}
	const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
	const int y1 = std::min(kTileSize - 1, static_cast<int>(std::ceil(maxY)));
	for (int y = y0; y <= y1; ++y) {
		std::vector<double> nodes;
		for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
			const double yi = pts[i].second;
			const double yj = pts[j].second;
			const double xi = pts[i].first;
			const double xj = pts[j].first;
			if ((yi < y && yj >= y) || (yj < y && yi >= y)) {
				nodes.push_back(xi + (y - yi) / (yj - yi + 1e-12) * (xj - xi));
			}
		}
		std::sort(nodes.begin(), nodes.end());
		for (size_t i = 0; i + 1 < nodes.size(); i += 2) {
			const int xStart = std::max(0, static_cast<int>(std::floor(nodes[i])));
			const int xEnd = std::min(kTileSize - 1, static_cast<int>(std::ceil(nodes[i + 1])));
			for (int x = xStart; x <= xEnd; ++x)
				putPixel(img, x, y, r, g, b, a);
		}
	}
}

std::vector<std::uint8_t> encodePng(const std::vector<std::uint8_t> &rgba)
{
	std::vector<std::uint8_t> png;
	stbi_write_png_to_func(
		[](void *context, void *data, int size) {
			auto *out = static_cast<std::vector<std::uint8_t> *>(context);
			const auto *bytes = static_cast<const std::uint8_t *>(data);
			out->insert(out->end(), bytes, bytes + size);
		},
		&png, kTileSize, kTileSize, 4, rgba.data(), kTileSize * 4);
	return png;
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

	for (const auto &el : (*osm)["elements"]) {
		if (!el.contains("type") || el["type"] != "way")
			continue;
		if (!el.contains("geometry") || !el["geometry"].is_array())
			continue;

		Feature f;
		f.kind = el.contains("tags") ? classifyTags(el["tags"]) : FeatureKind::Road;
		for (const auto &g : el["geometry"]) {
			if (!g.contains("lat") || !g.contains("lon"))
				continue;
			f.ring.push_back({g["lon"].get<double>(), g["lat"].get<double>()});
		}
		if (f.ring.size() < 2)
			continue;

		f.closed = (f.kind == FeatureKind::Building || f.kind == FeatureKind::Water || f.kind == FeatureKind::Green);
		if (f.closed && f.ring.front().lat != f.ring.back().lat) {
			// keep as-is; fill handles open rings via closing edge in scanline if first!=last
		}

		if (!intersects(ringBBox(f.ring), tileBBox))
			continue;
		features.push_back(std::move(f));
	}
	return features;
}

std::vector<std::uint8_t> TileRenderer::rasterize(int z, int x, int y, const std::vector<Feature> &features) const
{
	std::vector<std::uint8_t> img(static_cast<size_t>(kTileSize * kTileSize * 4), 255);
	// Cat-style warm ground
	for (int i = 0; i < kTileSize * kTileSize; ++i) {
		img[static_cast<size_t>(i) * 4 + 0] = 246;
		img[static_cast<size_t>(i) * 4 + 1] = 236;
		img[static_cast<size_t>(i) * 4 + 2] = 220;
		img[static_cast<size_t>(i) * 4 + 3] = 255;
	}

	auto project = [&](const LonLat &p) {
		double px, py;
		lonLatToPixel(p.lon, p.lat, z, x, y, px, py);
		return std::pair<double, double>{px, py};
	};

	// Green / water / buildings first
	for (const auto &f : features) {
		if (f.kind != FeatureKind::Green && f.kind != FeatureKind::Water && f.kind != FeatureKind::Building)
			continue;
		std::vector<std::pair<double, double>> pts;
		pts.reserve(f.ring.size());
		for (const auto &p : f.ring)
			pts.push_back(project(p));
		if (f.kind == FeatureKind::Green)
			fillPolygon(img, pts, 120, 180, 110, 220);
		else if (f.kind == FeatureKind::Water)
			fillPolygon(img, pts, 90, 160, 210, 230);
		else
			fillPolygon(img, pts, 220, 150, 120, 230);
	}

	// Roads / coast on top
	for (const auto &f : features) {
		if (f.kind != FeatureKind::Road && f.kind != FeatureKind::Coast)
			continue;
		const int thickness = (f.kind == FeatureKind::Coast) ? 2 : (z >= 18 ? 2 : 1);
		std::uint8_t r = 90, g = 70, b = 55;
		if (f.kind == FeatureKind::Coast) {
			r = 40;
			g = 90;
			b = 140;
		}
		for (size_t i = 1; i < f.ring.size(); ++i) {
			const auto a = project(f.ring[i - 1]);
			const auto c = project(f.ring[i]);
			drawLine(img, static_cast<int>(a.first), static_cast<int>(a.second),
				static_cast<int>(c.first), static_cast<int>(c.second), r, g, b, thickness);
		}
	}

	return encodePng(img);
}

} // namespace purrcat::osm
