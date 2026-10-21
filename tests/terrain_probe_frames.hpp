// Asset-free frame resources for the terrain lifetime probe only.
#pragma once
#include "rendering/vk_texture.hpp"
#include "rendering/vk_frame_data.hpp"
#include "rendering/camera.hpp"
#include <cstring>

struct TerrainProbeFrames {
    wowee::rendering::VkContext& ctx;
    wowee::rendering::AllocatedBuffer ubo{};
    wowee::rendering::AllocatedImage volume{};
    wowee::rendering::VkTexture depth, neutral;
    VkDescriptorPool pool=VK_NULL_HANDLE;
    VkDescriptorSet set=VK_NULL_HANDLE;
    wowee::rendering::Camera camera;
    explicit TerrainProbeFrames(wowee::rendering::VkContext& c):ctx(c){}
    ~TerrainProbeFrames(){
        ctx.waitIdle("terrain probe frame resources");
        if(pool)vkDestroyDescriptorPool(ctx.getDevice(),pool,nullptr);
        wowee::rendering::destroyBuffer(ctx.getAllocator(),ubo);
        wowee::rendering::destroyImage(ctx.getDevice(),ctx.getAllocator(),volume);
    }
    void initialize(VkDescriptorSetLayout layout){
        using namespace wowee::rendering;
        require(depth.createDepth(ctx,1,1),"Probe shadow image failed");
        ctx.immediateSubmit([&](VkCommandBuffer cmd){
            wowee::rendering::transitionImageLayout(cmd,depth.getImage(),
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        });
        depth.createSampler(ctx.getDevice(),VK_FILTER_NEAREST,VK_FILTER_NEAREST,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        const uint8_t rgba[4]={255,255,255,255};
        require(neutral.upload(ctx,rgba,1,1,VK_FORMAT_R8G8B8A8_UNORM,false),"Probe neutral image failed");
        neutral.createSampler(ctx.getDevice(),VK_FILTER_NEAREST,VK_FILTER_NEAREST,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        VkImageCreateInfo image{.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType=VK_IMAGE_TYPE_3D;image.format=VK_FORMAT_R8G8B8A8_UNORM;
        image.extent={1,1,1};image.mipLevels=1;image.arrayLayers=1;
        image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
        image.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo alloc{};alloc.usage=VMA_MEMORY_USAGE_GPU_ONLY;
        require(vmaCreateImage(ctx.getAllocator(),&image,&alloc,&volume.image,&volume.allocation,nullptr)==VK_SUCCESS,"Probe 3D image failed");
        VkImageViewCreateInfo view{.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image=volume.image;view.viewType=VK_IMAGE_VIEW_TYPE_3D;view.format=image.format;
        view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        require(vkCreateImageView(ctx.getDevice(),&view,nullptr,&volume.imageView)==VK_SUCCESS,"Probe 3D view failed");
        ctx.immediateSubmit([&](VkCommandBuffer cmd){
            transitionImageLayout(cmd,volume.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        });
        GPUPerFrameData data{};
        data.view=glm::lookAt(glm::vec3(4,4,30),glm::vec3(4,4,0),glm::vec3(0,1,0));
        data.projection=glm::orthoRH_ZO(-6.f,6.f,-6.f,6.f,0.1f,100.f);
        data.projection[1][1]*=-1;
        data.lightSpaceMatrix=glm::mat4(1);data.ambientColor=glm::vec4(1);
        data.viewPos=glm::vec4(4,4,30,1);data.fogParams=glm::vec4(1000,2000,0,0);
        ubo=uploadBuffer(ctx,&data,sizeof(data),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        require(ubo.buffer,"Probe UBO failed");
        const VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4}};
        VkDescriptorPoolCreateInfo pci{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pci.maxSets=1;pci.poolSizeCount=2;pci.pPoolSizes=sizes;
        require(vkCreateDescriptorPool(ctx.getDevice(),&pci,nullptr,&pool)==VK_SUCCESS,"Probe descriptor pool failed");
        VkDescriptorSetAllocateInfo sai{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        sai.descriptorPool=pool;sai.descriptorSetCount=1;sai.pSetLayouts=&layout;
        require(vkAllocateDescriptorSets(ctx.getDevice(),&sai,&set)==VK_SUCCESS,"Probe descriptor set failed");
        VkDescriptorBufferInfo bi{ubo.buffer,0,sizeof(data)};
        VkDescriptorImageInfo images[4]={depth.descriptorInfo(),neutral.descriptorInfo(),neutral.descriptorInfo(),neutral.descriptorInfo()};
        images[0].imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        images[1].imageView=volume.imageView;
        std::array<VkWriteDescriptorSet,5> writes{};
        for(uint32_t i=0;i<5;++i){
            writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;
            writes[i].dstBinding=i;writes[i].descriptorCount=1;
            writes[i].descriptorType=i?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            if(i)writes[i].pImageInfo=&images[i-1];else writes[i].pBufferInfo=&bi;
        }
        vkUpdateDescriptorSets(ctx.getDevice(),writes.size(),writes.data(),0,nullptr);
        camera.setPosition({4,4,30});
    }
    void aimAt(glm::vec3 center){
        using namespace wowee::rendering;
        require(ctx.waitIdle("probe camera update")==VK_SUCCESS,"Camera fence failed");
        GPUPerFrameData data{};
        const auto eye=center+glm::vec3(0,0,700);
        data.view=glm::lookAt(eye,center,glm::vec3(0,1,0));
        data.projection=glm::orthoRH_ZO(-400.f,400.f,-300.f,300.f,0.1f,2000.f);
        data.projection[1][1]*=-1;data.lightSpaceMatrix=glm::mat4(1);
        data.ambientColor=glm::vec4(1);data.viewPos=glm::vec4(eye,1);
        data.fogParams=glm::vec4(10000,20000,0,0);
        auto staging=uploadBuffer(ctx,&data,sizeof(data),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        ctx.immediateSubmit([&](VkCommandBuffer cmd){
            VkBufferCopy region{0,0,sizeof(data)};vkCmdCopyBuffer(cmd,staging.buffer,ubo.buffer,1,&region);
        });
        destroyBuffer(ctx.getAllocator(),staging);camera.setPosition(eye);
    }
    void drainRetirements(wowee::rendering::TerrainRenderer& terrain){
        int frames=0;
        while(ctx.hasPendingRetirements() && frames++<256)frame(terrain);
        require(!ctx.hasPendingRetirements(),"Budgeted terrain retirement did not converge within 256 frames");
    }
    // The callback retires resources after commands reference them but before
    // submission. Destruction must wait for every real frame slot's fence.
    void frame(wowee::rendering::TerrainRenderer& terrain,const std::function<void()>& recorded={}){
        SDL_PumpEvents();uint32_t imageIndex=0;
        VkCommandBuffer cmd=ctx.beginFrame(imageIndex);require(cmd,"Probe beginFrame failed");
        VkClearValue clears[4]{};clears[0].color={{0,0,0,1}};clears[1].depthStencil={1,0};clears[3].depthStencil={1,0};
        VkRenderPassBeginInfo pass{.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass.renderPass=ctx.getImGuiRenderPass();pass.framebuffer=ctx.getSwapchainFramebuffers()[imageIndex];
        pass.renderArea.extent=ctx.getSwapchainExtent();
        pass.clearValueCount=ctx.getMsaaSamples()==VK_SAMPLE_COUNT_1_BIT?2:(ctx.getDepthResolveImageView()?4:3);
        pass.pClearValues=clears;vkCmdBeginRenderPass(cmd,&pass,VK_SUBPASS_CONTENTS_INLINE);
        const auto size=ctx.getSwapchainExtent();
        VkViewport viewport{0,0,float(size.width),float(size.height),0,1};VkRect2D scissor{{0,0},size};
        vkCmdSetViewport(cmd,0,1,&viewport);vkCmdSetScissor(cmd,0,1,&scissor);
        terrain.render(cmd,set,camera);vkCmdEndRenderPass(cmd);
        if(recorded)recorded();ctx.endFrame(cmd,imageIndex);
        require(!ctx.isDeviceLost(),"Probe frame submission lost device");
        require(!ctx.isSwapchainDirty(),"Probe swapchain became unavailable");
    }
};
