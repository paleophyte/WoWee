#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

constexpr std::uint32_t MMAP_MAGIC = 0x4d4d4150;
constexpr std::uint32_t MMAP_VERSION = 8;
constexpr float SIZE_OF_GRIDS = 533.33333f;
constexpr int CENTER_GRID_ID = 32;
constexpr float CENTER_GRID_OFFSET = SIZE_OF_GRIDS / 2.0f;
constexpr int MAX_NUMBER_OF_GRIDS = 64;
constexpr int MAX_POLYS = 4096;
constexpr int MAX_POINTS = 4096;
constexpr unsigned short NAV_GROUND = 0x1;
constexpr unsigned short NAV_GROUND_STEEP = 0x2;
constexpr unsigned short NAV_WATER = 0x4;
constexpr unsigned short NAV_MAGMA_SLIME = 0x8;

struct MmapTileHeader {
    std::uint32_t mmapMagic;
    std::uint32_t dtVersion;
    std::uint32_t mmapVersion;
    std::uint32_t size;
    std::uint32_t usesLiquids;
};

struct Point {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct LoadedTile {
    dtTileRef ref = 0;
};

struct NavMeshDeleter {
    void operator()(dtNavMesh* mesh) const { dtFreeNavMesh(mesh); }
};

struct QueryDeleter {
    void operator()(dtNavMeshQuery* query) const { dtFreeNavMeshQuery(query); }
};

std::string readStdin()
{
    return std::string(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
}

void writeJson(json const& doc)
{
    std::cout << doc.dump() << "\n";
}

void fail(std::string const& message, json extra = {})
{
    json out = {
        {"ok", false},
        {"error", message},
        {"backend", "cmangos-mmap"}
    };
    for (auto& item : extra.items())
        out[item.key()] = item.value();
    writeJson(out);
    std::exit(1);
}

float number(json const& doc, char const* key)
{
    if (!doc.contains(key) || !doc[key].is_number())
        fail(std::string("missing numeric field: ") + key);
    return doc[key].get<float>();
}

Point point(json const& doc, char const* key)
{
    if (!doc.contains(key) || !doc[key].is_object())
        fail(std::string("missing object field: ") + key);
    auto const& p = doc[key];
    return Point{number(p, "x"), number(p, "y"), number(p, "z")};
}

float distance(Point const& a, Point const& b)
{
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float dz = b.z - a.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Point lerp(Point const& a, Point const& b, float t)
{
    return Point{
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

std::filesystem::path dataDir(json const& request)
{
    if (request.contains("dataDir") && request["dataDir"].is_string())
    {
        std::string value = request["dataDir"].get<std::string>();
        if (!value.empty())
            return value;
    }

    if (char const* env = std::getenv("WOWEE_CMANGOS_DATA_DIR"))
        return env;

    fail("missing dataDir; provide request.dataDir or WOWEE_CMANGOS_DATA_DIR");
    return {};
}

int generatorTileCoord(float detourAxisValue)
{
    // MapBuilder::getTileBounds writes tiles where:
    //   bmax = (32 - tile) * SIZE_OF_GRIDS
    //   bmin = bmax - SIZE_OF_GRIDS
    // So invert that interval test for a Detour X/Z axis value.
    int coord = int(std::floor(32.0f - (detourAxisValue / SIZE_OF_GRIDS)));
    return std::clamp(coord, 0, MAX_NUMBER_OF_GRIDS - 1);
}

std::filesystem::path mapPath(std::filesystem::path const& base, int mapId)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%03d.mmap", mapId);
    return base / "mmaps" / name;
}

std::filesystem::path tilePath(std::filesystem::path const& base, int mapId, int x, int y)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%03d%02d%02d.mmtile", mapId, x, y);
    return base / "mmaps" / name;
}

std::unique_ptr<dtNavMesh, NavMeshDeleter> loadNavMesh(std::filesystem::path const& base, int mapId)
{
    auto path = mapPath(base, mapId);
    std::ifstream file(path, std::ios::binary);
    if (!file)
        fail("could not open mmap file", {{"path", path.string()}});

    dtNavMeshParams params{};
    file.read(reinterpret_cast<char*>(&params), sizeof(params));
    if (!file)
        fail("could not read mmap params", {{"path", path.string()}});

    std::unique_ptr<dtNavMesh, NavMeshDeleter> mesh(dtAllocNavMesh());
    if (!mesh)
        fail("dtAllocNavMesh failed");

    dtStatus status = mesh->init(&params);
    if (dtStatusFailed(status))
        fail("dtNavMesh init failed", {{"status", status}});

    return mesh;
}

bool loadTile(std::filesystem::path const& path, dtNavMesh& mesh, std::vector<LoadedTile>& loaded)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    MmapTileHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file)
        fail("could not read mmtile header", {{"path", path.string()}});
    if (header.mmapMagic != MMAP_MAGIC)
        fail("bad mmtile magic", {{"path", path.string()}});
    if (header.dtVersion != DT_NAVMESH_VERSION)
        fail("mmtile Detour version mismatch", {{"path", path.string()}, {"fileVersion", header.dtVersion}, {"expectedVersion", DT_NAVMESH_VERSION}});
    if (header.mmapVersion != MMAP_VERSION)
        fail("mmtile mmap version mismatch", {{"path", path.string()}, {"fileVersion", header.mmapVersion}, {"expectedVersion", MMAP_VERSION}});

    unsigned char* data = static_cast<unsigned char*>(dtAlloc(header.size, DT_ALLOC_PERM));
    if (!data)
        fail("dtAlloc tile data failed", {{"path", path.string()}, {"size", header.size}});

    file.read(reinterpret_cast<char*>(data), header.size);
    if (!file)
    {
        dtFree(data);
        fail("could not read mmtile data", {{"path", path.string()}});
    }

    dtTileRef tileRef = 0;
    dtStatus status = mesh.addTile(data, header.size, DT_TILE_FREE_DATA, 0, &tileRef);
    if (dtStatusFailed(status))
    {
        dtFree(data);
        fail("could not add mmtile to navmesh", {{"path", path.string()}, {"status", status}});
    }
    loaded.push_back(LoadedTile{tileRef});
    return true;
}

int loadAllTiles(std::filesystem::path const& base, int mapId, dtNavMesh& mesh, std::vector<LoadedTile>& loaded)
{
    auto dir = base / "mmaps";
    if (!std::filesystem::is_directory(dir))
        fail("mmaps directory does not exist", {{"path", dir.string()}});

    char prefix[8];
    std::snprintf(prefix, sizeof(prefix), "%03d", mapId);

    int count = 0;
    for (auto const& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".mmtile")
            continue;
        std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) != 0)
            continue;
        if (loadTile(entry.path(), mesh, loaded))
            ++count;
    }
    return count;
}

