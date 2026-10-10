#include <windows.h>
#include <array>
#include <atomic>
#include <barrier>
#include <cassert>
#include <thread>
#include <vector>
#include "core/signals.h"
#include "shader_recompiler/ir/passes/srt.h"

static std::atomic<unsigned> recovered{};
static LONG CALLBACK handle_fault(EXCEPTION_POINTERS* exception) {
    if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        Core::Signals::Instance()->DispatchAccessViolation(exception,
            reinterpret_cast<void*>(exception->ExceptionRecord->ExceptionInformation[1]))) {
        ++recovered;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static bool dispatch_at(Shader::PFN_SrtWalker walker) {
    CONTEXT context{};
    context.Rip = reinterpret_cast<DWORD64>(walker);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    EXCEPTION_POINTERS exception{&record, &context};
    return Core::Signals::Instance()->DispatchAccessViolation(&exception, nullptr);
}

int main() {
    // No generated walkers precede registration: reproduce a cache-only start.
    // SysV: mov r10d,[rdi+8]; mov [rsi],r10d; ret.
    constexpr std::array<u8, 8> r10_code{0x44,0x8b,0x57,0x08,0x44,0x89,0x16,0xc3};
    auto r10 = Shader::RegisterWalkerCode(r10_code.data(), r10_code.size());
    auto* registration = AddVectoredExceptionHandler(1, handle_fault);
    assert(registration);
    const std::array<u32, 3> input{10,20,42};
    u32 output = 99;
    r10(input.data(), &output);
    assert(output == 42);
    r10(nullptr, &output);
    assert(output == 0);
    assert(recovered == 1);
    // Another thread can already have faulted before the first patch completed.
    assert(dispatch_at(r10));
    r10(nullptr, &output);
    assert(output == 0 && recovered == 1);

    // mov rdi,[rdi+8]; mov [rsi],edi; ret. Multiple pending fault contexts
    // patch the same cached instruction; run the resulting code afterwards.
    constexpr std::array<u8, 7> rdi_code{0x48,0x8b,0x7f,0x08,0x89,0x3e,0xc3};
    auto rdi = Shader::RegisterWalkerCode(rdi_code.data(), rdi_code.size());
    std::barrier start(8);
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 8; ++i) threads.emplace_back([&] {
        start.arrive_and_wait();
        for (unsigned repeat = 0; repeat < 100; ++repeat) assert(dispatch_at(rdi));
    });
    for (auto& thread : threads) thread.join();
    output = 99;
    rdi(nullptr, &output);
    assert(output == 0);
    const auto* bytes = reinterpret_cast<const u8*>(rdi);
    assert(bytes[0] == 0x48 && bytes[1] == 0x31 && bytes[2] == 0xff && bytes[3] == 0x90);
    // Faults outside the code buffer must continue to other handlers.
    CONTEXT outside{};
    outside.Rip = reinterpret_cast<DWORD64>(&main);
    EXCEPTION_RECORD record{};
    EXCEPTION_POINTERS exception{&record, &outside};
    assert(!Core::Signals::Instance()->DispatchAccessViolation(&exception, nullptr));
    assert(RemoveVectoredExceptionHandler(registration));
}
