#include "plugin/BackgroundRun.hpp"

#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <objc/message.h>
#include <objc/runtime.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

namespace nightcoop_mac::background_run
{
namespace
{
using FocusHandler = void (*)(id, SEL, id);
using FocusSlot = void (*)(uint64_t listener, uint64_t list, bool focused);
using FlushListeners = void (*)(uint64_t list);

constexpr uint64_t ListenerListOffset = 0x20;
constexpr uint64_t ListenerArrayOffset = 0x10;
constexpr uint64_t ListenerCountOffset = 0x1c;
constexpr uint32_t MostListeners = 64;
constexpr uint64_t FocusSlotOffset = 0x38;
constexpr uint64_t FlushCallOffset = 0x1c;
constexpr uint32_t FlushFirstWord = 0xa9bb67fa;
constexpr uint64_t NonUniqueNameBit = 1ull << 63;
constexpr uint64_t RecheckEveryMs = 5000;

FocusHandler OriginalResign = nullptr;
FocusHandler OriginalBecomeKey = nullptr;
FocusHandler OriginalOcclusion = nullptr;
FlushListeners Flush = nullptr;
std::mutex NotesLock;
std::string Notes;
int ResignsNoted = 0;
int BecomeKeysNoted = 0;
int OcclusionsNoted = 0;
int KeepNotes = 0;

std::atomic<bool> WantKeepRunning = false;
std::atomic<bool> ApplyPending = false;
bool EverAsked = false;
uint64_t NextRecheckMs = 0;

id GameDelegate = nil;
id GameWindow = nil;
std::vector<uint64_t> ToldListeners;

template <typename Value>
bool ReadMemory(uint64_t address, Value& value)
{
    vm_size_t got = 0;
    return address != 0 &&
           vm_read_overwrite(mach_task_self(), address, sizeof(Value), reinterpret_cast<vm_address_t>(&value), &got) == KERN_SUCCESS &&
           got == sizeof(Value);
}

std::string ReadName(uint64_t address)
{
    std::string name;
    for (int index = 0; index < 160; ++index)
    {
        char letter = 0;
        if (!ReadMemory(address + index, letter) || letter == 0)
        {
            break;
        }
        name += letter;
    }
    return name;
}

void AddNote(const std::string& line)
{
    std::lock_guard guard(NotesLock);
    Notes += line + "\n";
}

FlushListeners FindFlush(FocusHandler resign)
{
    uint32_t call = 0;
    const uint64_t callAddress = reinterpret_cast<uint64_t>(resign) + FlushCallOffset;
    if (!resign || !ReadMemory(callAddress, call) || (call & 0xfc000000) != 0x94000000)
    {
        return nullptr;
    }
    int64_t jump = call & 0x03ffffff;
    if (jump & 0x02000000)
    {
        jump -= 0x04000000;
    }
    const uint64_t target = callAddress + jump * 4;
    uint32_t firstWord = 0;
    return ReadMemory(target, firstWord) && firstWord == FlushFirstWord ? reinterpret_cast<FlushListeners>(target) : nullptr;
}

uint64_t ListenerList(id delegate)
{
    uint64_t list = 0;
    ReadMemory(reinterpret_cast<uint64_t>(delegate) + ListenerListOffset, list);
    return list;
}

std::vector<uint64_t> CurrentListeners(uint64_t list)
{
    std::vector<uint64_t> listeners;
    uint32_t count = 0;
    uint64_t array = 0;
    if (!list || !ReadMemory(list + ListenerCountOffset, count) || !ReadMemory(list + ListenerArrayOffset, array) || count > MostListeners)
    {
        return listeners;
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        uint64_t listener = 0;
        if (ReadMemory(array + index * 8, listener) && listener)
        {
            listeners.push_back(listener);
        }
    }
    return listeners;
}

FocusSlot SlotOf(uint64_t listener)
{
    uint64_t vtable = 0;
    FocusSlot slot = nullptr;
    return ReadMemory(listener, vtable) && ReadMemory(vtable + FocusSlotOffset, slot) ? slot : nullptr;
}

std::string NameOf(uint64_t listener)
{
    uint64_t vtable = 0;
    uint64_t typeInfo = 0;
    uint64_t namePointer = 0;
    ReadMemory(listener, vtable);
    ReadMemory(vtable - 8, typeInfo);
    ReadMemory(typeInfo + 8, namePointer);
    std::string name = ReadName(namePointer & ~NonUniqueNameBit);
    Dl_info info {};
    const auto slot = reinterpret_cast<uint64_t>(SlotOf(listener));
    char offset[40] = "";
    if (dladdr(reinterpret_cast<const void*>(slot), &info) && info.dli_fbase)
    {
        std::snprintf(offset, sizeof(offset), " fn +%#llx", static_cast<unsigned long long>(slot - reinterpret_cast<uint64_t>(info.dli_fbase)));
    }
    return (name.empty() ? "?" : name) + offset;
}

std::string DescribeListeners(id delegate)
{
    const auto listeners = CurrentListeners(ListenerList(delegate));
    std::string text = std::to_string(listeners.size()) + " listener(s):";
    for (size_t index = 0; index < listeners.size() && index < 16; ++index)
    {
        text += " [" + NameOf(listeners[index]) + "]";
    }
    return text;
}

int TellNewListeners(id delegate)
{
    const uint64_t list = ListenerList(delegate);
    if (!Flush || !list)
    {
        return 0;
    }
    Flush(list);
    const auto listeners = CurrentListeners(list);
    std::erase_if(ToldListeners, [&](uint64_t told) { return std::find(listeners.begin(), listeners.end(), told) == listeners.end(); });
    int told = 0;
    for (const uint64_t listener : listeners)
    {
        if (std::find(ToldListeners.begin(), ToldListeners.end(), listener) != ToldListeners.end())
        {
            continue;
        }
        if (const auto slot = SlotOf(listener))
        {
            slot(listener, list, true);
            ToldListeners.push_back(listener);
            ++told;
        }
    }
    return told;
}

void TakeBackFocus(id delegate)
{
    const uint64_t list = ListenerList(delegate);
    if (!Flush || !list)
    {
        return;
    }
    Flush(list);
    for (const uint64_t listener : CurrentListeners(list))
    {
        const auto slot = SlotOf(listener);
        if (slot && std::find(ToldListeners.begin(), ToldListeners.end(), listener) != ToldListeners.end())
        {
            slot(listener, list, false);
        }
    }
    ToldListeners.clear();
}

bool WindowIsKey()
{
    return GameWindow && reinterpret_cast<bool (*)(id, SEL)>(objc_msgSend)(GameWindow, sel_registerName("isKeyWindow"));
}

void FindGameDelegate()
{
    Class delegateClass = objc_getClass("GameWindowDelegate");
    id application = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(reinterpret_cast<id>(objc_getClass("NSApplication")), sel_registerName("sharedApplication"));
    id windows = application ? reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(application, sel_registerName("windows")) : nil;
    const auto windowCount = windows ? reinterpret_cast<unsigned long (*)(id, SEL)>(objc_msgSend)(windows, sel_registerName("count")) : 0;
    for (unsigned long index = 0; index < windowCount; ++index)
    {
        id window = reinterpret_cast<id (*)(id, SEL, unsigned long)>(objc_msgSend)(windows, sel_registerName("objectAtIndex:"), index);
        id delegate = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(window, sel_registerName("delegate"));
        if (delegate && reinterpret_cast<bool (*)(id, SEL, Class)>(objc_msgSend)(delegate, sel_registerName("isKindOfClass:"), delegateClass))
        {
            GameDelegate = delegate;
            GameWindow = window;
            return;
        }
    }
}

void ApplyKeepRunning(void*)
{
    ApplyPending = false;
    if (!GameDelegate)
    {
        FindGameDelegate();
    }
    if (!GameDelegate || WindowIsKey())
    {
        return;
    }
    if (WantKeepRunning)
    {
        const int told = TellNewListeners(GameDelegate);
        if (told > 0 && KeepNotes++ < 6)
        {
            AddNote("background run: told " + std::to_string(told) + " engine listener(s) we have focus so the world keeps going with the window in the back, " +
                    DescribeListeners(GameDelegate));
        }
    }
    else if (!ToldListeners.empty())
    {
        TakeBackFocus(GameDelegate);
        AddNote("background run: the game pauses again when its window is not up front");
    }
}

void OurResign(id self, SEL command, id notification)
{
    if (OriginalResign)
    {
        OriginalResign(self, command, notification);
    }
    if (ResignsNoted++ < 4)
    {
        AddNote("background run: game window lost focus, " + DescribeListeners(self));
    }
    GameDelegate = self;
    GameWindow = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(notification, sel_registerName("object"));
    ToldListeners.clear();
    if (WantKeepRunning)
    {
        TellNewListeners(self);
    }
}

void OurBecomeKey(id self, SEL command, id notification)
{
    if (OriginalBecomeKey)
    {
        OriginalBecomeKey(self, command, notification);
    }
    if (BecomeKeysNoted++ < 4)
    {
        AddNote("background run: game window got focus, " + DescribeListeners(self));
    }
    ToldListeners.clear();
}

void OurOcclusion(id self, SEL command, id notification)
{
    if (OriginalOcclusion)
    {
        OriginalOcclusion(self, command, notification);
    }
    if (OcclusionsNoted++ < 8)
    {
        id window = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(notification, sel_registerName("object"));
        const auto state = reinterpret_cast<unsigned long (*)(id, SEL)>(objc_msgSend)(window, sel_registerName("occlusionState"));
        AddNote(std::string("background run: game window ") + ((state & 2) ? "visible again" : "fully covered"));
    }
}

FocusHandler Swap(Class delegateClass, const char* selectorName, FocusHandler ours)
{
    Method method = class_getInstanceMethod(delegateClass, sel_registerName(selectorName));
    if (!method)
    {
        return nullptr;
    }
    return reinterpret_cast<FocusHandler>(method_setImplementation(method, reinterpret_cast<IMP>(ours)));
}
}

void Install()
{
    Class delegateClass = objc_getClass("GameWindowDelegate");
    if (!delegateClass || OriginalResign)
    {
        AddNote(delegateClass ? "background run: already in" : "background run: no GameWindowDelegate class");
        return;
    }
    OriginalResign = Swap(delegateClass, "windowDidResignKey:", OurResign);
    OriginalBecomeKey = Swap(delegateClass, "windowDidBecomeKey:", OurBecomeKey);
    OriginalOcclusion = Swap(delegateClass, "windowDidChangeOcclusionState:", OurOcclusion);
    Flush = FindFlush(OriginalResign);
    AddNote(std::string("background run: watching the game window (") + (OriginalResign ? "resign " : "") + (OriginalBecomeKey ? "key " : "") +
            (OriginalOcclusion ? "occlusion" : "") + (Flush ? ")" : ") but the listener flush is not where we know it so no keep running"));
}

void KeepRunning(bool keep, uint64_t nowMs)
{
    const bool changed = keep != WantKeepRunning || !EverAsked;
    if (!changed && (!keep || nowMs < NextRecheckMs))
    {
        return;
    }
    EverAsked = true;
    WantKeepRunning = keep;
    NextRecheckMs = nowMs + RecheckEveryMs;
    if (!ApplyPending.exchange(true))
    {
        dispatch_async_f(dispatch_get_main_queue(), nullptr, ApplyKeepRunning);
    }
}

std::string TakeNotes()
{
    std::lock_guard guard(NotesLock);
    std::string taken;
    taken.swap(Notes);
    return taken;
}
}
