cmake_minimum_required(VERSION 3.21)

project(PurrCatOsmTiles VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Homebrew OpenSSL on Apple Silicon / Intel
if(APPLE AND NOT OPENSSL_ROOT_DIR)
	foreach(_openssl_hint
		/opt/homebrew/opt/openssl@3
		/usr/local/opt/openssl@3
		/opt/homebrew/opt/openssl
		/usr/local/opt/openssl)
		if(EXISTS "${_openssl_hint}")
			set(OPENSSL_ROOT_DIR "${_openssl_hint}")
			break()
		endif()
	endforeach()
endif()

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_executable(purrcat-osm-tiles
	src/main.cpp
	src/RegionService.cpp
	src/TileRenderer.cpp
	src/TileServer.cpp
)

target_include_directories(purrcat-osm-tiles PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_compile_definitions(purrcat-osm-tiles PRIVATE
	CPPHTTPLIB_OPENSSL_SUPPORT
)

target_link_libraries(purrcat-osm-tiles PRIVATE
	Threads::Threads
	OpenSSL::SSL
	OpenSSL::Crypto
)

if(WIN32)
	target_link_libraries(purrcat-osm-tiles PRIVATE ws2_32 crypt32)
endif()

install(TARGETS purrcat-osm-tiles RUNTIME DESTINATION bin)
