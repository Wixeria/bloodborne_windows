#include <windows.h>
#include <cassert>
#include <atomic>
#include <thread>
#include <vector>
#include "runtime_fault.h"

static std::atomic<unsigned> faults{};
static LONG CALLBACK handler(EXCEPTION_POINTERS* exception) {
    if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        faults.fetch_add(1);
        bb_fault_try_recover();
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void read_unavailable(volatile unsigned* unavailable) {
    BbFaultBuffer recover;
    if (bb_fault_setjmp(recover)) {
        assert(runtime_fault_recover == nullptr);
        return;
    }
    runtime_fault_recover = &recover;
    volatile unsigned value = *unavailable;
    (void)value;
    assert(false && "no-access memory must fault");
}

int main() {
    auto* unavailable = static_cast<unsigned*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS));
    assert(unavailable);
    void* registration=AddVectoredExceptionHandler(1,handler);
    assert(registration);
    std::vector<std::thread> threads;
    for (unsigned i=0;i<8;++i) threads.emplace_back([=] {
        for (unsigned n=0;n<100;++n) {
            assert(!bb_fault_try_recover());
            read_unavailable(unavailable);
            assert(runtime_fault_recover == nullptr);
            // A fresh point on the same thread must remain usable after a jump.
            read_unavailable(unavailable);
        }
    });
    for (auto& thread : threads) thread.join();
    assert(faults == 1600);
    assert(RemoveVectoredExceptionHandler(registration));
    assert(VirtualFree(unavailable,0,MEM_RELEASE));
}