int loadBboxTiles(std::filesystem::path const& base, int mapId, Point start, Point end, int margin, dtNavMesh& mesh, std::vector<LoadedTile>& loaded)
{
    // Filename order follows MapBuilder output:
    //   %03u%02i%02i.mmtile => mapId, tileY, tileX
    // The tile bounds themselves are on Detour's X/Z plane, where WoWee
    // canonical x maps to Detour X and canonical y maps to Detour Z.
    int sx = generatorTileCoord(start.x);
    int sy = generatorTileCoord(start.y);
    int ex = generatorTileCoord(end.x);
    int ey = generatorTileCoord(end.y);

    int minX = std::clamp(std::min(sx, ex) - margin, 0, MAX_NUMBER_OF_GRIDS - 1);
    int maxX = std::clamp(std::max(sx, ex) + margin, 0, MAX_NUMBER_OF_GRIDS - 1);
    int minY = std::clamp(std::min(sy, ey) - margin, 0, MAX_NUMBER_OF_GRIDS - 1);
    int maxY = std::clamp(std::max(sy, ey) + margin, 0, MAX_NUMBER_OF_GRIDS - 1);

    int count = 0;
    for (int tileX = minX; tileX <= maxX; ++tileX)
        for (int tileY = minY; tileY <= maxY; ++tileY)
            if (loadTile(tilePath(base, mapId, tileY, tileX), mesh, loaded))
                ++count;
    return count;
}

