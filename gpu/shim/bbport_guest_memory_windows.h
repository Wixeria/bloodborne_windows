// SPDX-License-Identifier: GPL-2.0-or-later
// Windows sections replace Linux memfd/dma-buf; imports stay alive for the runtime pool's lifetime.
#include "bbport_guest_memory.h"
#include "bblayer_gpu_memory.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
extern "C" void* runtime_memory_backing_pointer(std::uint64_t phys);
extern "C" void runtime_memory_set_guest_chunk_allocator(int (*)(std::uint64_t, std::uint64_t));
namespace BbGuestMemory {
namespace {
using u64=std::uint64_t;
constexpr std::size_t MaxChunks=256;
vk::Device device;
vk::PhysicalDeviceMemoryProperties memory_properties;
std::mutex mutex;
std::array<Chunk*,MaxChunks> chunks{};
std::size_t chunk_count=0;
std::atomic<u64> chunk_bytes{0};
vk::BufferUsageFlags ChunkBufferUsage() {
    vk::BufferUsageFlags usage =
        vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eUniformBuffer |
        vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
        vk::BufferUsageFlagBits::eIndirectBuffer;
    if (LayerMemory()) {
        usage |= vk::BufferUsageFlagBits::eUniformTexelBuffer |
                 vk::BufferUsageFlagBits::eStorageTexelBuffer |
                 vk::BufferUsageFlagBits::eShaderDeviceAddress;
    }
    return usage;
}

/// A new chunk becomes a source of the layer's memory module.
void RegisterChunk(Chunk* chunk) {
    if (!LayerMemory()) {
        return;
    }
    const vk::DeviceAddress address = device.getBufferAddress({.buffer = chunk->buffer});
    BbLayer::GpuMemory::Get().AddGuestSource(chunk->phys, chunk->size, chunk->buffer, address,
                                             chunk);
}

vk::DeviceSize import_alignment = 4096;

std::uint32_t FindHostType(std::uint32_t bits) {
    // Cached host memory first, then any host-visible type the import allows.
    for (const bool cached : {true, false}) {
        for (std::uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
            const auto flags = memory_properties.memoryTypes[i].propertyFlags;
            if (!(bits & (1u << i)) || !(flags & vk::MemoryPropertyFlagBits::eHostVisible) || !(flags & vk::MemoryPropertyFlagBits::eHostCoherent) ||
                (flags & vk::MemoryPropertyFlagBits::eDeviceLocal) ||
                (cached && !(flags & vk::MemoryPropertyFlagBits::eHostCached))) {
                continue;
            }
            return i;
        }
    }
    return ~0u;
}

constexpr vk::BufferUsageFlags ChunkUsage =
    vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eUniformBuffer |
    vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
    vk::BufferUsageFlagBits::eIndirectBuffer;

/// Imports [pointer, pointer + size) as device memory with a buffer over it; false on failure.
bool ImportHost(void* pointer, u64 size, vk::Buffer& buffer, vk::DeviceMemory& memory,
                vk::Result& error) {
    const vk::ExternalMemoryBufferCreateInfo external{
        .handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT,
    };
    const auto [buffer_result, created] = device.createBuffer({
        .pNext = &external,
        .size = size,
        .usage = ChunkBufferUsage(),
        .sharingMode = vk::SharingMode::eExclusive,
    });
    if (buffer_result != vk::Result::eSuccess) {
        error = buffer_result;
        return false;
    }
    const auto [props_result, props] = device.getMemoryHostPointerPropertiesEXT(
        vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT, pointer);
    const auto requirements = device.getBufferMemoryRequirements(created);
    const std::uint32_t type = props_result == vk::Result::eSuccess
                                   ? FindHostType(props.memoryTypeBits & requirements.memoryTypeBits)
                                   : ~0u;
    if (type == ~0u) {
        device.destroyBuffer(created);
        error = props_result != vk::Result::eSuccess ? props_result
                                                     : vk::Result::eErrorFeatureNotPresent;
        return false;
    }
    const vk::MemoryAllocateFlagsInfo address_flags{
        .flags = vk::MemoryAllocateFlagBits::eDeviceAddress,
    };
    const vk::ImportMemoryHostPointerInfoEXT import_info{
        .pNext = LayerMemory() ? &address_flags : nullptr,
        .handleType = vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT,
        .pHostPointer = pointer,
    };
    const auto [memory_result, allocated] = device.allocateMemory({
        .pNext = &import_info,
        .allocationSize = size,
        .memoryTypeIndex = type,
    });
    if (memory_result != vk::Result::eSuccess) {
        device.destroyBuffer(created);
        error = memory_result;
        return false;
    }
    if (const auto result = device.bindBufferMemory(created, allocated, 0);
        result != vk::Result::eSuccess) {
        device.freeMemory(allocated);
        device.destroyBuffer(created);
        error = result;
        return false;
    }
    buffer = created;
    memory = allocated;
    return true;
}

/// The runtime's chunk allocator in host import mode: CHUNK_IMPORTED (-3, the chunk stays in the
/// memfd) once its backing view is imported, or -1.
int AllocImported(u64 phys, u64 size) {
    {
        std::scoped_lock lk{mutex};
        if (chunk_count == MaxChunks) {
            return -1;
        }
    }
    void* pointer = runtime_memory_backing_pointer(phys);
    if (!pointer || reinterpret_cast<std::uintptr_t>(pointer) % import_alignment != 0 ||
        size % import_alignment != 0) {
        return -1;
    }
    vk::Buffer buffer;
    vk::DeviceMemory memory;
    vk::Result error{};
    if (!ImportHost(pointer, size, buffer, memory, error)) {
        static std::atomic<int> failures{0};
        if (failures.fetch_add(1) < 4) {
            std::fprintf(stderr, "Guest memory: importing chunk %#llx failed (%s); it stays in the "
                                 "memfd\n",
                         (unsigned long long)phys, vk::to_string(error).c_str());
        }
        return -1;
    }
    {
        std::scoped_lock lk{mutex};
        std::size_t i = chunk_count++;
        for (; i > 0 && chunks[i - 1]->phys > phys; --i) {
            chunks[i] = chunks[i - 1];
        }
        static std::uint32_t next_index = 0;
        chunks[i] = new Chunk{phys, size, buffer, memory, next_index++};
        RegisterChunk(chunks[i]);
    }
    const u64 total = chunk_bytes.fetch_add(size) + size;
    std::printf("Guest memory: direct memory %#llx+%llu MiB imported by the GPU (host memory), "
                "%llu MiB so far\n",
                (unsigned long long)phys, (unsigned long long)(size >> 20),
                (unsigned long long)(total >> 20));
    return -3;
}

/// Runs `record` on the graphics queue and waits; false on any failure.
template <typename Record>
bool RunOnce(const Vulkan::Instance& instance, Record&& record) {
    const vk::Device dev = instance.GetDevice();
    const auto [pool_result, pool] = dev.createCommandPool(
        {.queueFamilyIndex = instance.GetGraphicsQueueFamilyIndex()});
    if (pool_result != vk::Result::eSuccess) {
        return false;
    }
    bool ok = false;
    const auto [alloc_result, cmdbufs] = dev.allocateCommandBuffers(
        {.commandPool = pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1});
    const auto [fence_result, fence] = dev.createFence({});
    if (alloc_result == vk::Result::eSuccess && fence_result == vk::Result::eSuccess) {
        const vk::CommandBuffer cmd = cmdbufs[0];
        if (cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}) ==
            vk::Result::eSuccess) {
            record(cmd);
            const vk::MemoryBarrier to_host{.srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                                            .dstAccessMask = vk::AccessFlagBits::eHostRead};
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                vk::PipelineStageFlagBits::eHost, {}, to_host, {}, {});
            if (cmd.end() == vk::Result::eSuccess) {
                std::scoped_lock submit_lock{Vulkan::Scheduler::submit_mutex};
                const vk::SubmitInfo submit{.commandBufferCount = 1, .pCommandBuffers = &cmd};
                if (instance.GetGraphicsQueue().submit(submit, fence) == vk::Result::eSuccess) {
                    auto waited = dev.waitForFences(fence, true, 5'000'000'000ull);
                    if (waited == vk::Result::eTimeout) {
                        // The caller also frees imported memory. A timeout cannot make pending
                        // work safe to destroy: wait for completion or device loss first.
                        waited = dev.waitForFences(fence, true, ~std::uint64_t{0});
                    }
                    ok = waited == vk::Result::eSuccess;
                }
            }
        }
    }
    if (fence_result == vk::Result::eSuccess) {
        dev.destroyFence(fence);
    }
    dev.destroyCommandPool(pool);
    return ok;
}


