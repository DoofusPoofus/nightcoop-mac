#include "plugin/KeyWatch.hpp"

#include <objc/message.h>
#include <objc/runtime.h>

#include <atomic>

namespace nightcoop_mac
{
namespace
{
constexpr unsigned long KeyDownType = 10;
constexpr unsigned long long KeyDownMask = 1ull << 10;
constexpr unsigned long long KeyUpMask = 1ull << 11;
constexpr unsigned short KnownKeys = 128;

std::atomic<bool> HeldKeys[KnownKeys];
std::atomic<bool> SwallowedKeys[KnownKeys];
std::atomic<unsigned> SwallowedPresses[KnownKeys];
bool Watching = false;
}

void WatchKeys()
{
    if (Watching)
    {
        return;
    }
    Watching = true;
    id (^noteKey)(id) = ^id(id event) {
        const auto type = reinterpret_cast<unsigned long (*)(id, SEL)>(objc_msgSend)(event, sel_registerName("type"));
        const auto keyCode = reinterpret_cast<unsigned short (*)(id, SEL)>(objc_msgSend)(event, sel_registerName("keyCode"));
        if (keyCode >= KnownKeys)
        {
            return event;
        }
        HeldKeys[keyCode] = type == KeyDownType;
        if (type != KeyDownType || !SwallowedKeys[keyCode])
        {
            return event;
        }
        const double timestamp = reinterpret_cast<double (*)(id, SEL)>(objc_msgSend)(event, sel_registerName("timestamp"));
        if (timestamp == 0.0)
        {
            return event;
        }
        const bool repeat = reinterpret_cast<bool (*)(id, SEL)>(objc_msgSend)(event, sel_registerName("isARepeat"));
        if (!repeat)
        {
            ++SwallowedPresses[keyCode];
        }
        return nil;
    };
    reinterpret_cast<id (*)(id, SEL, unsigned long long, id)>(objc_msgSend)(reinterpret_cast<id>(objc_getClass("NSEvent")),
                                                                            sel_registerName("addLocalMonitorForEventsMatchingMask:handler:"),
                                                                            KeyDownMask | KeyUpMask, reinterpret_cast<id>(noteKey));
}

bool KeyHeld(unsigned short keyCode)
{
    return keyCode < KnownKeys && HeldKeys[keyCode];
}

void HoldKeyPresses(unsigned short keyCode, bool hold)
{
    if (keyCode < KnownKeys)
    {
        SwallowedKeys[keyCode] = hold;
    }
}

unsigned TakeHeldPresses(unsigned short keyCode)
{
    return keyCode < KnownKeys ? SwallowedPresses[keyCode].exchange(0) : 0;
}
}
