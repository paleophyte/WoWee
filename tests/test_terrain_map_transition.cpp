#include <catch_amalgamated.hpp>
#include "rendering/terrain_manager.hpp"

#include <chrono>
#include <thread>

namespace wowee::rendering {

// Only existing locks and the real worker/cache/queue paths are used. There is
// no alternate loader, injected completion callback or graphics dependency.
struct TerrainManagerTestAccess {
    struct Result {
        bool workerStarted = false;
        bool resetReachedBarrier = false;
        bool workerFinished = false;
        bool oldTilePublishedAfterReset = false;
        bool oldCacheSurvived = false;
        bool poolActive = false;
        bool destinationTileLoaded = false;
    };

    template<class Predicate> static bool wait(Predicate predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            if (predicate()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }

    static void cache(TerrainManager& manager, const std::shared_ptr<PendingTile>& tile) {
        std::lock_guard lock(manager.tileCacheMutex_);
        manager.tileCacheLru_.push_front(tile->coord);
        manager.tileCache_[tile->coord] = {tile, 0, manager.tileCacheLru_.begin()};
    }

    static void startOneWorker(TerrainManager& manager) {
        manager.workerCount = 1;
        manager.workerRunning.store(true);
        manager.workerThreads.emplace_back(&TerrainManager::workerLoop, &manager);
    }

    static Result transition(bool changeMap) {
        TerrainManager manager;
        const TileCoord coord{37, 40};
        auto origin = std::make_shared<PendingTile>();
        origin->coord = coord;
        cache(manager, origin);
        manager.uploadedM2Ids_.insert(123); // detects when old reset passes queue clearing

        // The worker will pop its request, then wait at getCachedTile. Hold a
        // later reset lock too, so the old implementation cannot finish reset
        // before we let that worker publish its result after the queue clear.
        std::unique_lock cacheGate(manager.tileCacheMutex_);
        std::unique_lock resetGate(manager.preparedWmoUniqueIdsMutex_);
        startOneWorker(manager);
        manager.enqueueTile(coord.x, coord.y);
        Result result;
        result.workerStarted = wait([&] {
            std::lock_guard lock(manager.queueMutex);
            return manager.preparingTiles_ == 1;
        });
        if (!result.workerStarted) {
            cacheGate.unlock(); resetGate.unlock(); manager.stopWorkers();
            return result;
        }

        std::atomic<bool> resetFinished{false};
        std::thread reset([&] {
            if (changeMap) manager.setMapName("Kalimdor");
            else manager.softReset();
            resetFinished.store(true);
        });
        result.resetReachedBarrier = wait([&] {
            if (!manager.workerRunning.load() || resetFinished.load()) return true;
            std::lock_guard lock(manager.uploadedM2IdsMutex_);
            return manager.uploadedM2Ids_.empty();
        });
        cacheGate.unlock();
        result.workerFinished = wait([&] {
            std::lock_guard lock(manager.queueMutex);
            return manager.preparingTiles_ == 0;
        });
        resetGate.unlock();
        reset.join();

        {
            std::lock_guard lock(manager.queueMutex);
            auto ready = manager.readyQueue;
            while (!ready.empty()) {
                result.oldTilePublishedAfterReset |= ready.front() == origin;
                ready.pop();
            }
            // The old-code failure has now been observed. Consume its result
            // before checking that a fresh request at the same coordinates can
            // still be loaded; no data is uploaded to a renderer in this test.
            while (!manager.readyQueue.empty()) manager.readyQueue.pop();
        }
        result.oldCacheSurvived = manager.getCachedTile(coord) == origin;
        result.poolActive = manager.workerRunning.load();

        if (!changeMap) manager.setMapName("Kalimdor");
        auto destination = std::make_shared<PendingTile>();
        destination->coord = coord;
        {
            std::lock_guard lock(manager.tileCacheMutex_);
            manager.tileCache_.clear(); manager.tileCacheLru_.clear();
        }
        cache(manager, destination);
        manager.enqueueTile(coord.x, coord.y);
        result.destinationTileLoaded = wait([&] {
            std::lock_guard lock(manager.queueMutex);
            return !manager.readyQueue.empty() && manager.readyQueue.front() == destination;
        });
        manager.stopWorkers();
        return result;
    }

    static bool sameMapKeepsPendingWork() {
        TerrainManager manager;
        auto tile = std::make_shared<PendingTile>(); tile->coord = {37, 40};
        cache(manager, tile);
        std::unique_lock gate(manager.tileCacheMutex_);
        startOneWorker(manager); manager.enqueueTile(37, 40);
        const bool started = wait([&] {
            std::lock_guard lock(manager.queueMutex); return manager.preparingTiles_ == 1;
        });
        std::atomic<bool> returned{false};
        std::thread sameMap([&] { manager.setMapName("Azeroth"); returned.store(true); });
        const bool didNotInterrupt = wait([&] { return returned.load(); });
        gate.unlock();
        sameMap.join();
        const bool delivered = wait([&] {
            std::lock_guard lock(manager.queueMutex);
            return !manager.readyQueue.empty() && manager.readyQueue.front() == tile;
        });
        manager.stopWorkers();
        return started && didNotInterrupt && delivered;
    }

    static bool inactiveResetCreatesNoWorkers() {
        TerrainManager manager;
        manager.softReset();
        return !manager.workerRunning.load() && manager.workerThreads.empty();
    }
};
}

TEST_CASE("terrain map boundaries cannot retain a worker's old tile", "[terrain-map-transition]") {
    using wowee::rendering::TerrainManagerTestAccess;
    for (bool changeMap : {false, true}) {
        CAPTURE(changeMap);
        const auto result = TerrainManagerTestAccess::transition(changeMap);
        REQUIRE(result.workerStarted);
        REQUIRE(result.resetReachedBarrier);
        REQUIRE(result.workerFinished);
        CHECK_FALSE(result.oldTilePublishedAfterReset);
        CHECK_FALSE(result.oldCacheSurvived);
        CHECK(result.poolActive);
        CHECK(result.destinationTileLoaded);
    }
}

TEST_CASE("selecting the same terrain map preserves valid in-flight work", "[terrain-map-transition]") {
    CHECK(wowee::rendering::TerrainManagerTestAccess::sameMapKeepsPendingWork());
}

TEST_CASE("an inactive terrain reset does not start background workers", "[terrain-map-transition]") {
    CHECK(wowee::rendering::TerrainManagerTestAccess::inactiveResetCreatesNoWorkers());
}