bool Probe(const Vulkan::Instance& instance) {
    if (!instance.IsHostMemoryImportSupported()) return false;
    vk::PhysicalDeviceExternalMemoryHostPropertiesEXT props;
    vk::PhysicalDeviceProperties2 properties{.pNext=&props};
    instance.GetPhysicalDevice().getProperties2(&properties);
    import_alignment=std::max<vk::DeviceSize>(4096,props.minImportedHostPointerAlignment);
    constexpr u64 size=2ull<<20;
    if (size%import_alignment) return false;
    HANDLE section=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,size,nullptr);
    if (!section) return false;
    auto* backing=static_cast<std::uint32_t*>(MapViewOfFile(section,FILE_MAP_ALL_ACCESS,0,0,size));
    auto* guest=static_cast<std::uint32_t*>(MapViewOfFile(section,FILE_MAP_ALL_ACCESS,0,0,size));
    bool ok=false;
    vk::Buffer buffer{};
    vk::DeviceMemory memory{};
    vk::Result error{};
    if (backing && guest && reinterpret_cast<u64>(backing)%import_alignment==0 &&
        ImportHost(backing,size,buffer,memory,error)) {
        guest[0]=0x12345678;
        ok=RunOnce(instance,[&](vk::CommandBuffer cmd) {
            const vk::BufferCopy copy{.srcOffset=0,.dstOffset=size-4,.size=4};
            cmd.copyBuffer(buffer,buffer,copy);
            const vk::MemoryBarrier before{.srcAccessMask=vk::AccessFlagBits::eTransferRead,
                .dstAccessMask=vk::AccessFlagBits::eTransferWrite};
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,vk::PipelineStageFlagBits::eTransfer,
                                {},before,{},{});
            cmd.fillBuffer(buffer,0,4,0xaabbccdd);
        }) && guest[0]==0xaabbccdd && guest[size/4-1]==0x12345678;
        device.destroyBuffer(buffer);
        device.freeMemory(memory);
    }
    if (backing) UnmapViewOfFile(backing);
    if (guest) UnmapViewOfFile(guest);
    CloseHandle(section);
    return ok;
}
} // namespace
bool LayerMemory() { return true; } // Windows never enters the sparse arena with imported memory.
bool HostImported() { return true; }
vk::ExternalMemoryHandleTypeFlagBits HandleType() {
    return vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT;
}
bool PcModelGpu(const Vulkan::Instance&) {
    const char* pc=std::getenv("BB_PC_MODEL");
    const char* in_place=std::getenv("BB_GUEST_IN_PLACE");
    return pc ? pc[0]=='1' : !in_place || in_place[0]=='1';
}
bool Usable(const Vulkan::Instance& instance) {
    static const bool usable=[&] {
        device=instance.GetDevice();
        memory_properties=instance.GetPhysicalDevice().getMemoryProperties();
        const bool ok=Probe(instance);
        std::printf("Guest memory: Windows host import probe %s (alignment %llu)\n",
                    ok ? "passed" : "failed; using old memory model",
                    static_cast<unsigned long long>(import_alignment));
        return ok;
    }();
    return usable;
}
void Install(const Vulkan::Instance& instance) {
    if (PcModelGpu(instance) && Usable(instance)) {
        runtime_memory_set_guest_chunk_allocator(AllocImported);
        std::puts("Guest memory: Windows imported host chunks, layer VRAM mirrors, no sparse rebinding");
    }
}
const Chunk* Find(std::uint64_t phys) {
    std::scoped_lock lk{mutex};
    for (std::size_t i=0;i<chunk_count;++i)
        if (phys>=chunks[i]->phys && phys-chunks[i]->phys<chunks[i]->size) return chunks[i];
    return nullptr;
}
} // namespace BbGuestMemory
