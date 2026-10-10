// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include "core/signals.h"

static unsigned seen;
static bool First(void*, void*) { seen |= 1; return false; }
static bool Second(void*, void*) { seen |= 2; return false; }
static bool Accept(void*, void*) { seen |= 4; return true; }
static bool IllegalFirst(void*) { seen |= 1; return false; }
static bool IllegalSecond(void*) { seen |= 2; return false; }

int main() {
    Core::SignalDispatch dispatch;
    dispatch.RegisterAccessViolationHandler(First, 10);
    dispatch.RegisterAccessViolationHandler(Second, 10);
    dispatch.RegisterAccessViolationHandler(First, 10); // duplicate stays idempotent
    assert(!dispatch.DispatchAccessViolation(nullptr, nullptr));
    assert(seen == 3);
    seen = 0;
    dispatch.RegisterAccessViolationHandler(Accept, 20);
    assert(dispatch.DispatchAccessViolation(nullptr, nullptr));
    assert(seen == 7); // higher numeric priority runs after both trackers
    seen = 0;
    dispatch.RegisterIllegalInstructionHandler(IllegalFirst, 10);
    dispatch.RegisterIllegalInstructionHandler(IllegalSecond, 10);
    assert(!dispatch.DispatchIllegalInstruction(nullptr));
    assert(seen == 3);
    dispatch.RemoveHandlers();
    seen = 0;
    assert(!dispatch.DispatchAccessViolation(nullptr, nullptr));
    assert(!dispatch.DispatchIllegalInstruction(nullptr));
    assert(seen == 0);
    return 0;
}
