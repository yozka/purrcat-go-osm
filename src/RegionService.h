#pragma once

#include "RegionId.h"

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace purrcat::osm {

enum class JobStatus {
	Queued,
	DownloadingGeometry,
	PackingRegion,
	Ready,
	Error
};

struct JobState {
	std::string jobId;
	std::string regionId;
	JobStatus status = JobStatus::Queued;
	double progress = 0.0; // 0..1
	std::string message;
	std::string error;
};

inline const char *toString(JobStatus s)
{
	switch (s) {
	case JobStatus::Queued:
		return "queued";
	case JobStatus::DownloadingGeometry:
		return "downloading";
	case JobStatus::PackingRegion:
		return "packing";
	case JobStatus::Ready:
		return "ready";
	case JobStatus::Error:
		return "error";
	}
	return "unknown";
}

class RegionService {
public:
	using ProgressFn = std::function<void(const JobState &)>;

	explicit RegionService(std::filesystem::path dataRoot);

	std::filesystem::path regionDir(const RegionId &id) const;
	std::filesystem::path geometryPath(const RegionId &id) const;
	std::filesystem::path regionPackPath(const RegionId &id) const;
	std::filesystem::path tilePath(int z, int x, int y) const;

	bool hasGeometry(const RegionId &id) const;
	bool hasRegionPack(const RegionId &id) const;

	JobState lookup(double lat, double lon) const;
	JobState ensure(double lat, double lon);
	JobState job(const std::string &jobId) const;

	/// Load pack JSON text for renderer; empty if missing.
	std::string loadPackJson(const RegionId &id) const;

	/// Delete all cached PNG tiles under data/tiles.
	std::size_t clearTileCache();

private:
	void ensureDirs() const;
	void ensureRegionDir(const RegionId &id) const;
	JobState ensureLocked(const RegionId &id);
	void runJob(const std::string &jobId, RegionId id);
	bool downloadGeometry(const RegionId &id, JobState &state);
	bool buildPack(const RegionId &id, JobState &state);
	void updateJob(const JobState &state);

	std::filesystem::path m_root;
	std::filesystem::path m_geometryDir;
	std::filesystem::path m_tilesDir;

	mutable std::mutex m_mutex;
	std::unordered_map<std::string, JobState> m_jobs;
	std::unordered_map<std::string, std::string> m_regionToJob;
	int m_nextJob = 1;
};

} // namespace purrcat::osm
