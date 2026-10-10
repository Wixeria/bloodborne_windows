// SPDX-License-Identifier: GPL-2.0-or-later
// Regression cases for guest aliases and partial VRAM mirror invalidation.
#include <cassert>
#include "bblayer_gpu_memory.h"

static bool Translate(std::uint64_t va, std::uint64_t* phys, std::uint64_t* end) {
    if (va >= 0x100000 && va < 0x140000) {
        *phys = va - 0x100000;
        *end = 0x140000;
        return true;
    }
    if (va >= 0x200000 && va < 0x240000) {
        *phys = va - 0x200000;
        *end = 0x240000;
        return true;
    }
    return false;
}

int main() {
    using namespace BbLayer;
    auto& memory = GpuMemory::Get();
    memory.SetTranslate(Translate);
    memory.SetBlockShift(16);
    const auto guest = reinterpret_cast<VkBuffer>(std::uintptr_t{1});
    const auto mirror = reinterpret_cast<VkBuffer>(std::uintptr_t{2});
    memory.AddGuestSource(0, 0x40000, guest, 0x10000000, nullptr);
    auto a = memory.ResolveInPlace(0x101000, 0x2000);
    auto alias = memory.ResolveInPlace(0x201000, 0x2000);
    assert(a && alias && a->source == alias->source && a->offset == alias->offset);
    assert(!memory.ResolveInPlace(0x13f000, 0x2000));
    assert(memory.Resolve(0x300000, 4096).kind == Resolution::Kind::None);
    memory.AddMirror(0x100000, 0x40000, mirror, 0x20000000, nullptr);
    memory.MarkValid(0x10, 0x14);
    assert(memory.Resolve(0x101000, 0x2000).kind == Resolution::Kind::Mirror);
    auto version = memory.ValidVersion();
    memory.MarkInvalid(0x11, 0x12);
    assert(memory.ValidVersion() > version);
    assert(memory.Resolve(0x100000, 0x20000).kind == Resolution::Kind::Mixed);
    assert(memory.Resolve(0x110000, 0x10000).kind == Resolution::Kind::InPlace);
    assert(memory.ValidBytes() == 0x30000);
    assert(memory.Resolve(0x201000, 0x2000).kind == Resolution::Kind::InPlace);
    memory.MarkInvalid(0x10, 0x14);
    memory.RemoveMirror(0x100000);
    assert(memory.ValidBytes() == 0);
    assert(memory.Resolve(0x101000, 0x2000).kind == Resolution::Kind::InPlace);
    return 0;
}
