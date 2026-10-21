#pragma once
#include "rendering/terrain_manager.hpp"
#include <chrono>
#include <thread>

namespace wowee::rendering {
// Use the existing test-access friendship; no production test mode or alternate
// worker/finalization implementation. Generated modes seed cached input; real mode queues ADTs.
struct TerrainManagerTestAccess {
    static void seed(TerrainManager& manager,const pipeline::TerrainMesh& mesh,
                     const std::vector<std::string>& paths,
                     const std::unordered_map<std::string,pipeline::BLPImage>& textures,int count){
        for(int x=0;x<count;++x){
            auto pending=std::make_shared<PendingTile>();pending->coord={x,0};
            pending->mesh=mesh;pending->terrain.textures=paths;pending->preloadedTextures=textures;
            {
                std::lock_guard lock(manager.tileCacheMutex_);
                manager.tileCacheLru_.push_front(pending->coord);
                manager.tileCache_[pending->coord]={pending,0,manager.tileCacheLru_.begin()};
            }
            require(manager.enqueueTile(x,0),"Generated worker tile enqueue failed");
        }
    }
    static Json resetWithBusyWorker(TerrainManager& manager,const std::string& name,
                                    const pipeline::TerrainMesh& mesh){
        // Hold the production cache lock so a real worker pauses inside
        // prepareTile. The reset runs serially on a helper thread solely so
        // this thread can release the lock once stopWorkers reaches its join.
        // Restart the fixture pool with the gate already held. This prevents
        // mistaking a worker returning a previous cache hit for a blocked one.
        // Queued source work and GPU/finalizer state are preserved.
        manager.stopWorkers();
        std::unique_lock cacheGate(manager.tileCacheMutex_);
        auto pending=std::make_shared<PendingTile>();pending->coord={63,0};pending->mesh=mesh;
        manager.tileCacheLru_.push_front(pending->coord);
        manager.tileCache_[pending->coord]={pending,0,manager.tileCacheLru_.begin()};
        require(manager.enqueueTile(63,0),"Busy worker enqueue failed");
        manager.startWorkers();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        int observedPreparing=0;
        do{
            {std::lock_guard lock(manager.queueMutex);observedPreparing=manager.preparingTiles_;}
            if(observedPreparing>0)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }while(std::chrono::steady_clock::now()<deadline);
        require(observedPreparing>0,"No real worker reached cache barrier");
        std::atomic<bool> finished{false};std::exception_ptr resetError;
        std::thread reset([&]{try{manager.setMapName(name);}catch(...){resetError=std::current_exception();}finished.store(true);});
        const auto joinDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(manager.workerRunning.load() && !finished.load() && std::chrono::steady_clock::now()<joinDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool waited=!manager.workerRunning.load() && !finished.load();
        cacheGate.unlock();reset.join();
        if(resetError)std::rethrow_exception(resetError);
        require(waited,"Map reset did not wait at busy worker join barrier");
        return {{"fixture_pool_started_under_cache_gate",true},{"observed_preparing_workers",observedPreparing},{"reset_waited_for_worker",waited},
                {"workers_resumed",manager.workerRunning.load()}};
    }
    static void enqueueReal(TerrainManager& manager,int x,int y){
        require(manager.enqueueTile(x,y),"Real ADT enqueue failed");
    }
    static glm::vec3 realCenter(TerrainManager& manager){
        require(!manager.loadedTiles.empty(),"No loaded real tile");
        const auto& tile=*manager.loadedTiles.begin()->second;
        const auto& v=tile.mesh.chunks[128].vertices.at(0);
        return {v.position[0],v.position[1],v.position[2]};
    }
    static void pump(TerrainManager& manager){manager.processReadyTiles();}
    static Json snapshot(TerrainManager& manager){
        Json result={{"loaded_tiles",manager.loadedTiles.size()},
            {"finalizing_tiles",manager.finalizingTiles_.size()},
            {"workers_active",manager.workerRunning.load()},{"map",manager.mapName}};
        {
            std::lock_guard lock(manager.queueMutex);
            result["load_queue"]=manager.loadQueue.size();result["ready_queue"]=manager.readyQueue.size();
            result["pending_tiles"]=manager.pendingTiles.size();result["preparing_tiles"]=manager.preparingTiles_;
        }
        {
            std::lock_guard lock(manager.tileCacheMutex_);result["cached_tiles"]=manager.tileCache_.size();
        }
        return result;
    }
    static bool empty(TerrainManager& manager){
        auto s=snapshot(manager);
        return s["loaded_tiles"]==0 && s["finalizing_tiles"]==0 && s["load_queue"]==0 &&
            s["ready_queue"]==0 && s["pending_tiles"]==0 && s["preparing_tiles"]==0 && s["cached_tiles"]==0;
    }
};
}
