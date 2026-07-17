#include "TileServer.h"

#include "Http.h"
#include "Log.h"

#include <iostream>
#include <sstream>

namespace purrcat::osm {
namespace {

std::string jsonEscape(const std::string &s)
{
	std::string o;
	o.reserve(s.size() + 8);
	for (char c : s) {
		switch (c) {
		case '\\':
			o += "\\\\";
			break;
		case '"':
			o += "\\\"";
			break;
		case '\n':
			o += "\\n";
			break;
		default:
			o += c;
			break;
		}
	}
	return o;
}

std::string jobToJson(const JobState &st)
{
	std::ostringstream ss;
	ss << "{"
	   << "\"jobId\":\"" << jsonEscape(st.jobId) << "\","
	   << "\"regionId\":\"" << jsonEscape(st.regionId) << "\","
	   << "\"status\":\"" << toString(st.status) << "\","
	   << "\"progress\":" << st.progress << ","
	   << "\"message\":\"" << jsonEscape(st.message) << "\","
	   << "\"error\":\"" << jsonEscape(st.error) << "\""
	   << "}";
	return ss.str();
}

bool parseLatLon(const httplib::Request &req, double &lat, double &lon)
{
	if (!req.has_param("lat") || !req.has_param("lon"))
		return false;
	try {
		lat = std::stod(req.get_param_value("lat"));
		lon = std::stod(req.get_param_value("lon"));
		return true;
	} catch (...) {
		return false;
	}
}

} // namespace

TileServer::TileServer(std::filesystem::path dataRoot, int port)
	: m_port(port)
	, m_regions(std::make_unique<RegionService>(std::move(dataRoot)))
	, m_renderer(std::make_unique<TileRenderer>(*m_regions))
{
}

void TileServer::run()
{
	httplib::Server svr;

	svr.set_logger([](const httplib::Request &req, const httplib::Response &res) {
		logInfo("HTTP ", req.method, " ", req.path, " -> ", res.status);
	});

	svr.Get("/healthz", [](const httplib::Request &, httplib::Response &res) {
		res.set_content("{\"ok\":true}", "application/json");
	});

	svr.Get("/regions/lookup", [this](const httplib::Request &req, httplib::Response &res) {
		double lat = 0, lon = 0;
		if (!parseLatLon(req, lat, lon)) {
			res.status = 400;
			res.set_content("{\"error\":\"lat and lon required\"}", "application/json");
			return;
		}
		const auto json = jobToJson(m_regions->lookup(lat, lon));
		logInfo("lookup response ", json);
		res.set_content(json, "application/json");
	});

	svr.Post("/regions/ensure", [this](const httplib::Request &req, httplib::Response &res) {
		double lat = 0, lon = 0;
		if (!parseLatLon(req, lat, lon)) {
			res.status = 400;
			res.set_content("{\"error\":\"lat and lon required\"}", "application/json");
			return;
		}
		const auto json = jobToJson(m_regions->ensure(lat, lon));
		logInfo("ensure response ", json);
		res.set_content(json, "application/json");
	});

	svr.Get("/regions/ensure", [this](const httplib::Request &req, httplib::Response &res) {
		double lat = 0, lon = 0;
		if (!parseLatLon(req, lat, lon)) {
			res.status = 400;
			res.set_content("{\"error\":\"lat and lon required\"}", "application/json");
			return;
		}
		const auto json = jobToJson(m_regions->ensure(lat, lon));
		logInfo("ensure response ", json);
		res.set_content(json, "application/json");
	});

	svr.Get(R"(/regions/jobs/([^/]+))", [this](const httplib::Request &req, httplib::Response &res) {
		const std::string jobId = req.matches[1];
		const auto json = jobToJson(m_regions->job(jobId));
		logInfo("job response ", json);
		res.set_content(json, "application/json");
	});

	svr.Post("/tiles/clear", [this](const httplib::Request &, httplib::Response &res) {
		const auto n = m_regions->clearTileCache();
		res.set_content("{\"cleared\":" + std::to_string(n) + "}", "application/json");
	});

	svr.Get("/tiles/clear", [this](const httplib::Request &, httplib::Response &res) {
		const auto n = m_regions->clearTileCache();
		res.set_content("{\"cleared\":" + std::to_string(n) + "}", "application/json");
	});

	svr.Get(R"(/tiles/(\d+)/(\d+)/(\d+)\.png)", [this](const httplib::Request &req, httplib::Response &res) {
		const int z = std::stoi(req.matches[1]);
		const int x = std::stoi(req.matches[2]);
		const int y = std::stoi(req.matches[3]);
		logInfo("tile request z=", z, " x=", x, " y=", y);
		const auto png = m_renderer->renderOrLoad(z, x, y);
		if (png.empty()) {
			logWarn("tile unavailable z/x/y=", z, "/", x, "/", y, " (supported: 16, 18)");
			res.status = 404;
			res.set_content("tile unavailable (supported zoom: 16 and 18)", "text/plain");
			res.set_header("Cache-Control", "no-store");
			return;
		}
		logInfo("tile ok bytes=", png.size());
		res.set_content(std::string(reinterpret_cast<const char *>(png.data()), png.size()), "image/png");
		res.set_header("Cache-Control", "no-cache");
	});

	logInfo("listening http://0.0.0.0:", m_port);
	logInfo("  GET  /healthz");
	logInfo("  GET  /regions/lookup?lat=&lon=");
	logInfo("  POST /regions/ensure?lat=&lon=");
	logInfo("  GET  /regions/jobs/{jobId}");
	logInfo("  GET|POST /tiles/clear");
	logInfo("  GET  /tiles/{z}/{x}/{y}.png  (z=16 and z=18)");

	if (!svr.listen("0.0.0.0", m_port)) {
		logError("Failed to listen on port ", m_port);
	}
}

} // namespace purrcat::osm
