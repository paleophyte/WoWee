#pragma once
#include "rendering/terrain_manager.hpp"
#include "rendering/terrain_renderer.hpp"
#include "rendering/m2_renderer.hpp"
#include "rendering/wmo_renderer.hpp"
#include "rendering/water_renderer.hpp"
#include "rendering/character_renderer.hpp"
#include "rendering/camera_controller.hpp"
#include "pipeline/asset_manager.hpp"
#include <imgui.h>
#include <thread>
#include "rendering/loading_screen.hpp"

// Offline scene fixture: production rendering and tile workers, no fabricated
// network snapshot, character, quest result or invocation of world entry.
static void sceneTerrainProbe(wowee::core::Application& app,
    const std::filesystem::path& tracePath,
    const std::function<void(const char*,nlohmann::json)>& record,bool loadingPresentation=false){
    using namespace wowee;
    using json=nlohmann::json;
    auto check=[](bool ok,const char* error){if(!ok)throw std::runtime_error(error);};
    for(const char* key:{"WOWEE_SKIP_ALL_RENDER","WOWEE_SKIP_WMO","WOWEE_SKIP_M2","WOWEE_SKIP_TERRAIN","WOWEE_SKIP_SKY","WOWEE_SKIP_CHARS"})
        check(std::getenv(key)==nullptr,"Scene probe refuses render-skip overrides");
    auto* renderer=app.getRenderer();auto* assets=app.getAssetManager();
    check(renderer->initializeRenderers(assets,"Azeroth"),"Scene renderer initialization failed");
    auto* manager=renderer->getTerrainManager();auto* terrain=renderer->getTerrainRenderer();
    auto* m2=renderer->getM2Renderer();auto* wmo=renderer->getWMORenderer();
    auto* water=renderer->getWaterRenderer();auto* ctx=renderer->getVkContext();
    check(manager && terrain && m2 && wmo && water,"Scene sub-renderer missing");
    // Bound file requests explicitly. The normal worker/finalization and scene
    // rendering methods remain unchanged; streaming discovery is fixture-owned.
    manager->setStreamingEnabled(false);renderer->setTerrainEnabled(true);
    renderer->getCameraController()->setEnabled(false);
    auto snapshot=[&]{
        VmaTotalStatistics stats{};vmaCalculateStatistics(ctx->getAllocator(),&stats);
        return json{{"allocations",stats.total.statistics.allocationCount},
            {"allocation_bytes",stats.total.statistics.allocationBytes},
            {"loaded_tiles",manager->getLoadedTileCount()},{"remaining_tiles",manager->getRemainingTileCount()},
            {"terrain_chunks",terrain->getChunkCount()},{"terrain_draws",terrain->getRenderedChunkCount()},
            {"m2_models",m2->getModelCount()},{"m2_instances",m2->getInstanceCount()},
            {"m2_draws",m2->getDrawCallCount()},{"wmo_models",wmo->getModelCount()},
            {"wmo_instances",wmo->getInstanceCount()},{"wmo_draws",wmo->getDrawCallCount()},
            {"water_surfaces",water->getSurfaceCount()}};
    };
    auto frame=[&](bool pump=true){
        app.beatWatchdog();SDL_Event event;
        while(SDL_PollEvent(&event))check(event.type!=SDL_EVENT_QUIT,"Scene window closed");
        game::GameHandlerTestAccess::prepareFrame(app,.016f);
        if(pump)manager->processReadyTiles();
        renderer->update(.016f);ImGui::Render();
        renderer->beginFrame();renderer->renderWorld(app.getWorld());renderer->endFrame();
        check(!ctx->isDeviceLost() && !ctx->isSwapchainDirty(),"Scene device/swapchain failed");
    };
    auto enqueue=[&](const std::string& name,int x,int y){
        const auto path="World\\Maps\\"+name+"\\"+name+"_"+std::to_string(x)+"_"+std::to_string(y)+".adt";
        if(!assets->fileExists(path))return false;
        check(manager->enqueueTile(x,y),"Scene tile enqueue failed");return true;
    };
    json warmBaseline;
    for(int cycle=0;cycle<2;++cycle){
        for(int map=0;map<2;++map){
            const std::string name=map?"Kalimdor":"Azeroth";const int x=map?41:33,y=39;
            renderer->setActiveMapName(name);
            renderer->getCamera()->setPosition({-3800.f,map?-4500.f:-800.f,100.f});
            renderer->getCamera()->setRotation(180.f,-25.f);
            int expected=0;
            for(int tx=x-3;tx<=x+3;++tx)for(int ty=y-3;ty<=y+3;++ty)expected+=enqueue(name,tx,ty);
            record("scene_loading",{{"cycle",cycle},{"map",name},{"expected_tiles",expected}});
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
            auto nextReport=std::chrono::steady_clock::now();
            while(manager->getLoadedTileCount()!=expected || manager->getRemainingTileCount()!=0){
                check(std::chrono::steady_clock::now()<deadline,"Scene load timed out");frame();
                if(std::chrono::steady_clock::now()>=nextReport){record("scene_loading_sample",snapshot());nextReport=std::chrono::steady_clock::now()+std::chrono::seconds(2);}
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            for(int i=0;i<4;++i)frame();
            check(terrain->getRenderedChunkCount()>0 && m2->getDrawCallCount()>0 && wmo->getDrawCallCount()>0,"Scene terrain/M2/WMO not drawn");
            check(m2->getInstanceCount()>0 && wmo->getInstanceCount()>0 && water->getSurfaceCount()>0,"Scene missing real M2/WMO/water content");
            auto loaded=snapshot();loaded["cycle"]=cycle;loaded["map"]=name;record("scene_loaded",loaded);
            const auto image=tracePath.parent_path()/("scene-"+std::to_string(cycle)+"-"+name+".png");
            check(renderer->captureScreenshot(image.string()),"Scene screenshot failed");record("scene_screenshot",{{"path",image.string()}});
            // Queue a real outer strip, then interrupt it without finalizing it.
            int extra=0;for(int ty=y-3;ty<=y+3;++ty)extra+=enqueue(name,x-4,ty);
            check(extra>0 && manager->getRemainingTileCount()>0,"No real source work pending at scene reset");
            frame(false);record("scene_reset_with_pending_work",snapshot());
            std::unique_ptr<rendering::LoadingScreen> loading;
            if(loadingPresentation){
                loading=std::make_unique<rendering::LoadingScreen>();
                loading->setVkContext(ctx);loading->setSDLWindow(app.getWindow()->getSDLWindow());
                check(loading->initialize(),"Offline loading-screen initialization failed");
                loading->setZoneName(map?"Dustwallow Marsh":"Wetlands");
                loading->setStatus("Waiting for world position...");loading->setProgress(.02f);
                // Present once before cleanup, as normal world entry does.
                app.beatWatchdog();loading->render();
            }
            // Same ordering as WorldLoader's old-map cleanup. No thread gate,
            // fake cache input or forced generated placements.
            wmo->clearAll();m2->clear();
            if(auto* character=renderer->getCharacterRenderer())character->clear();
            manager->softReset();
            if(loading){
                // Present after retirement too: beginFrame may execute deferred
                // destruction once both source frame fences have been waited.
                for(int i=0;i<12;++i){
                    app.beatWatchdog();SDL_PumpEvents();const auto start=std::chrono::steady_clock::now();
                    loading->render();
                    const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                    auto row=snapshot();row["cycle"]=cycle;row["map"]=name;row["sequence"]=i;row["presentation_ms"]=elapsed;
                    record("loading_screen_presented",row);
                    check(!ctx->isDeviceLost() && !ctx->isSwapchainDirty(),"Loading-screen presentation failed");
                }
                loading->shutdown();
            }
            frame(false);frame(false);
            check(manager->getLoadedTileCount()==0 && manager->getRemainingTileCount()==0 && terrain->getChunkCount()==0,"Scene reset retained old map work");
            check(m2->getModelCount()==0 && m2->getInstanceCount()==0 && wmo->getModelCount()==0 && wmo->getInstanceCount()==0 && water->getSurfaceCount()==0,"Scene reset retained model/water content");
            auto cleared=snapshot();
            if(cycle==0 && map==1)warmBaseline=cleared;
            if(cycle>0)check(cleared["allocations"]==warmBaseline["allocations"] && cleared["allocation_bytes"]==warmBaseline["allocation_bytes"],"Scene allocations grew after warm cycle");
            cleared["cycle"]=cycle;cleared["map"]=name;record("scene_cleared",cleared);
        }
    }
}
