#include "TileServer.h"

#include "Log.h"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
	std::filesystem::path dataRoot = "data";
	int port = 8080;

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if ((arg == "--data" || arg == "-d") && i + 1 < argc) {
			dataRoot = argv[++i];
		} else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
			port = std::stoi(argv[++i]);
		} else if (arg == "--help" || arg == "-h") {
			std::cout << "Usage: purrcat-osm-tiles [--data DIR] [--port N]\n";
			return 0;
		}
	}

	dataRoot = std::filesystem::absolute(dataRoot);
	std::filesystem::create_directories(dataRoot / "tiles");
	std::filesystem::create_directories(dataRoot / "geometry");

	purrcat::osm::logInfo("=== purrcat-osm-tiles start ===");
	purrcat::osm::logInfo("cwd=", std::filesystem::current_path().string());
	purrcat::osm::logInfo("data root=", dataRoot.string());
	purrcat::osm::logInfo("port=", port);

	purrcat::osm::TileServer server(dataRoot, port);
	server.run();
	return 0;
}