void toDetour(Point p, float out[3])
{
    // WoWee canonical: x=north, y=west, z=up.
    // CMaNGOS server: x=west, y=north, z=up.
    // Detour in CMaNGOS PathFinder: {serverY, z, serverX}.
    out[0] = p.x;
    out[1] = p.z;
    out[2] = p.y;
}

Point fromDetour(float const* p)
{
    return Point{p[0], p[2], p[1]};
}

bool projectToNavmeshHeight(dtNavMeshQuery& query, dtQueryFilter const& filter, float const extents[3], Point const& input, Point& output)
{
    float detour[3];
    toDetour(input, detour);

    dtPolyRef ref = 0;
    float nearest[3];
    dtStatus nearestStatus = query.findNearestPoly(detour, extents, &filter, &ref, nearest);
    if (dtStatusFailed(nearestStatus) || !ref)
    {
        output = input;
        return false;
    }

    float height = 0.0f;
    dtStatus heightStatus = query.getPolyHeight(ref, nearest, &height);
    if (dtStatusFailed(heightStatus))
    {
        output = fromDetour(nearest);
        return false;
    }

    nearest[1] = height;
    output = fromDetour(nearest);
    return true;
}

std::vector<Point> densifyWaypoints(dtNavMeshQuery& query, dtQueryFilter const& filter, float const extents[3],
                                    std::vector<Point> const& points, float stepYards, int& projectedCount)
{
    std::vector<Point> dense;
    projectedCount = 0;
    if (points.empty())
        return dense;

    stepYards = std::max(1.0f, stepYards);
    dense.reserve(points.size() * 4);
    dense.push_back(points.front());

    for (std::size_t i = 1; i < points.size(); ++i)
    {
        Point const& a = points[i - 1];
        Point const& b = points[i];
        int segments = std::max(1, int(std::ceil(distance(a, b) / stepYards)));
        for (int s = 1; s <= segments; ++s)
        {
            Point sample = lerp(a, b, float(s) / float(segments));
            Point projected;
            if (projectToNavmeshHeight(query, filter, extents, sample, projected))
                ++projectedCount;
            else
                projected = sample;

            if (!dense.empty() && distance(dense.back(), projected) < 0.05f)
                continue;
            dense.push_back(projected);
        }
    }

    return dense;
}

json pathTypeName(dtStatus pathStatus, int polyCount, dtPolyRef endRef, dtPolyRef lastRef)
{
    if (polyCount == 0)
        return "nopollies";
    if (dtStatusFailed(pathStatus))
        return "failed";
    if (lastRef != endRef)
        return "partial";
    return "navmesh";
}

json navMeshBounds(dtNavMesh const& mesh)
{
    json tiles = json::array();
    float globalMin[3] = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    float globalMax[3] = {
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
    };

    for (int i = 0; i < mesh.getMaxTiles(); ++i)
    {
        dtMeshTile const* tile = mesh.getTile(i);
        if (!tile || !tile->header)
            continue;
        for (int axis = 0; axis < 3; ++axis)
        {
            globalMin[axis] = std::min(globalMin[axis], tile->header->bmin[axis]);
            globalMax[axis] = std::max(globalMax[axis], tile->header->bmax[axis]);
        }
        if (tiles.size() < 12)
        {
            tiles.push_back({
                {"x", tile->header->x},
                {"y", tile->header->y},
                {"bmin", {tile->header->bmin[0], tile->header->bmin[1], tile->header->bmin[2]}},
                {"bmax", {tile->header->bmax[0], tile->header->bmax[1], tile->header->bmax[2]}},
            });
        }
    }

    float const* orig = mesh.getParams()->orig;
    return {
        {"orig", {orig[0], orig[1], orig[2]}},
        {"detourMin", {globalMin[0], globalMin[1], globalMin[2]}},
        {"detourMax", {globalMax[0], globalMax[1], globalMax[2]}},
        {"sampleTiles", tiles},
    };
}

