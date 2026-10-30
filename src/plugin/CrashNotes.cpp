#include "plugin/CrashNotes.hpp"

#include <RED4ext/RED4ext.hpp>

#include <dlfcn.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace nightcoop_mac
{
namespace
{
char NotesPath[1024] = {};
struct sigaction PreviousSegv = {};
struct sigaction PreviousBus = {};
volatile sig_atomic_t AlreadyNoting = 0;
volatile sig_atomic_t Probing = 0;
sigjmp_buf ProbeJump;

bool ReadWord(uintptr_t address, uintptr_t& value)
{
    if (address < 0x100000000 || (address & 7) != 0)
    {
        return false;
    }
    vm_size_t readBytes = 0;
    return vm_read_overwrite(mach_task_self(), address, sizeof(value), reinterpret_cast<vm_address_t>(&value), &readBytes) == KERN_SUCCESS &&
           readBytes == sizeof(value);
}

bool InGameImage(uintptr_t address)
{
    Dl_info info {};
    return dladdr(reinterpret_cast<const void*>(address), &info) && info.dli_fname && std::strstr(info.dli_fname, "Cyberpunk2077");
}

std::string DescribeObject(uintptr_t address)
{
    char line[400];
    Dl_info addressInfo {};
    if (dladdr(reinterpret_cast<const void*>(address), &addressInfo) && addressInfo.dli_fname)
    {
        const char* imageName = std::strrchr(addressInfo.dli_fname, '/');
        std::snprintf(line, sizeof(line), "inside %s+%#lx", imageName ? imageName + 1 : addressInfo.dli_fname,
                      static_cast<unsigned long>(address - reinterpret_cast<uintptr_t>(addressInfo.dli_fbase)));
        return line;
    }
    uintptr_t vtable = 0;
    if (!ReadWord(address, vtable))
    {
        return {};
    }
    Dl_info vtableInfo {};
    const bool vtableKnown = dladdr(reinterpret_cast<const void*>(vtable), &vtableInfo) && vtableInfo.dli_fname;
    const uintptr_t vtableOffset = vtableKnown ? vtable - reinterpret_cast<uintptr_t>(vtableInfo.dli_fbase) : 0;
    if (!vtableKnown || !std::strstr(vtableInfo.dli_fname, "Cyberpunk2077"))
    {
        return {};
    }
    uintptr_t getType = 0;
    if (!ReadWord(vtable + sizeof(uintptr_t), getType) || !InGameImage(getType))
    {
        std::snprintf(line, sizeof(line), "object with vtable game+%#lx (no GetType)", static_cast<unsigned long>(vtableOffset));
        return line;
    }
    auto* object = reinterpret_cast<RED4ext::ISerializable*>(address);
    auto* classType = object->GetType();
    uintptr_t classVtable = 0;
    if (!classType || !ReadWord(reinterpret_cast<uintptr_t>(classType), classVtable) || !InGameImage(classVtable))
    {
        std::snprintf(line, sizeof(line), "object with vtable game+%#lx, GetType gave %p", static_cast<unsigned long>(vtableOffset),
                      static_cast<void*>(classType));
        return line;
    }
    const char* name = classType->GetName().ToString();
    std::snprintf(line, sizeof(line), "%s (vtable game+%#lx)", name && *name ? name : "?", static_cast<unsigned long>(vtableOffset));
    return line;
}

std::string DescribeCustomizationLookup(uintptr_t component)
{
    using StateSlot = RED4ext::Handle<RED4ext::IScriptable> (*)(void*);
    uintptr_t vtable = 0;
    uintptr_t slot = 0;
    if (!ReadWord(component, vtable) || !ReadWord(vtable + 0x2a8, slot) || !InGameImage(slot))
    {
        return "slot 0x2a8 unreadable";
    }
    const auto handed = reinterpret_cast<StateSlot>(slot)(reinterpret_cast<void*>(component));
    const uintptr_t instance = reinterpret_cast<uintptr_t>(handed.instance);
    uintptr_t lookupId = 0;
    ReadWord(instance + 0x25c, lookupId);
    uint32_t lowHalf = 0;
    uint32_t highHalf = 0;
    vm_size_t readBytes = 0;
    vm_read_overwrite(mach_task_self(), instance + 0x25c, 4, reinterpret_cast<vm_address_t>(&lowHalf), &readBytes);
    vm_read_overwrite(mach_task_self(), instance + 0x260, 4, reinterpret_cast<vm_address_t>(&highHalf), &readBytes);
    char line[400];
    std::snprintf(line, sizeof(line), "slot 0x2a8 handed %p (%s), its +0x25c is %#x%08x", reinterpret_cast<void*>(instance),
                  instance ? DescribeObject(instance).c_str() : "nothing", highHalf, lowHalf);
    return line;
}

void WriteLine(int file, const std::string& line)
{
    const std::string withEnd = line + "\n";
    write(file, withEnd.data(), withEnd.size());
}

void OnCrash(int signalNumber, siginfo_t* info, void* context)
{
    if (Probing)
    {
        siglongjmp(ProbeJump, 1);
    }
    if (!AlreadyNoting)
    {
        AlreadyNoting = 1;
        const int file = open(NotesPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (file >= 0)
        {
            auto* machineContext = static_cast<ucontext_t*>(context)->uc_mcontext;
            char head[160];
            std::snprintf(head, sizeof(head), "crash: signal %d at %p, pc %p", signalNumber, info ? info->si_addr : nullptr,
                          reinterpret_cast<void*>(machineContext->__ss.__pc));
            WriteLine(file, head);
            for (int index = 0; index < 29; ++index)
            {
                const uintptr_t value = machineContext->__ss.__x[index];
                std::string described;
                if (sigsetjmp(ProbeJump, 1) == 0)
                {
                    Probing = 1;
                    described = DescribeObject(value);
                    Probing = 0;
                }
                else
                {
                    Probing = 0;
                    described = "something that faulted when asked for its type";
                }
                char line[500];
                std::snprintf(line, sizeof(line), "  x%d = %p%s%s", index, reinterpret_cast<void*>(value), described.empty() ? "" : " is a ",
                              described.c_str());
                WriteLine(file, line);
                if (described.find("CharacterCustomization") != std::string::npos && described.find("Controller") != std::string::npos)
                {
                    std::string lookup;
                    if (sigsetjmp(ProbeJump, 1) == 0)
                    {
                        Probing = 1;
                        lookup = DescribeCustomizationLookup(value);
                        Probing = 0;
                    }
                    else
                    {
                        Probing = 0;
                        lookup = "calling slot 0x2a8 faulted";
                    }
                    WriteLine(file, "    " + lookup);
                }
            }
            close(file);
        }
    }
    sigaction(SIGSEGV, &PreviousSegv, nullptr);
    sigaction(SIGBUS, &PreviousBus, nullptr);
}


void PutOnTop(int signalNumber, struct sigaction& previous)
{
    struct sigaction current = {};
    if (sigaction(signalNumber, nullptr, &current) != 0 || ((current.sa_flags & SA_SIGINFO) && current.sa_sigaction == &OnCrash))
    {
        return;
    }
    struct sigaction ours = {};
    ours.sa_sigaction = &OnCrash;
    ours.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&ours.sa_mask);
    sigaction(signalNumber, &ours, &previous);
}
}

// the mac binary has like zero symbols so the crash gotta snitch on itself
void InstallCrashNotes(const std::filesystem::path& pluginFolder)
{
    std::snprintf(NotesPath, sizeof(NotesPath), "%s", (pluginFolder / "crash-notes.txt").string().c_str());
    KeepCrashNotesOnTop();
}

void KeepCrashNotesOnTop()
{
    if (!NotesPath[0])
    {
        return;
    }
    PutOnTop(SIGSEGV, PreviousSegv);
    PutOnTop(SIGBUS, PreviousBus);
}
}
