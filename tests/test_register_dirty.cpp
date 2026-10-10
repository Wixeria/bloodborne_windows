#include <cstdlib>
#include <cassert>
#include <vector>
#include <memory>
#include <set>
#include "video_core/amdgpu/regs.h"

int main() {
    AmdGpu::RegDirty dirty;
    std::vector<size_t> actual;
    const auto collect=[&] {
        actual.clear();
        dirty.ForEachBlock([&](size_t block) { actual.push_back(block); });
    };
    collect();
    assert(actual.empty());
    dirty.Mark(31,2); // a write spanning register blocks
    dirty.Mark(63*32,64); // crossing a bitset word boundary
    dirty.Mark(AmdGpu::Regs::NumRegs-1,1);
    dirty.Mark(AmdGpu::Regs::NumRegs,1); // rejected out-of-range write
    dirty.Mark(100,0);
    collect();
    const std::vector<size_t> expected{0,1,63,64,AmdGpu::RegDirty::NumBlocks-1};
    assert(actual == expected);
    dirty.reset=true;
    dirty.Clear();
    collect();
    assert(actual.empty() && !dirty.reset);
    // Every register block must appear once, in ascending order.
    dirty.Mark(0,AmdGpu::Regs::NumRegs);
    collect();
    assert(actual.size() == AmdGpu::RegDirty::NumBlocks);
    for (size_t i=0;i<actual.size();++i) assert(actual[i] == i);

    // Sparse patterns must preserve packet ordering even across empty 64-bit words.
    u32 random = 0x83a179b5;
    for (unsigned round=0; round<100; ++round) {
        dirty.Clear();
        std::set<size_t> reference;
        for (unsigned n=0; n<=round; ++n) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            const size_t block = random % AmdGpu::RegDirty::NumBlocks;
            reference.insert(block);
            dirty.Mark(u32(block * AmdGpu::RegDirty::BlockWords), 1);
        }
        collect();
        assert(actual == std::vector<size_t>(reference.begin(), reference.end()));
    }
    auto regs = std::make_unique<AmdGpu::Regs>();
    for (u32 i=0; i<AmdGpu::Regs::NumRegs; ++i) regs->reg_array[i] = i ^ 0x93750000;
    AmdGpu::RegDelta delta;
    delta.Capture(*regs, dirty);
    assert(delta.blocks.size() == actual.size());
    assert(delta.words.size() == actual.size() * AmdGpu::RegDirty::BlockWords);
    for (size_t i=0; i<actual.size(); ++i) {
        assert(delta.blocks[i] == actual[i]);
        for (u32 j=0; j<AmdGpu::RegDirty::BlockWords; ++j)
            assert(delta.words[i*AmdGpu::RegDirty::BlockWords+j] ==
                   regs->reg_array[actual[i]*AmdGpu::RegDirty::BlockWords+j]);
    }
#ifdef _LIBCPP_VERSION
    AmdGpu::RegBlockBits<65> tail;
    assert(tail.FindFirst() == 65);
    tail.set(64);
    assert(tail.FindFirst() == 64 && tail.FindNext(64) == 65);
    tail.set(0);
    assert(tail.FindFirst() == 0 && tail.FindNext(0) == 64);
    tail.reset();
    assert(tail.FindFirst() == 65);
#endif
}