std::vector<Point> smoothWaypoints(std::vector<Point> const& points, float zWeight, float zMinChange)
{
    if (points.size() <= 2)
        return points;

    std::vector<Point> result;
    result.reserve(points.size());
    result.push_back(points.front());

    float prevSmoothedZ = points[0].z;

    for (std::size_t i = 1; i + 1 < points.size(); ++i)
    {
        Point const& prev = points[i - 1];
        Point const& curr = points[i];
        Point const& next = points[i + 1];

        Point s = curr;

        float hDistNext = std::sqrt(
            (next.x - prev.x) * (next.x - prev.x) +
            (next.y - prev.y) * (next.y - prev.y)
        );

        float rawZ = curr.z;

        if (hDistNext > 0.5f)
        {
            float zChangePerYard = std::abs(rawZ - prev.z) / hDistNext;
            if (zChangePerYard < zMinChange)
            {
                prevSmoothedZ = prevSmoothedZ + zWeight * (rawZ - prevSmoothedZ);
            }
            else
            {
                prevSmoothedZ = rawZ;
            }
        }
        else
        {
            prevSmoothedZ = prevSmoothedZ + zWeight * (rawZ - prevSmoothedZ);
        }

        s.z = prevSmoothedZ;

        float smoothX = 0.333f * prev.x + 0.334f * curr.x + 0.333f * next.x;
        float smoothY = 0.333f * prev.y + 0.334f * curr.y + 0.333f * next.y;

        if (i == 1 || distance(result.back(), s) >= 0.05f)
            result.push_back(s);
    }

    result.push_back(points.back());
    return result;
}

} // namespace

