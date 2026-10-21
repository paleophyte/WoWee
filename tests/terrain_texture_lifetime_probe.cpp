// Offline probe of terrain uploads and lifetime; explicit opt-in real-data coverage.
#include "rendering/vk_context.hpp"
#include "rendering/vk_utils.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/terrain_renderer.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/terrain_mesh.hpp"
#include "core/logger.hpp"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <string>
#include <fstream>
#include <iostream>
#include <stdexcept>
using Json=nlohmann::json;
using namespace wowee;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
#include "terrain_probe_frames.hpp"
#include "terrain_probe_manager.hpp"
#include "terrain_probe_blp.hpp"
static uint32_t allocations(rendering::VkContext& ctx){
    VmaTotalStatistics stats{};vmaCalculateStatistics(ctx.getAllocator(),&stats);
    return stats.total.statistics.allocationCount;
}
static void settle(rendering::VkContext& ctx){
    ctx.waitAllUploads();require(ctx.waitIdle("terrain texture probe")==VK_SUCCESS,"Device wait failed");
    ctx.flushDeferredCleanup();
}
static pipeline::TerrainMesh generatedTile(int count=4){
    pipeline::TerrainMesh mesh{};mesh.validChunkCount=count;
    for(int id=0;id<count;++id){
        auto& c=mesh.chunks[id];c.chunkX=id%16;c.chunkY=id/16;c.worldX=float(id*2);c.worldY=0;c.worldZ=0;
        if(count==4){
            c.vertices.resize(4);c.indices={0,1,2,0,2,3};
            for(int v=0;v<4;++v){c.vertices[v].position[0]=c.worldX+float(v==1||v==2);c.vertices[v].position[1]=float(v>=2);c.vertices[v].normal[2]=1.f;}
        }else{
            // The ADT topology: 9x9 outer vertices plus 8x8 cell centres,
            // with four triangles per cell (145 vertices, 768 indices).
            c.worldX=float(c.chunkX*8);c.worldY=float(c.chunkY*8);
            c.vertices.resize(145);
            for(int y=0;y<9;++y)for(int x=0;x<9;++x){
                auto& v=c.vertices[y*9+x];v.position[0]=c.worldX+float(x);v.position[1]=c.worldY+float(y);v.normal[2]=1.f;
            }
            for(int y=0;y<8;++y)for(int x=0;x<8;++x){
                const uint32_t centre=81+y*8+x;
                auto& v=c.vertices[centre];v.position[0]=c.worldX+x+0.5f;v.position[1]=c.worldY+y+0.5f;v.normal[2]=1.f;
                const uint32_t a=y*9+x,b=a+1,d=a+9,e=d+1;
                c.indices.insert(c.indices.end(),{a,b,centre,b,e,centre,e,d,centre,d,a,centre});
            }
        }
        c.layers.resize(4);
        for(int layer=0;layer<4;++layer){c.layers[layer].textureId=0;c.layers[layer].flags=0;if(layer)c.layers[layer].alphaData.assign(64*64,uint8_t(40+layer*30+id));}
    }
    return mesh;
}
static Json memorySnapshot(rendering::VkContext& ctx){
    VmaTotalStatistics stats{};vmaCalculateStatistics(ctx.getAllocator(),&stats);
    const auto& s=stats.total.statistics;
    return {{"allocations",s.allocationCount},{"allocation_bytes",s.allocationBytes},
            {"blocks",s.blockCount},{"block_bytes",s.blockBytes}};
}
// Full 16x16 tiles, uploaded through the same 16-chunk asynchronous batches as
// TerrainManager. Bound residency and retirements rather than testing to OOM.
static bool streamingPressure(rendering::VkContext& ctx, rendering::TerrainRenderer& terrain,
                              Json& result, const char* output){
    const auto baseline=memorySnapshot(ctx);
    const auto tile=generatedTile(256);
    result["streaming_pressure"]={{"resident_tiles",9},{"chunks_per_tile",256},{"vertices_per_chunk",145},{"indices_per_chunk",768},
        {"alpha_images_per_chunk",3},{"alpha_extent",{64,64}},
        {"alpha_format","VK_FORMAT_R8_UNORM"},{"batch_chunks",16},
        {"baseline",baseline},{"samples",Json::array()}};
    result["state"]="streaming_pressure_running";
    auto& samples=result["streaming_pressure"]["samples"];
    auto sample=[&](const char* phase,int cycle,int tileId){
        Json row=memorySnapshot(ctx);row["phase"]=phase;row["cycle"]=cycle;
        row["tile"]=tileId;row["chunks"]=terrain.getChunkCount();samples.push_back(row);
        // Persist before the next allocation: an abort must retain the last
        // completed phase, even if destructors/exception handling cannot run.
        std::ofstream(output)<<result.dump(2)<<'\n';
        std::cout<<row.dump()<<std::endl;
    };
    sample("before_streaming",0,-1);
    for(int cycle=0;cycle<3;++cycle){
        for(int tileId=0;tileId<12;++tileId){
            if(tileId>=9){terrain.removeTile(tileId-9,cycle);sample("retired_before_replacement",cycle,tileId);}
            int next=0;
            for(int calls=0;next<256 && calls<32;++calls){
                const int before=next;
                terrain.loadTerrainIncremental(tile,{},tileId,cycle,next,16);
                require(next>before,"Streaming upload made no progress");
            }
            require(next==256,"Streaming upload exceeded bounded calls");
            sample("uploaded_before_settle",cycle,tileId);
            settle(ctx);
            require(terrain.getChunkCount()==size_t(std::min(tileId+1,9)*256),"Streaming lost resident chunks");
            sample("settled",cycle,tileId);
        }
        terrain.clear();sample("clear_before_fences",cycle,12);settle(ctx);
        sample("cleared",cycle,12);
        require(terrain.getChunkCount()==0,"Streaming clear retained chunks");
        require(memorySnapshot(ctx)["allocations"]==baseline["allocations"],"Streaming clear retained allocations");
        require(memorySnapshot(ctx)["allocation_bytes"]==baseline["allocation_bytes"],"Streaming clear retained allocation bytes");
    }
    return true;
}
static bool framedRetirement(rendering::VkContext& ctx, rendering::TerrainRenderer& terrain,
                             VkDescriptorSetLayout layout,Json& result,const char* output,int residentTiles=2,bool largeTextures=false,bool parsedBLP=false){
    TerrainProbeFrames frames(ctx);frames.initialize(layout);terrain.setFrustumCulling(false);
    std::unordered_map<std::string,pipeline::BLPImage> textures;
    std::vector<std::string> paths;
    const int textureCount=largeTextures?residentTiles*4:4;
    const int textureExtent=largeTextures?512:256;
    result["framed_retirement"]={{"resident_tiles",residentTiles},{"tileset_images",textureCount},
        {"tileset_extent",{textureExtent,textureExtent}},{"tileset_format","RGBA8 with generated mipmaps"},
        {"samples",Json::array()}};
    result["state"]="generating_reusable_textures";
    std::ofstream(output)<<result.dump(2)<<'\n';
    for(int i=0;i<textureCount;++i){
        std::string path="generated_tileset_"+std::to_string(i);paths.push_back(path);
        auto& image=textures[path];
        if(parsedBLP){
            const int variant=i%4==3?2:i%4;
            const auto bytes=generatedBLP2(variant);
            image=pipeline::BLPLoader::load(bytes,i%4!=3);
            require(image.isValid(),"Generated BLP2 parser failed");
            require(image.isBlockCompressed()==(i%4!=3),"BLP parser used wrong storage path");
            const auto decoded=pipeline::BLPLoader::load(bytes,false);
            require(decoded.isValid() && decoded.data.size()==512*512*4,"BLP CPU decode failed");
            const auto pixels=image.isBlockCompressed()?pipeline::BLPLoader::decodeBaseLevel(image):image.data;
            require(pixels==decoded.data,"BLP block/decoded pixels disagree");
            const auto expected=variant==0?pipeline::BLPCompression::DXT1:(variant==1?pipeline::BLPCompression::DXT3:pipeline::BLPCompression::DXT5);
            require(image.compression==expected,"BLP parser selected wrong compression format");
            if(image.isBlockCompressed())require(image.mipmaps.size()==10,"BLP mip chain incomplete");
        }else{
            image.width=image.height=textureExtent;image.data.assign(textureExtent*textureExtent*4,uint8_t(70+i*40));
        }
    }
    if(parsedBLP)result["framed_retirement"]["tileset_format"]="Generated BLP2 DXT1/3/5 preserved plus DXT5 decoded to RGBA8";
    const auto allPaths=paths;
    const auto blockUploadsBefore=rendering::VkTexture::blockUploadTally();
    settle(ctx);const auto beforeTilesets=allocations(ctx);
    result["state"]="uploading_reusable_textures";
    result["framed_retirement"]["before_tilesets"]=memorySnapshot(ctx);
    std::ofstream(output)<<result.dump(2)<<'\n';
    terrain.uploadPreloadedTextures(textures);settle(ctx);
    require(allocations(ctx)==beforeTilesets+textures.size(),"Generated reusable textures did not all upload");
    if(parsedBLP){
        const auto uploaded=rendering::VkTexture::blockUploadTally();
        require(uploaded.textures-blockUploadsBefore.textures==uint64_t(textureCount/4*3),"BLP compressed uploads did not reach GPU path");
        result["framed_retirement"]["block_uploads"]={{"textures",uploaded.textures-blockUploadsBefore.textures},
            {"block_bytes",uploaded.blockBytes-blockUploadsBefore.blockBytes},
            {"decoded_bytes",uploaded.decodedBytes-blockUploadsBefore.decodedBytes}};
    }
    const auto baseline=memorySnapshot(ctx);
    auto tile=generatedTile(256);
    for(auto& chunk:tile.chunks)for(int i=0;i<4;++i)chunk.layers[i].textureId=i;
    result["framed_retirement"]["baseline"]=baseline;
    result["state"]="framed_retirement_running";
    auto sample=[&](const char* phase,int cycle){
        auto row=memorySnapshot(ctx);row["phase"]=phase;row["cycle"]=cycle;
        row["chunks"]=terrain.getChunkCount();result["framed_retirement"]["samples"].push_back(row);
        std::ofstream(output)<<result.dump(2)<<'\n';std::cout<<row.dump()<<std::endl;
    };
    sample("before_frames",0);
    for(int cycle=0;cycle<3;++cycle){
        for(int tileId=0;tileId<residentTiles;++tileId){
            if(largeTextures)paths.assign(allPaths.begin()+tileId*4,allPaths.begin()+tileId*4+4);
            // Persist before any allocation that might abort inside Metal.
            sample("before_tile_upload",cycle);
            if(residentTiles==2){
                require(terrain.loadTerrain(tile,paths,tileId,cycle),"Framed tile failed");
            }else{
                int next=0;
                for(int call=0;next<256 && call<32;++call){
                    const int before=next;
                    terrain.loadTerrainIncremental(tile,paths,tileId,cycle,next,16);
                    require(next>before,"Framed incremental upload made no progress");
                }
                require(next==256,"Framed incremental upload exceeded bounded calls");
                frames.frame(terrain);
                require(terrain.getRenderedChunkCount()==(tileId+1)*256,"Incremental tile did not draw all chunks");
                sample("tile_uploaded_and_drawn",cycle);
            }
        }
        if(residentTiles==2)settle(ctx);
        require(terrain.getChunkCount()==size_t(residentTiles*256),"Framed generated chunks missing");
        const auto loaded=allocations(ctx);
        frames.frame(terrain,[&]{
            require(terrain.getRenderedChunkCount()==residentTiles*256,"Probe did not record every terrain draw");
            terrain.removeTile(0,cycle);
            require(allocations(ctx)==loaded,"Framed tile destroyed before submission");
            sample("retired_before_submit",cycle);
        });
        frames.frame(terrain);
        require(allocations(ctx)==loaded,"Framed resources destroyed before both slot fences");
        sample("one_slot_waited",cycle);
        frames.frame(terrain);
        require(allocations(ctx)==uint32_t(baseline["allocations"])+(loaded-uint32_t(baseline["allocations"]))*(residentTiles-1)/residentTiles,"Frame slots did not retire exactly one tile");
        require(terrain.getRenderedChunkCount()==(residentTiles-1)*256,"Framed retirement damaged live tile");
        sample("both_slots_waited",cycle);
        frames.frame(terrain,[&]{terrain.clear();sample("clear_before_submit",cycle);});
        frames.frame(terrain);frames.frame(terrain);
        sample("cleared_through_frames",cycle);
        require(memorySnapshot(ctx)["allocations"]==baseline["allocations"],"Framed clear retained allocations");
        require(memorySnapshot(ctx)["allocation_bytes"]==baseline["allocation_bytes"],"Framed clear retained allocation bytes");
    }
    settle(ctx);return true;
}
static bool workerTransitions(rendering::VkContext& ctx,rendering::TerrainRenderer& terrain,
                              pipeline::AssetManager& assets,VkDescriptorSetLayout layout,
                              Json& result,const char* output){
    using Access=rendering::TerrainManagerTestAccess;
    TerrainProbeFrames frames(ctx);frames.initialize(layout);terrain.setFrustumCulling(false);
    rendering::TerrainManager manager;require(manager.initialize(&assets,&terrain),"Terrain manager initialization failed");
    std::unordered_map<std::string,pipeline::BLPImage> textures;std::vector<std::string> paths;
    for(int i=0;i<4;++i){
        const auto path="generated_worker_tileset_"+std::to_string(i);paths.push_back(path);
        auto& image=textures[path];image.width=image.height=256;image.data.assign(256*256*4,127);
    }
    terrain.uploadPreloadedTextures(textures);settle(ctx);const auto baseline=memorySnapshot(ctx);
    auto tile=generatedTile(256);for(auto& chunk:tile.chunks)for(int i=0;i<4;++i)chunk.layers[i].textureId=i;
    result["worker_transitions"]={{"baseline",baseline},{"input","Generated cached decoded tiles; real workers, queues, finalizer and map reset"},{"samples",Json::array()}};
    result["state"]="worker_transitions_running";
    auto sample=[&](const char* phase,int cycle){
        auto row=memorySnapshot(ctx);row["phase"]=phase;row["cycle"]=cycle;row["chunks"]=terrain.getChunkCount();
        row["manager"]=Access::snapshot(manager);result["worker_transitions"]["samples"].push_back(row);
        std::ofstream(output)<<result.dump(2)<<'\n';std::cout<<row.dump()<<std::endl;
    };
    for(int cycle=0;cycle<3;++cycle){
        manager.setMapName("GeneratedSource"+std::to_string(cycle));Access::seed(manager,tile,paths,textures,8);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        for(int step=0;terrain.getChunkCount()==0 && step<256;++step){
            require(std::chrono::steady_clock::now()<deadline,"Worker source finalization timed out");
            Access::pump(manager);frames.frame(terrain);std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(terrain.getChunkCount()>0,"Worker source did not upload terrain");
        require(Access::snapshot(manager)["loaded_tiles"]!=8,"Source finalized completely before interruption");
        sample("source_partially_finalized",cycle);
        const auto loaded=allocations(ctx);
        frames.frame(terrain,[&]{
            require(terrain.getRenderedChunkCount()>0,"Worker source did not record draws");
            auto barrier=Access::resetWithBusyWorker(manager,"GeneratedDestination"+std::to_string(cycle),tile);
            result["worker_transitions"]["busy_reset_barriers"].push_back(barrier);
            require(terrain.getChunkCount()==0,"Map reset retained GPU chunks");
            require(Access::empty(manager),"Map reset retained old worker/cache/finalizer state");
            require(allocations(ctx)==loaded,"Map reset freed recorded resources before submission");
            sample("map_reset_before_submit",cycle);
        });
        frames.frame(terrain);frames.frame(terrain);
        require(Access::empty(manager),"Old worker published after map reset");
        require(memorySnapshot(ctx)["allocations"]==baseline["allocations"],"Map reset retained allocations after frame fences");
        require(memorySnapshot(ctx)["allocation_bytes"]==baseline["allocation_bytes"],"Map reset retained allocation bytes");
        sample("destination_empty_after_fences",cycle);
        Access::seed(manager,tile,paths,textures,1);
        const auto arrivalDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        for(int step=0;Access::snapshot(manager)["loaded_tiles"]!=1 && step<256;++step){
            require(std::chrono::steady_clock::now()<arrivalDeadline,"Destination worker finalization timed out");
            Access::pump(manager);frames.frame(terrain);std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(Access::snapshot(manager)["loaded_tiles"]==1,"Fresh destination tile never finalized");
        require(terrain.getChunkCount()==256 && terrain.getRenderedChunkCount()==256,"Destination tile did not fully draw");
        sample("fresh_destination_drawn",cycle);
        frames.frame(terrain,[&]{manager.softReset();});frames.frame(terrain);frames.frame(terrain);
        require(Access::empty(manager),"Soft reset retained manager state");
        require(memorySnapshot(ctx)["allocations"]==baseline["allocations"],"Soft reset retained GPU allocations");
        sample("soft_reset_through_frames",cycle);
    }
    settle(ctx);return true;
}
static bool realTerrain(rendering::VkContext& ctx,rendering::TerrainRenderer& terrain,
                        pipeline::AssetManager& assets,VkDescriptorSetLayout layout,
                        Json& result,const char* output){
    using Access=rendering::TerrainManagerTestAccess;
    TerrainProbeFrames frames(ctx);frames.initialize(layout);terrain.setFrustumCulling(false);
    terrain.setViewDistance(2400);
    rendering::TerrainManager manager;require(manager.initialize(&assets,&terrain),"Real manager initialization failed");
    result["scope"]="Offline real ADT/BLP terrain and production workers/finalizer; no account, connection, M2/WMO/water rendering or gameplay";
    result["real_terrain"]={{"samples",Json::array()}};
    Json warmBaseline;
    for(int cycle=0;cycle<3;++cycle){
        for(int map=0;map<2;++map){
            const std::string name=map?"Kalimdor":"Azeroth";const int x=map?41:33,y=39;
            manager.setMapName(name);Access::enqueueReal(manager,x,y);
            result["state"]="real_terrain_loading";result["loading"]={{"cycle",cycle},{"map",name},{"x",x},{"y",y}};
            std::ofstream(output)<<result.dump(2)<<'\n';
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);
            while(Access::snapshot(manager)["loaded_tiles"]!=1){
                require(std::chrono::steady_clock::now()<deadline,"Real terrain finalization timed out");
                Access::pump(manager);frames.frame(terrain);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            const auto center=Access::realCenter(manager);frames.aimAt(center);frames.frame(terrain);
            require(terrain.getChunkCount()>0 && terrain.getRenderedChunkCount()>0,"Real terrain recorded no draws");
            auto row=memorySnapshot(ctx);row["cycle"]=cycle;row["map"]=name;
            row["chunks"]=terrain.getChunkCount();row["draws"]=terrain.getRenderedChunkCount();row["manager"]=Access::snapshot(manager);
            row["camera_target"]={center.x,center.y,center.z};
            frames.frame(terrain,[&]{manager.softReset();});frames.frame(terrain);frames.frame(terrain);
            require(Access::empty(manager),"Real reset retained worker or tile state");
            row["after_reset"]=memorySnapshot(ctx);result["real_terrain"]["samples"].push_back(row);
            std::ofstream(output)<<result.dump(2)<<'\n';std::cout<<row.dump()<<std::endl;
        }
        const auto cleared=memorySnapshot(ctx);
        if(cycle==0)warmBaseline=cleared;
        else{
            require(cleared["allocations"]==warmBaseline["allocations"],"Real terrain retained allocations after warm cycle");
            require(cleared["allocation_bytes"]==warmBaseline["allocation_bytes"],"Real terrain retained bytes after warm cycle");
        }
    }
    result["real_terrain"]["warm_baseline"]=warmBaseline;settle(ctx);return true;
}
int main(int argc,char** argv){
    if(argc<2 || argc>3 || (argc==3 && std::string(argv[2])!="--streaming-pressure" && std::string(argv[2])!="--framed-retirement" && std::string(argv[2])!="--framed-pressure" && std::string(argv[2])!="--texture-pressure" && std::string(argv[2])!="--worker-transitions" && std::string(argv[2])!="--blp-pressure" && std::string(argv[2])!="--real-terrain")){std::cerr<<"Usage: terrain_alpha_probe RESULT.json [--streaming-pressure|--framed-retirement|--framed-pressure|--texture-pressure|--worker-transitions|--blp-pressure|--real-terrain]\n";return 2;}
    Json result={{"scope","Actual GPU allocation lifetime; generated four-chunk tiles, no extracted assets, accounts or gameplay"}};
    try{
        require(SDL_Init(SDL_INIT_VIDEO),"SDL video initialization failed");
        struct Video{SDL_Window* window=nullptr;~Video(){if(window)SDL_DestroyWindow(window);SDL_Quit();}} video;
        video.window=SDL_CreateWindow("Terrain texture lifetime",320,240,SDL_WINDOW_VULKAN|SDL_WINDOW_HIDDEN);
        require(video.window,"Vulkan window creation failed");
        ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;
        struct Gui{~Gui(){ImGui::DestroyContext();}} gui;
        rendering::VkContext ctx;require(ctx.initialize(video.window),"Native VkContext initialization failed");
        VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(ctx.getPhysicalDevice(),&props);
        result["device"]={{"name",props.deviceName},{"vendor",props.vendorID},{"driver_version",props.driverVersion},{"api_version",props.apiVersion}};
        VkSamplerCreateInfo samplerInfo{};samplerInfo.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter=samplerInfo.minFilter=VK_FILTER_LINEAR;samplerInfo.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU=samplerInfo.addressModeV=samplerInfo.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.compareEnable=VK_TRUE;samplerInfo.compareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
        const auto shadowSampler=ctx.getOrCreateSampler(samplerInfo);
        std::array<VkDescriptorSetLayoutBinding,5> bindings{};
        for(uint32_t b=0;b<5;++b){bindings[b].binding=b;bindings[b].descriptorCount=1;bindings[b].descriptorType=b?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;bindings[b].stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;}
        bindings[1].pImmutableSamplers=&shadowSampler;
        const auto perFrame=rendering::createDescriptorSetLayout(ctx.getDevice(),std::vector<VkDescriptorSetLayoutBinding>(bindings.begin(),bindings.end()));require(perFrame,"Per-frame descriptor layout failed");
        pipeline::AssetManager assets;
        if(argc==3 && std::string(argv[2])=="--real-terrain"){
            const char* path=std::getenv("WOW_DATA_PATH");require(path && *path,"Real terrain requires explicit TBC manifest directory in WOW_DATA_PATH");
            require(assets.initialize(path),"Real terrain asset initialization failed");
            assets.setExpansionDataPath(path);result["asset_path"]=path;
        }
        rendering::TerrainRenderer terrain;require(terrain.initialize(&ctx,perFrame,&assets),"Native terrain renderer initialization failed");
        settle(ctx);const auto baseline=allocations(ctx);result["baseline_allocations"]=baseline;result["rounds"]=Json::array();
        const auto tile=generatedTile();bool passed=true;
        for(int round=0;round<3;++round){
            require(terrain.loadTerrain(tile,{},10+round,20),"Generated tile upload failed");settle(ctx);
            require(terrain.getChunkCount()==4,"Generated chunks missing");const auto loaded=allocations(ctx);
            terrain.clear();const auto beforeFences=allocations(ctx);
            require(beforeFences==loaded,"Terrain resources destroyed before GPU fences");
            settle(ctx);const auto cleared=allocations(ctx);
            require(terrain.getChunkCount()==0,"Map clear retained chunks");
            result["rounds"].push_back({{"round",round},{"loaded_allocations",loaded},{"cleared_allocations",cleared},{"before_fences_allocations",beforeFences}});
            passed=passed && cleared==baseline;
        }
        require(terrain.loadTerrain(tile,{},40,20),"First partial-test tile failed");
        require(terrain.loadTerrain(tile,{},41,20),"Second partial-test tile failed");settle(ctx);
        const auto two=allocations(ctx);require(terrain.getChunkCount()==8,"Two-tile chunks missing");
        terrain.removeTile(40,20);settle(ctx);const auto one=allocations(ctx);
        require(terrain.getChunkCount()==4,"Removing one tile damaged the other tile");
        result["partial_unload"]={{"two_tile_allocations",two},{"one_tile_allocations",one},{"remaining_chunks",terrain.getChunkCount()}};
        passed=passed && one==baseline+(two-baseline)/2;
        terrain.clear();settle(ctx);result["final_allocations"]=allocations(ctx);passed=passed && allocations(ctx)==baseline;
        if(argc==3 && std::string(argv[2])=="--streaming-pressure") passed=streamingPressure(ctx,terrain,result,argv[1]) && passed;
        if(argc==3 && std::string(argv[2])=="--framed-retirement") passed=framedRetirement(ctx,terrain,perFrame,result,argv[1]) && passed;
        if(argc==3 && std::string(argv[2])=="--framed-pressure") passed=framedRetirement(ctx,terrain,perFrame,result,argv[1],49) && passed;
        if(argc==3 && std::string(argv[2])=="--texture-pressure") passed=framedRetirement(ctx,terrain,perFrame,result,argv[1],49,true) && passed;
        if(argc==3 && std::string(argv[2])=="--worker-transitions") passed=workerTransitions(ctx,terrain,assets,perFrame,result,argv[1]) && passed;
        if(argc==3 && std::string(argv[2])=="--blp-pressure") passed=framedRetirement(ctx,terrain,perFrame,result,argv[1],49,true,true) && passed;
        if(argc==3 && std::string(argv[2])=="--real-terrain") passed=realTerrain(ctx,terrain,assets,perFrame,result,argv[1]) && passed;
        terrain.shutdown();settle(ctx);vkDestroyDescriptorSetLayout(ctx.getDevice(),perFrame,nullptr);ctx.shutdown();
        result["state"]=passed?"pass":"obsolete_alpha_allocations_retained";
        result["exit"]=passed?0:42;
        std::ofstream(argv[1])<<result.dump(2)<<'\n';std::cout<<result.dump()<<'\n';return passed?0:42;
    }catch(const std::exception& e){result["state"]="probe_failed";result["error"]=e.what();result["exit"]=1;std::ofstream(argv[1])<<result.dump(2)<<'\n';std::cerr<<result.dump()<<'\n';return 1;}
}
