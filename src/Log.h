#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace purrcat::osm {

inline std::mutex &logMutex()
{
	static std::mutex m;
	return m;
}

inline std::string logTimestamp()
{
	using clock = std::chrono::system_clock;
	const auto now = clock::now();
	const auto t = clock::to_time_t(now);
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
	std::tm tm{};
#if defined(_WIN32)
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec,
		static_cast<int>(ms.count()));
	return buf;
}

template <typename... Args>
inline void logInfo(Args &&...args)
{
	std::lock_guard lock(logMutex());
	std::ostringstream ss;
	ss << "[" << logTimestamp() << "] ";
	(ss << ... << args);
	std::cout << ss.str() << std::endl;
}

template <typename... Args>
inline void logWarn(Args &&...args)
{
	std::lock_guard lock(logMutex());
	std::ostringstream ss;
	ss << "[" << logTimestamp() << "] WARN ";
	(ss << ... << args);
	std::cerr << ss.str() << std::endl;
}

template <typename... Args>
inline void logError(Args &&...args)
{
	std::lock_guard lock(logMutex());
	std::ostringstream ss;
	ss << "[" << logTimestamp() << "] ERROR ";
	(ss << ... << args);
	std::cerr << ss.str() << std::endl;
}

} // namespace purrcat::osm