int main()
{
    try
    {
        json request = json::parse(readStdin());
        int mapId = int(number(request, "mapId"));
        Point start = point(request, "start");
        Point end = point(request, "end");
        std::filesystem::path base = dataDir(request);
        std::string tileMode = request.value("tileMode", "all");
        int tileMargin = request.value("tileMargin", 2);

        auto mesh = loadNavMesh(base, mapId);
        std::vector<LoadedTile> loadedTiles;
        int loadedCount = tileMode == "bbox"
            ? loadBboxTiles(base, mapId, start, end, tileMargin, *mesh, loadedTiles)
            : loadAllTiles(base, mapId, *mesh, loadedTiles);
        if (loadedCount == 0)
            fail("no mmtile files loaded", {{"mapId", mapId}, {"dataDir", base.string()}, {"tileMode", tileMode}});

        if (request.value("debugMode", std::string()) == "bounds")
        {
            json bounds = navMeshBounds(*mesh);
            bounds["ok"] = true;
            bounds["backend"] = "cmangos-mmap";
            bounds["pathType"] = "debug-bounds";
            bounds["mapId"] = mapId;
            bounds["loadedTiles"] = loadedCount;
            writeJson(bounds);
            return 0;
        }

        std::unique_ptr<dtNavMeshQuery, QueryDeleter> query(dtAllocNavMeshQuery());
        if (!query)
            fail("dtAllocNavMeshQuery failed");
        dtStatus initStatus = query->init(mesh.get(), 8192);
        if (dtStatusFailed(initStatus))
            fail("dtNavMeshQuery init failed", {{"status", initStatus}});

        float startPt[3];
        float endPt[3];
        toDetour(start, startPt);
        toDetour(end, endPt);

        float extents[3] = {
            request.value("polySearchX", 10.0f),
            request.value("polySearchZ", 10.0f),
            request.value("polySearchY", 10.0f),
        };
        dtQueryFilter filter;
        filter.setIncludeFlags(NAV_GROUND | NAV_WATER);
        filter.setExcludeFlags(NAV_MAGMA_SLIME | NAV_GROUND_STEEP);
        filter.setAreaCost(9, 20.0f);
        filter.setAreaCost(12, 5.0f);
        filter.setAreaCost(13, 20.0f);

        dtPolyRef startRef = 0;
        dtPolyRef endRef = 0;
        float nearestStart[3];
        float nearestEnd[3];
        dtStatus startStatus = query->findNearestPoly(startPt, extents, &filter, &startRef, nearestStart);
        dtStatus endStatus = query->findNearestPoly(endPt, extents, &filter, &endRef, nearestEnd);
        if (dtStatusFailed(startStatus) || !startRef)
            fail("could not find nearest start polygon", {
                {"status", startStatus},
                {"loadedTiles", loadedCount},
                {"detourStart", {startPt[0], startPt[1], startPt[2]}},
                {"searchExtents", {extents[0], extents[1], extents[2]}},
            });
        if (dtStatusFailed(endStatus) || !endRef)
            fail("could not find nearest end polygon", {
                {"status", endStatus},
                {"loadedTiles", loadedCount},
                {"detourEnd", {endPt[0], endPt[1], endPt[2]}},
                {"searchExtents", {extents[0], extents[1], extents[2]}},
            });

        std::vector<dtPolyRef> polys(MAX_POLYS);
        int polyCount = 0;
        dtStatus pathStatus = query->findPath(startRef, endRef, nearestStart, nearestEnd, &filter, polys.data(), &polyCount, MAX_POLYS);
        if (dtStatusFailed(pathStatus) || polyCount <= 0)
            fail("findPath failed", {{"status", pathStatus}, {"loadedTiles", loadedCount}});

        std::vector<float> straight(MAX_POINTS * 3);
        std::vector<unsigned char> straightFlags(MAX_POINTS);
        std::vector<dtPolyRef> straightRefs(MAX_POINTS);
        int pointCount = 0;
        dtStatus straightStatus = query->findStraightPath(
            nearestStart,
            nearestEnd,
            polys.data(),
            polyCount,
            straight.data(),
            straightFlags.data(),
            straightRefs.data(),
            &pointCount,
            MAX_POINTS
        );
        if (dtStatusFailed(straightStatus) || pointCount <= 0)
            fail("findStraightPath failed", {{"status", straightStatus}, {"polyCount", polyCount}, {"loadedTiles", loadedCount}});

        float waypointStepYards = request.value("waypointStepYards", 4.0f);
        if (!std::isfinite(waypointStepYards) || waypointStepYards <= 0.0f)
            waypointStepYards = 4.0f;
        waypointStepYards = std::clamp(waypointStepYards, 1.0f, 40.0f);

        std::vector<Point> straightPoints;
        straightPoints.reserve(pointCount);
        for (int i = 0; i < pointCount; ++i)
        {
            Point p = fromDetour(&straight[i * 3]);
            straightPoints.push_back(p);
        }

        int projectedSamples = 0;
        std::vector<Point> densePoints = densifyWaypoints(*query, filter, extents, straightPoints, waypointStepYards, projectedSamples);

        float smoothZWeight = request.value("smoothZWeight", 0.15f);
        float smoothZMinChange = request.value("smoothZMinChange", 0.6f);
        if (std::isfinite(smoothZWeight) && smoothZWeight > 0.0f && densePoints.size() > 2)
        {
            densePoints = smoothWaypoints(densePoints, smoothZWeight, smoothZMinChange);
        }

        json waypoints = json::array();
        float waypointArrivalRadius = request.value("waypointArrivalRadius", 1.5f);
        if (!std::isfinite(waypointArrivalRadius) || waypointArrivalRadius <= 0.0f)
            waypointArrivalRadius = 1.5f;
        waypointArrivalRadius = std::clamp(waypointArrivalRadius, 0.5f, 5.0f);

        for (Point const& p : densePoints)
        {
            waypoints.push_back({{"x", p.x}, {"y", p.y}, {"z", p.z}, {"arrivalRadius", waypointArrivalRadius}});
        }

        json out = {
            {"ok", true},
            {"backend", "cmangos-mmap"},
            {"pathType", pathTypeName(pathStatus, polyCount, endRef, polys[polyCount - 1])},
            {"coordinateSpace", "wowee-canonical"},
            {"mapId", mapId},
            {"loadedTiles", loadedCount},
            {"polyCount", polyCount},
            {"straightPointCount", pointCount},
            {"waypointStepYards", waypointStepYards},
            {"waypointArrivalRadius", waypointArrivalRadius},
            {"projectedSamples", projectedSamples},
            {"smoothZWeight", smoothZWeight},
            {"smoothZMinChange", smoothZMinChange},
            {"waypoints", waypoints},
        };
        writeJson(out);
        return 0;
    }
    catch (std::exception const& exc)
    {
        fail(std::string("unhandled exception: ") + exc.what());
    }
}
