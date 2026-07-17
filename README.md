# PurrCat OSM Tiles

Local tile server: Overpass → geometry → region pack → PNG tiles.

## Run server (macOS)

```bash
./cmake/build_server_mac.command
```

Data: `build/server_mac/data/geometry/<regionId>/` (`osm.json`, `pack.json`) and `build/server_mac/data/tiles/`  
HTTP: **cpp-httplib** (server + Overpass HTTPS client via OpenSSL)  
API: `http://127.0.0.1:8080`

## Run server (Windows)

```bat
cmake\build_server_windows.cmd
```

Data (Windows): `build/server_windows/data/`
## API

- `GET /healthz`
- `GET /regions/lookup?lat=&lon=`
- `GET|POST /regions/ensure?lat=&lon=`
- `GET /regions/jobs/{jobId}`
- `GET /tiles/{z}/{x}/{y}.png` — **z=16** (wide) и **z=18** (close)

## Demo client

```bash
./cmake/build_demo_mac.command
```

```bat
cmake\build_demo_windows.cmd
```

Build: `build/demo_mac/` / `build/demo_windows/`

## Dependencies

- CMake 3.21+, C++20
- OpenSSL (macOS: `brew install openssl@3`)
- cpp-httplib + nlohmann/json + stb (vendored in `third_party/`)
