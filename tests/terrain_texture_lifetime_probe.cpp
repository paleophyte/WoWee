// Asset-free probe of the actual terrain renderer, Vulkan allocator and GPU uploads.
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
            c.vertices.resize(145);
            for(int y=0;y<9;++y)for(int x=0;x<9;++x){
                auto& v=c.vertices[y*9+x];v.position[0]=float(x);v.position[1]=float(y);v.normal[2]=1.f;
            }
            for(int y=0;y<8;++y)for(int x=0;x<8;++x){
                const uint32_t centre=81+y*8+x;
                auto& v=c.vertices[centre];v.position[0]=x+0.5f;v.position[1]=y+0.5f;v.normal[2]=1.f;
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
int main(int argc,char** argv){
    if(argc<2 || argc>3 || (argc==3 && std::string(argv[2])!="--streaming-pressure")){std::cerr<<"Usage: terrain_alpha_probe RESULT.json [--streaming-pressure]\n";return 2;}
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
        if(argc==3) passed=streamingPressure(ctx,terrain,result,argv[1]) && passed;
        terrain.shutdown();settle(ctx);vkDestroyDescriptorSetLayout(ctx.getDevice(),perFrame,nullptr);ctx.shutdown();
        result["state"]=passed?"pass":"obsolete_alpha_allocations_retained";
        result["exit"]=passed?0:42;
        std::ofstream(argv[1])<<result.dump(2)<<'\n';std::cout<<result.dump()<<'\n';return passed?0:42;
    }catch(const std::exception& e){result["state"]="probe_failed";result["error"]=e.what();result["exit"]=1;std::ofstream(argv[1])<<result.dump(2)<<'\n';std::cerr<<result.dump()<<'\n';return 1;}
}
