#include "RegionService.h"

#include "Http.h"
#include "Log.h"
#include "json.hpp"

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

namespace purrcat::osm {
namespace {

std::string readFile(const std::filesystem::path &path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return {};
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

bool writeFile(const std::filesystem::path &path, const std::string &data)
{
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	if (ec) {
		logError("create_directories failed for ", path.parent_path().string(), ": ", ec.message());
		return false;
	}
	std::ofstream out(path, std::ios::binary);
	if (!out) {
		logError("cannot open for write: ", path.string());
		return false;
	}
	out.write(data.data(), static_cast<std::streamsize>(data.size()));
	if (!out) {
		logError("write failed: ", path.string());
		return false;
	}
	logInfo("wrote ", data.size(), " bytes -> ", path.string());
	return true;
}

bool parseHttpsUrl(const std::string &url, std::string &host, std::string &path)
{
	const std::string prefix = "https://";
	if (url.size() <= prefix.size() || url.compare(0, prefix.size(), prefix) != 0)
		return false;
	const std::string rest = url.substr(prefix.size());
	const auto slash = rest.find('/');
	if (slash == std::string::npos) {
		host = rest;
		path = "/";
		return !host.empty();
	}
	host = rest.substr(0, slash);
	path = rest.substr(slash);
	return !host.empty();
}

/// POST Overpass query via cpp-httplib (same lib as the tile HTTP server).
bool httpPostFormData(const std::string &url, const std::string &query, std::string &outBody, std::string &err)
{
	std::string host;
	std::string path;
	if (!parseHttpsUrl(url, host, path)) {
		err = "unsupported url (need https://host/path): " + url;
		return false;
	}

	logInfo("httplib SSLClient host=", host, " path=", path, " queryBytes=", query.size());

	httplib::SSLClient cli(host);
	cli.set_connection_timeout(30, 0);
	cli.set_read_timeout(300, 0);
	cli.set_write_timeout(120, 0);
	cli.set_follow_location(true);

	httplib::Params params;
	params.emplace("data", query);

	const auto res = cli.Post(path, params);
	if (!res) {
		err = std::string("httplib error: ") + httplib::to_string(res.error());
		logError(err);
		return false;
	}

	logInfo("httplib response status=", res->status, " bodyBytes=", res->body.size());
	outBody = res->body;
	if (res->status < 200 || res->status >= 300) {
		err = "HTTP status " + std::to_string(res->status);
		const auto preview = outBody.substr(0, std::min<size_t>(outBody.size(), 200));
		logWarn("httplib non-2xx preview: ", preview);
		return false;
	}
	return true;
}

} // namespace

RegionService::RegionService(std::filesystem::path dataRoot)
	: m_root(std::move(dataRoot))
	, m_geometryDir(m_root / "geometry")
	, m_tilesDir(m_root / "tiles")
{
	ensureDirs();
	logInfo("RegionService dataRoot=", std::filesystem::absolute(m_root).string());
	logInfo("geometryDir=", std::filesystem::absolute(m_geometryDir).string());
	logInfo("tilesDir=", std::filesystem::absolute(m_tilesDir).string());
}

void RegionService::ensureDirs() const
{
	std::filesystem::create_directories(m_geometryDir);
	std::filesystem::create_directories(m_tilesDir);
}

void RegionService::ensureRegionDir(const RegionId &id) const
{
	const auto dir = regionDir(id);
	std::filesystem::create_directories(dir);
	logInfo("ensureRegionDir ", dir.string());
}

std::filesystem::path RegionService::regionDir(const RegionId &id) const
{
	return m_geometryDir / id.key();
}

std::filesystem::path RegionService::geometryPath(const RegionId &id) const
{
	return regionDir(id) / "osm.json";
}

std::filesystem::path RegionService::regionPackPath(const RegionId &id) const
{
	return regionDir(id) / "pack.json";
}

std::filesystem::path RegionService::tilePath(int z, int x, int y) const
{
	return m_tilesDir / std::to_string(z) / std::to_string(x) / (std::to_string(y) + ".png");
}

bool RegionService::hasGeometry(const RegionId &id) const
{
	const auto p = geometryPath(id);
	std::error_code ec;
	const bool ok = std::filesystem::exists(p, ec) && std::filesystem::file_size(p, ec) > 32;
	logInfo("hasGeometry ", id.key(), " path=", p.string(), " -> ", ok ? "yes" : "no");
	return ok;
}

bool RegionService::hasRegionPack(const RegionId &id) const
{
	const auto p = regionPackPath(id);
	std::error_code ec;
	const bool ok = std::filesystem::exists(p, ec) && std::filesystem::file_size(p, ec) > 32;
	logInfo("hasRegionPack ", id.key(), " path=", p.string(), " -> ", ok ? "yes" : "no");
	return ok;
}

std::string RegionService::loadPackJson(const RegionId &id) const
{
	const auto p = regionPackPath(id);
	auto text = readFile(p);
	logInfo("loadPackJson ", id.key(), " bytes=", text.size());
	return text;
}

std::size_t RegionService::clearTileCache()
{
	std::error_code ec;
	std::size_t removed = 0;
	if (!std::filesystem::exists(m_tilesDir, ec)) {
		logInfo("clearTileCache: tiles dir missing");
		return 0;
	}
	for (std::filesystem::recursive_directory_iterator it(m_tilesDir, ec), end; it != end; it.increment(ec)) {
		if (ec)
			break;
		if (!it->is_regular_file(ec))
			continue;
		if (it->path().extension() == ".png") {
			std::filesystem::remove(it->path(), ec);
			if (!ec)
				++removed;
		}
	}
	logInfo("clearTileCache removed ", removed, " png files from ", m_tilesDir.string());
	return removed;
}

JobState RegionService::lookup(double lat, double lon) const
{
	const RegionId id = RegionId::fromLatLon(lat, lon);
	const BBox b = id.bbox();
	logInfo("lookup lat=", lat, " lon=", lon, " region=", id.key(),
		" bbox=[", b.minLon, ",", b.minLat, ",", b.maxLon, ",", b.maxLat, "]");

	JobState st;
	st.regionId = id.key();
	st.jobId.clear();

	{
		std::lock_guard lock(m_mutex);
		const auto it = m_regionToJob.find(id.key());
		if (it != m_regionToJob.end()) {
			const auto jt = m_jobs.find(it->second);
			if (jt != m_jobs.end()) {
				// Memory can be stale if files were deleted — trust disk for ready.
				if (jt->second.status == JobStatus::Ready && !hasRegionPack(id)) {
					logWarn("lookup: memory ready but pack missing on disk — ignoring memory");
				} else if (jt->second.status != JobStatus::Ready || hasRegionPack(id)) {
					logInfo("lookup: return in-memory job ", jt->second.jobId, " status=", toString(jt->second.status));
					return jt->second;
				}
			}
		}
	}

	if (hasRegionPack(id)) {
		st.status = JobStatus::Ready;
		st.progress = 1.0;
		st.message = "region pack ready";
		logInfo("lookup: pack on disk");
		return st;
	}
	if (hasGeometry(id)) {
		st.status = JobStatus::PackingRegion;
		st.progress = 0.7;
		st.message = "geometry present, pack missing";
		logInfo("lookup: geometry on disk, pack missing");
		return st;
	}
	st.status = JobStatus::Queued;
	st.progress = 0.0;
	st.message = "region missing";
	logInfo("lookup: region missing on disk");
	return st;
}

JobState RegionService::ensure(double lat, double lon)
{
	const RegionId id = RegionId::fromLatLon(lat, lon);
	logInfo("ensure lat=", lat, " lon=", lon, " region=", id.key());
	std::lock_guard lock(m_mutex);
	return ensureLocked(id);
}

JobState RegionService::ensureLocked(const RegionId &id)
{
	if (hasRegionPack(id)) {
		JobState st;
		st.regionId = id.key();
		st.status = JobStatus::Ready;
		st.progress = 1.0;
		st.message = "region pack ready";
		logInfo("ensure: already ready on disk");
		return st;
	}

	const auto existing = m_regionToJob.find(id.key());
	if (existing != m_regionToJob.end()) {
		const auto jt = m_jobs.find(existing->second);
		if (jt != m_jobs.end()) {
			if (jt->second.status == JobStatus::Ready && !hasRegionPack(id)) {
				logWarn("ensure: stale ready job ", jt->second.jobId, " — restarting download");
				m_jobs.erase(jt);
				m_regionToJob.erase(existing);
			} else if (jt->second.status != JobStatus::Error && jt->second.status != JobStatus::Ready) {
				logInfo("ensure: reuse running job ", jt->second.jobId, " status=", toString(jt->second.status));
				return jt->second;
			} else if (jt->second.status == JobStatus::Ready && hasRegionPack(id)) {
				logInfo("ensure: memory ready + pack on disk");
				return jt->second;
			} else if (jt->second.status == JobStatus::Error) {
				logWarn("ensure: previous job error — retry: ", jt->second.error);
				m_jobs.erase(jt);
				m_regionToJob.erase(id.key());
			}
		}
	}

	JobState st;
	st.jobId = "job-" + std::to_string(m_nextJob++);
	st.regionId = id.key();
	st.status = JobStatus::Queued;
	st.progress = 0.0;
	st.message = "queued";
	m_jobs[st.jobId] = st;
	m_regionToJob[id.key()] = st.jobId;
	logInfo("ensure: started ", st.jobId, " for region ", id.key());

	std::thread([this, jobId = st.jobId, id]() { runJob(jobId, id); }).detach();
	return st;
}

JobState RegionService::job(const std::string &jobId) const
{
	std::lock_guard lock(m_mutex);
	const auto it = m_jobs.find(jobId);
	if (it == m_jobs.end()) {
		JobState st;
		st.jobId = jobId;
		st.status = JobStatus::Error;
		st.error = "unknown job";
		st.message = "unknown job";
		logWarn("job lookup unknown: ", jobId);
		return st;
	}
	return it->second;
}

void RegionService::updateJob(const JobState &state)
{
	std::lock_guard lock(m_mutex);
	m_jobs[state.jobId] = state;
	m_regionToJob[state.regionId] = state.jobId;
	logInfo("job ", state.jobId, " region=", state.regionId, " status=", toString(state.status),
		" progress=", state.progress, " msg=", state.message,
		state.error.empty() ? "" : " error=", state.error);
}

void RegionService::runJob(const std::string &jobId, RegionId id)
{
	logInfo("runJob begin ", jobId, " region=", id.key());
	JobState state;
	{
		std::lock_guard lock(m_mutex);
		state = m_jobs[jobId];
	}

	try {
		if (!hasGeometry(id)) {
			state.status = JobStatus::DownloadingGeometry;
			state.progress = 0.05;
			state.message = "downloading geometry from Overpass";
			updateJob(state);
			if (!downloadGeometry(id, state)) {
				state.status = JobStatus::Error;
				if (state.error.empty())
					state.error = "geometry download failed";
				state.message = state.error;
				updateJob(state);
				logError("runJob download failed: ", state.error);
				return;
			}
		} else {
			logInfo("runJob: geometry already on disk, skip download");
		}

		state.status = JobStatus::PackingRegion;
		state.progress = 0.75;
		state.message = "building region pack";
		updateJob(state);

		if (!buildPack(id, state)) {
			state.status = JobStatus::Error;
			if (state.error.empty())
				state.error = "pack build failed";
			state.message = state.error;
			updateJob(state);
			logError("runJob pack failed: ", state.error);
			return;
		}

		state.status = JobStatus::Ready;
		state.progress = 1.0;
		state.message = "ready";
		state.error.clear();
		updateJob(state);
		logInfo("runJob done ", jobId, " pack=", regionPackPath(id).string());
	} catch (const std::exception &ex) {
		state.status = JobStatus::Error;
		state.error = ex.what();
		state.message = ex.what();
		updateJob(state);
		logError("runJob exception: ", ex.what());
	}
}

bool RegionService::downloadGeometry(const RegionId &id, JobState &state)
{
	ensureRegionDir(id);
	const BBox b = id.bbox();
	const auto outPath = geometryPath(id);
	const auto tmpPath = regionDir(id) / "osm.json.part";
	logInfo("downloadGeometry region=", id.key(), " -> ", outPath.string());
	logInfo("bbox lat[", b.minLat, "..", b.maxLat, "] lon[", b.minLon, "..", b.maxLon, "]");

	const std::vector<std::string> endpoints = {
		"https://overpass-api.de/api/interpreter",
		"https://overpass.openstreetmap.fr/api/interpreter",
		"https://overpass.private.coffee/api/interpreter",
	};

	auto fetchQuery = [&](const std::string &query, const std::string &label) -> std::string {
		const auto queryPath = regionDir(id) / (label + ".query.txt");
		logInfo("fetchQuery label=", label, " queryBytes=", query.size());
		if (!writeFile(queryPath, query)) {
			state.error = "cannot write overpass query file";
			logError(state.error);
			return {};
		}
		state.message = "Overpass: " + label + "…";
		updateJob(state);

		for (const auto &url : endpoints) {
			state.message = "Overpass [" + label + "] " + url;
			updateJob(state);

			std::string body;
			std::string err;
			if (!httpPostFormData(url, query, body, err)) {
				logWarn("fetchQuery label=", label, " url=", url, " failed: ", err);
				continue;
			}

			const auto preview = body.substr(0, std::min<size_t>(body.size(), 180));
			logInfo("response preview: ", preview);
			if (body.find("\"elements\"") != std::string::npos) {
				logInfo("fetchQuery OK label=", label, " bytes=", body.size());
				return body;
			}
			logWarn("response has no \"elements\" — trying next mirror");
		}
		logError("fetchQuery failed for all mirrors, label=", label);
		return {};
	};

	std::ostringstream bbStream;
	bbStream << std::fixed << b.minLat << "," << b.minLon << "," << b.maxLat << "," << b.maxLon;
	const std::string bb = bbStream.str();
	logInfo("overpass bbox clause: ", bb);

	std::ostringstream q1;
	q1 << "[out:json][timeout:180];\n(\n"
	   << "  way[\"highway\"](" << bb << ");\n"
	   << "  way[\"natural\"=\"water\"](" << bb << ");\n"
	   << "  relation[\"type\"=\"multipolygon\"][\"natural\"=\"water\"](" << bb << ");\n"
	   << "  way[\"waterway\"](" << bb << ");\n"
	   << "  way[\"natural\"=\"coastline\"](" << bb << ");\n"
	   << "  way[\"landuse\"~\"grass|forest|meadow|recreation_ground|village_green\"](" << bb << ");\n"
	   << "  way[\"leisure\"~\"park|garden|nature_reserve\"](" << bb << ");\n"
	   << "  way[\"natural\"~\"wood|scrub\"](" << bb << ");\n"
	   << ");\nout body geom;\n";

	state.progress = 0.1;
	updateJob(state);
	const std::string baseJson = fetchQuery(q1.str(), "base");
	if (baseJson.empty()) {
		state.error = "overpass failed for base layers (roads/water/green)";
		logError(state.error);
		return false;
	}
	state.progress = 0.45;
	state.message = "base geometry downloaded";
	updateJob(state);

	std::ostringstream q2;
	q2 << "[out:json][timeout:180];\n"
	   << "way[\"building\"](" << bb << ");\n"
	   << "out body geom;\n";

	state.progress = 0.5;
	updateJob(state);
	std::string buildingsJson = fetchQuery(q2.str(), "buildings");
	if (buildingsJson.empty()) {
		logWarn("buildings skipped (overpass busy), using base layers only");
		state.message = "buildings skipped (overpass busy), using base layers";
		updateJob(state);
		buildingsJson = "{\"elements\":[]}";
	}

	try {
		auto base = nlohmann::json::parse(baseJson);
		auto buildings = nlohmann::json::parse(buildingsJson);
		if (!base.contains("elements") || !base["elements"].is_array()) {
			state.error = "invalid base overpass JSON";
			logError(state.error);
			return false;
		}
		const size_t baseCount = base["elements"].size();
		if (buildings.contains("elements") && buildings["elements"].is_array()) {
			for (auto &el : buildings["elements"])
				base["elements"].push_back(std::move(el));
		}
		const std::string merged = base.dump();
		logInfo("merged elements=", base["elements"].size(), " (base=", baseCount, ") bytes=", merged.size());
		if (!writeFile(tmpPath, merged)) {
			state.error = "cannot write geometry temp file";
			return false;
		}
		std::error_code ec;
		std::filesystem::rename(tmpPath, outPath, ec);
		if (ec) {
			logError("rename failed: ", ec.message());
			state.error = "rename geometry failed: " + ec.message();
			return false;
		}
		state.progress = 0.7;
		state.message = "geometry saved (" + std::to_string(merged.size()) + " bytes, "
			+ std::to_string(base["elements"].size()) + " elements)";
		updateJob(state);
		logInfo("geometry saved OK ", outPath.string());
		return true;
	} catch (const std::exception &ex) {
		state.error = std::string("json merge failed: ") + ex.what();
		logError(state.error);
		return false;
	}
}

bool RegionService::buildPack(const RegionId &id, JobState &state)
{
	ensureRegionDir(id);
	const auto geoPath = geometryPath(id);
	const auto packPath = regionPackPath(id);
	logInfo("buildPack ", id.key(), " from ", geoPath.string());
	if (!std::filesystem::exists(geoPath)) {
		state.error = "geometry file missing";
		logError(state.error, " ", geoPath.string());
		return false;
	}

	const auto geo = readFile(geoPath);
	std::ostringstream pack;
	pack << "{\n";
	pack << "  \"regionId\": \"" << id.key() << "\",\n";
	pack << "  \"stepDeg\": " << kRegionStepDeg << ",\n";
	const BBox b = id.bbox();
	pack << "  \"bbox\": [" << b.minLon << "," << b.minLat << "," << b.maxLon << "," << b.maxLat << "],\n";
	pack << "  \"source\": \"overpass\",\n";
	pack << "  \"osm\": " << geo << "\n";
	pack << "}\n";

	if (!writeFile(packPath, pack.str())) {
		state.error = "cannot write pack file";
		return false;
	}

	state.progress = 0.95;
	state.message = "pack written";
	updateJob(state);
	logInfo("pack saved OK ", packPath.string());
	return true;
}

} // namespace purrcat::osm
