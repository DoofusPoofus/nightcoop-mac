#include "plugin/PartnerLook.hpp"

#include <mach-o/dyld.h>
#include <mach/mach.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace nightcoop_mac::partner_look
{
namespace
{
constexpr uintptr_t GetStateForGenderOffset = 0x4bc7228;
constexpr uintptr_t GetStateOffset = 0x4bc6098;
constexpr uintptr_t LookPartOffset = 0x4bacda8;
constexpr uintptr_t LookPartOwnerSlot = 0x2a8;
constexpr size_t EntityIdOffset = 0x48;
constexpr size_t ComponentEntityOffset = 0x50;
constexpr int CallerRegisterCount = 10;
constexpr uint8_t GetStateForGenderBytes[24] = {0xf6, 0x57, 0xbd, 0xa9, 0xf4, 0x4f, 0x01, 0xa9, 0xfd, 0x7b, 0x02, 0xa9,
                                                0xfd, 0x83, 0x00, 0x91, 0xf6, 0x03, 0x01, 0xaa, 0xf5, 0x03, 0x00, 0xaa};
constexpr uint8_t GetStateBytes[24] = {0xf6, 0x57, 0xbd, 0xa9, 0xf4, 0x4f, 0x01, 0xa9, 0xfd, 0x7b, 0x02, 0xa9,
                                       0xfd, 0x83, 0x00, 0x91, 0xf4, 0x03, 0x00, 0xaa, 0xf5, 0x03, 0x08, 0xaa};
constexpr uint8_t LookPartBytes[24] = {0xff, 0xc3, 0x01, 0xd1, 0xf6, 0x57, 0x04, 0xa9, 0xf4, 0x4f, 0x05, 0xa9,
                                       0xfd, 0x7b, 0x06, 0xa9, 0xfd, 0x83, 0x01, 0x91, 0xf3, 0x03, 0x01, 0xaa};

using StateHandle = RED4ext::Handle<RED4ext::IScriptable>;
using GetStateForGenderFunction = StateHandle (*)(void* system, bool isMale);
using GetStateFunction = StateHandle (*)(void* system);
using LookPartFunction = void (*)(void* component, void* slot);
using OwnerFunction = StateHandle (*)(void* component);

GetStateForGenderFunction OriginalGetStateForGender = nullptr;
GetStateFunction OriginalGetState = nullptr;
LookPartFunction OriginalLookPart = nullptr;
LogFunction Log;
bool HooksInstalled = false;

std::atomic<uint32_t> PartnerStateCount {0};
std::atomic<uint32_t> AnsweredCount {0};
std::atomic<uint32_t> PartsOfPartners {0};
std::atomic<uint32_t> PartsOfOthers {0};
std::atomic<uint32_t> GenderAsksInScope {0};
std::atomic<uint32_t> GenderAsksOutside {0};
std::atomic<uint32_t> FoundByCaller {0};
std::mutex PartnerStatesLock;
struct PartnerEntry
{
    StateHandle State;
    bool IsMale = false;
};
std::unordered_map<uint64_t, PartnerEntry> PartnerStates;
std::atomic<int> OwnGender {-1};

thread_local uint64_t LookOwnerInScope = 0;

uintptr_t GameImageBase()
{
    for (uint32_t index = 0; index < _dyld_image_count(); ++index)
    {
        const char* name = _dyld_get_image_name(index);
        if (name && std::strstr(name, "/Contents/MacOS/Cyberpunk2077"))
        {
            return reinterpret_cast<uintptr_t>(_dyld_get_image_header(index));
        }
    }
    return 0;
}

uint64_t OwnerEntityOf(void* component)
{
    const auto vtable = *reinterpret_cast<uintptr_t*>(component);
    const auto ownerOf = *reinterpret_cast<OwnerFunction*>(vtable + LookPartOwnerSlot);
    const StateHandle owner = ownerOf(component);
    if (!owner)
    {
        return 0;
    }
    uint64_t entityHash = 0;
    std::memcpy(&entityHash, reinterpret_cast<uint8_t*>(owner.instance) + EntityIdOffset, sizeof(entityHash));
    return entityHash;
}

void OnLookPart(void* component, void* slot)
{
    if (PartnerStateCount.load(std::memory_order_acquire) == 0 || !component)
    {
        PartsOfOthers.fetch_add(1, std::memory_order_relaxed);
        OriginalLookPart(component, slot);
        return;
    }
    const uint64_t outerScope = LookOwnerInScope;
    LookOwnerInScope = OwnerEntityOf(component);
    {
        std::lock_guard guard(PartnerStatesLock);
        (PartnerStates.count(LookOwnerInScope) ? PartsOfPartners : PartsOfOthers).fetch_add(1, std::memory_order_relaxed);
    }
    OriginalLookPart(component, slot);
    LookOwnerInScope = outerScope;
}

bool PlausiblePointer(uint64_t value)
{
    return value > 0x100000000ull && value < 0x0000800000000000ull && (value & 7) == 0;
}

bool SafeRead(uint64_t address, uint64_t& value)
{
    vm_size_t readBytes = 0;
    return PlausiblePointer(address & ~7ull) &&
           vm_read_overwrite(mach_task_self(), address, sizeof(value), reinterpret_cast<vm_address_t>(&value), &readBytes) == KERN_SUCCESS &&
           readBytes == sizeof(value);
}

bool IsPartnerBody(uint64_t entityHash)
{
    std::lock_guard guard(PartnerStatesLock);
    return PartnerStates.count(entityHash) != 0;
}

uint64_t PartnerBodyAmong(const uint64_t* callerRegisters)
{
    for (int index = 0; index < CallerRegisterCount; ++index)
    {
        const uint64_t object = callerRegisters[index];
        if (!PlausiblePointer(object))
        {
            continue;
        }
        uint64_t entityHash = 0;
        if (SafeRead(object + EntityIdOffset, entityHash) && entityHash && IsPartnerBody(entityHash))
        {
            return entityHash;
        }
        uint64_t entity = 0;
        if (SafeRead(object + ComponentEntityOffset, entity) && PlausiblePointer(entity) && SafeRead(entity + EntityIdOffset, entityHash) &&
            entityHash && IsPartnerBody(entityHash))
        {
            return entityHash;
        }
    }
    return 0;
}

bool ScopedState(StateHandle& state, uint64_t owner)
{
    if (!owner || PartnerStateCount.load(std::memory_order_acquire) == 0)
    {
        return false;
    }
    std::lock_guard guard(PartnerStatesLock);
    const auto partner = PartnerStates.find(owner);
    if (partner == PartnerStates.end() || !partner->second.State)
    {
        return false;
    }
    state = partner->second.State;
    return true;
}

bool OnlyPartnerOfGender(bool isMale, StateHandle& state)
{
    const int ownGender = OwnGender.load(std::memory_order_acquire);
    if (LookOwnerInScope || ownGender < 0 || (ownGender == 1) == isMale || PartnerStateCount.load(std::memory_order_acquire) == 0)
    {
        return false;
    }
    std::lock_guard guard(PartnerStatesLock);
    const StateHandle* found = nullptr;
    for (const auto& [entityHash, entry] : PartnerStates)
    {
        if (entry.IsMale != isMale || !entry.State)
        {
            continue;
        }
        if (found && found->instance != entry.State.instance)
        {
            return false;
        }
        found = &entry.State;
    }
    if (!found)
    {
        return false;
    }
    state = *found;
    return true;
}

uint64_t OwnerFor(const uint64_t* callerRegisters)
{
    if (LookOwnerInScope)
    {
        return LookOwnerInScope;
    }
    if (PartnerStateCount.load(std::memory_order_acquire) == 0)
    {
        return 0;
    }
    const uint64_t owner = PartnerBodyAmong(callerRegisters);
    if (owner)
    {
        FoundByCaller.fetch_add(1, std::memory_order_relaxed);
    }
    return owner;
}
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
extern "C" StateHandle NightCoopGetStateForGenderWithCaller(void* system, bool isMale, const uint64_t* callerRegisters)
{
    const uint64_t owner = OwnerFor(callerRegisters);
    (owner ? GenderAsksInScope : GenderAsksOutside).fetch_add(1, std::memory_order_relaxed);
    StateHandle state;
    if (ScopedState(state, owner) || (!owner && OnlyPartnerOfGender(isMale, state)))
    {
        AnsweredCount.fetch_add(1, std::memory_order_relaxed);
        return state;
    }
    return OriginalGetStateForGender(system, isMale);
}

extern "C" StateHandle NightCoopGetStateWithCaller(void* system, const uint64_t* callerRegisters)
{
    StateHandle state;
    if (ScopedState(state, OwnerFor(callerRegisters)))
    {
        AnsweredCount.fetch_add(1, std::memory_order_relaxed);
        return state;
    }
    return OriginalGetState(system);
}

#pragma clang diagnostic pop

extern "C" __attribute__((naked)) void NightCoopGetStateForGenderEntry()
{
    asm volatile("sub sp, sp, #0x60\n"
                 "stp x19, x20, [sp, #0x00]\n"
                 "stp x21, x22, [sp, #0x10]\n"
                 "stp x23, x24, [sp, #0x20]\n"
                 "stp x25, x26, [sp, #0x30]\n"
                 "stp x27, x28, [sp, #0x40]\n"
                 "stp x29, x30, [sp, #0x50]\n"
                 "add x29, sp, #0x50\n"
                 "mov x2, sp\n"
                 "bl _NightCoopGetStateForGenderWithCaller\n"
                 "ldp x29, x30, [sp, #0x50]\n"
                 "add sp, sp, #0x60\n"
                 "ret\n");
}

extern "C" __attribute__((naked)) void NightCoopGetStateEntry()
{
    asm volatile("sub sp, sp, #0x60\n"
                 "stp x19, x20, [sp, #0x00]\n"
                 "stp x21, x22, [sp, #0x10]\n"
                 "stp x23, x24, [sp, #0x20]\n"
                 "stp x25, x26, [sp, #0x30]\n"
                 "stp x27, x28, [sp, #0x40]\n"
                 "stp x29, x30, [sp, #0x50]\n"
                 "add x29, sp, #0x50\n"
                 "mov x1, sp\n"
                 "bl _NightCoopGetStateWithCaller\n"
                 "ldp x29, x30, [sp, #0x50]\n"
                 "add sp, sp, #0x60\n"
                 "ret\n");
}

namespace
{

bool AttachChecked(const RED4ext::v1::Sdk* sdk, RED4ext::v1::PluginHandle pluginHandle, uintptr_t base, uintptr_t offset,
                   const uint8_t (&expected)[24], void* detour, void** original, const char* what)
{
    const auto target = reinterpret_cast<void*>(base + offset);
    if (std::memcmp(target, expected, sizeof(expected)) != 0)
    {
        Log(std::string("look: ") + what + " is not the code seen in mac 2.5.0, partners keep our look");
        return false;
    }
    if (!sdk->hooking->Attach(pluginHandle, target, detour, original) || !*original)
    {
        Log(std::string("look: could not hook ") + what + ", partners keep our look");
        return false;
    }
    return true;
}
}

bool Install(const RED4ext::v1::Sdk* sdk, RED4ext::v1::PluginHandle pluginHandle, LogFunction log)
{
    Log = std::move(log);
    if (HooksInstalled)
    {
        return true;
    }
    const uintptr_t base = GameImageBase();
    if (!base)
    {
        Log("look: no game image, partners keep our look");
        return false;
    }
    if (!AttachChecked(sdk, pluginHandle, base, GetStateForGenderOffset, GetStateForGenderBytes, reinterpret_cast<void*>(&NightCoopGetStateForGenderEntry),
                       reinterpret_cast<void**>(&OriginalGetStateForGender), "the customization systems GetState(isMale)") ||
        !AttachChecked(sdk, pluginHandle, base, GetStateOffset, GetStateBytes, reinterpret_cast<void*>(&NightCoopGetStateEntry),
                       reinterpret_cast<void**>(&OriginalGetState), "the customization systems GetState()") ||
        !AttachChecked(sdk, pluginHandle, base, LookPartOffset, LookPartBytes, reinterpret_cast<void*>(&OnLookPart),
                       reinterpret_cast<void**>(&OriginalLookPart), "the look part function"))
    {
        return false;
    }
    HooksInstalled = true;
    Log("look: GetState(isMale), GetState() and the look part function hooked, partner bodies can wear their own look");
    return true;
}

bool IsInstalled()
{
    return HooksInstalled;
}

void SetPartnerState(uint64_t entityHash, const RED4ext::Handle<RED4ext::IScriptable>& state, bool isMale)
{
    std::lock_guard guard(PartnerStatesLock);
    PartnerStates[entityHash] = {state, isMale};
    PartnerStateCount.store(static_cast<uint32_t>(PartnerStates.size()), std::memory_order_release);
}

void ForgetPartnerState(uint64_t entityHash)
{
    std::lock_guard guard(PartnerStatesLock);
    PartnerStates.erase(entityHash);
    PartnerStateCount.store(static_cast<uint32_t>(PartnerStates.size()), std::memory_order_release);
}

void SetOwnGender(bool isMale)
{
    OwnGender.store(isMale ? 1 : 0, std::memory_order_release);
}

uint32_t TakeAnsweredCount()
{
    return AnsweredCount.exchange(0, std::memory_order_relaxed);
}

std::string TakeTrafficReport()
{
    const uint32_t partners = PartsOfPartners.exchange(0, std::memory_order_relaxed);
    const uint32_t others = PartsOfOthers.exchange(0, std::memory_order_relaxed);
    const uint32_t inScope = GenderAsksInScope.exchange(0, std::memory_order_relaxed);
    const uint32_t outside = GenderAsksOutside.exchange(0, std::memory_order_relaxed);
    if (!partners && !others && !inScope && !outside)
    {
        return {};
    }
    const uint32_t byCaller = FoundByCaller.exchange(0, std::memory_order_relaxed);
    return "look: look parts of partner bodies " + std::to_string(partners) + ", of others " + std::to_string(others) + "; GetState(isMale) for a partner " +
           std::to_string(inScope) + " (" + std::to_string(byCaller) + " found by the callers registers), others " + std::to_string(outside);
}

void ForgetAllPartnerStates()
{
    std::lock_guard guard(PartnerStatesLock);
    PartnerStates.clear();
    PartnerStateCount.store(0, std::memory_order_release);
}
}
