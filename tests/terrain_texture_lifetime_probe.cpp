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
static pipeline::TerrainMesh generatedTile(){
    pipeline::TerrainMesh mesh{};mesh.validChunkCount=4;
    for(int id=0;id<4;++id){
        auto& c=mesh.chunks[id];c.chunkX=id;c.chunkY=0;c.worldX=float(id*2);c.worldY=0;c.worldZ=0;
        c.vertices.resize(4);c.indices={0,1,2,0,2,3};
        for(int v=0;v<4;++v){c.vertices[v].position[0]=c.worldX+float(v==1||v==2);c.vertices[v].position[1]=float(v>=2);c.vertices[v].normal[2]=1.f;}
        c.layers.resize(4);
        for(int layer=0;layer<4;++layer){c.layers[layer].textureId=0;c.layers[layer].flags=0;if(layer)c.layers[layer].alphaData.assign(64*64,uint8_t(40+layer*30+id));}
    }
    return mesh;
}
int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"Usage: terrain_alpha_probe RESULT.json\n";return 2;}
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
        terrain.shutdown();settle(ctx);vkDestroyDescriptorSetLayout(ctx.getDevice(),perFrame,nullptr);ctx.shutdown();
        result["state"]=passed?"pass":"obsolete_alpha_allocations_retained";
        result["exit"]=passed?0:42;
        std::ofstream(argv[1])<<result.dump(2)<<'\n';std::cout<<result.dump()<<'\n';return passed?0:42;
    }catch(const std::exception& e){result["state"]="probe_failed";result["error"]=e.what();result["exit"]=1;std::ofstream(argv[1])<<result.dump(2)<<'\n';std::cerr<<result.dump()<<'\n';return 1;}
}
