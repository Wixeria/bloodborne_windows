#include "gpu/shim/bbport_threads.h"
#include "gpu/shim/bbport_write_log.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <thread>
int main() {
    DWORD_PTR mask,system;
    assert(GetProcessAffinityMask(GetCurrentProcess(),&mask,&system));
    assert(BbThreads::Available()==static_cast<unsigned>(__builtin_popcountll(mask)));
    std::thread helper([] { BbThreads::MakeBackground(); SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_END); });
    helper.join();
    assert(!_putenv("BB_WRITE_LOG=1")); assert(BbWriteLog::Enabled());
    std::uint64_t memory[32]={0}; memory[8]=0x0000005300000000ull;
    BbWriteLog::Note(reinterpret_cast<std::uint64_t>(&memory[8]),&memory[8],8,BbWriteLog::WriteData);
    CONTEXT context{}; context.Rax=reinterpret_cast<DWORD64>(&memory[8]);
    bbgpu_dump_guest_writes(&context);
    context.Rax=0x12345678; bbgpu_dump_guest_writes(&context); // unmapped diagnostic reads must not crash
    std::puts("GPU host helpers: affinity, background thread, Win32 context and safe diagnostic reads PASS");
}
