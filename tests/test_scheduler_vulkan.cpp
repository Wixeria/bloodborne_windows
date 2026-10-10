// Real GPU ordering across 0.4 recording workers, flushes and direct segments.
#include "gpu_release_headers.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include <vk_mem_alloc.h>
#include <cstdio>
#include <cstdlib>

extern "C" void runtime_restart(void) { std::abort(); }
int main() {
    Vulkan::Instance instance(0, false);
    VkBuffer raw{}; VmaAllocation allocation{}; VmaAllocationInfo mapped{};
    const VkBufferCreateInfo info{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size=4,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    const VmaAllocationCreateInfo memory{.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT,.usage=VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
    assert(vmaCreateBuffer(instance.GetAllocator(),&info,&memory,&raw,&allocation,&mapped)==VK_SUCCESS);
    assert(mapped.pMappedData);
    const vk::Buffer buffer{raw};
    {
        Vulkan::Scheduler scheduler(instance,true);
        const auto fill=[buffer](vk::CommandBuffer cmd,u32 value) {
            const vk::MemoryBarrier2 before{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
                .srcAccessMask=vk::AccessFlagBits2::eMemoryWrite,
                .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,
                .dstAccessMask=vk::AccessFlagBits2::eTransferWrite};
            cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&before});
            cmd.fillBuffer(buffer,0,4,value);
        };
        for (u32 frame=0;frame<8;++frame) {
            for (u32 i=1;i<=400;++i) {
                const u32 value=frame*400+i;
                scheduler.Record([fill,value](vk::CommandBuffer cmd){fill(cmd,value);});
                // Commands recorded by the producer must keep their position
                // between the worker segments before and after them.
                if (i%83==0) fill(scheduler.CommandBuffer(),value);
            }
            scheduler.Flush();
        }
        scheduler.Record([](vk::CommandBuffer cmd) {
            const vk::MemoryBarrier2 read{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,
                .srcAccessMask=vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask=vk::PipelineStageFlagBits2::eHost,
                .dstAccessMask=vk::AccessFlagBits2::eHostRead};
            cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&read});
        });
        scheduler.Finish();
        assert(vmaInvalidateAllocation(instance.GetAllocator(),allocation,0,4)==VK_SUCCESS);
        assert(*static_cast<u32*>(mapped.pMappedData)==3200);
    }
    vmaDestroyBuffer(instance.GetAllocator(),raw,allocation);
    std::puts("PASS: GPU order across worker, direct and submission segments");
}
