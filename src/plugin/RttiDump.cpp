#include "plugin/RttiDump.hpp"

#include <RED4ext/RED4ext.hpp>

#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

#include <dlfcn.h>
#include <mach-o/dyld.h>

#include <cstring>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace nightcoop_mac
{
namespace
{
std::string TypeName(const RED4ext::rtti::IType* type)
{
    return type ? type->GetName().ToString() : std::string("void");
}

void WriteFunction(std::ofstream& out, const char* kind, RED4ext::CBaseFunction* function)
{
    out << "  " << kind << " " << function->shortName.ToString() << "  [" << function->fullName.ToString() << "]";
    out << (function->flags.isStatic ? " static" : "") << (function->flags.isNative ? " native" : " scripted");
    out << " -> " << (function->returnType ? TypeName(function->returnType->type) : std::string("void")) << " (";
    for (uint32_t index = 0; index < function->params.Size(); ++index)
    {
        const auto* param = function->params[index];
        out << (index ? ", " : "") << param->name.ToString() << ": " << TypeName(param->type);
    }
    out << ")\n";
}

void WriteVtableSlots(std::ofstream& out, const char* what, RED4ext::IScriptable* object)
{
    const auto vtable = *reinterpret_cast<uintptr_t*>(object);
    Dl_info info {};
    dladdr(reinterpret_cast<void*>(vtable), &info);
    const auto imageBase = reinterpret_cast<uintptr_t>(info.dli_fbase);
    char line[200];
    std::snprintf(line, sizeof(line), "vtable %s: object %p is a %s, vtable game+%#lx\n", what, static_cast<void*>(object),
                  object->GetType()->GetName().ToString(), static_cast<unsigned long>(vtable - imageBase));
    out << line;
    for (uintptr_t slot = 0; slot < 0x300; slot += 8)
    {
        const auto function = *reinterpret_cast<uintptr_t*>(vtable + slot);
        std::snprintf(line, sizeof(line), "  slot %#05lx -> game+%#lx\n", static_cast<unsigned long>(slot), static_cast<unsigned long>(function - imageBase));
        out << line;
    }
    out << "\n";
}

void WriteSystemVtable(std::ofstream& out, const std::string& getterName, bool stateOfIt)
{
    auto* scriptGameInstance = RED4ext::CRTTISystem::Get()->GetClass("ScriptGameInstance");
    auto* getter = scriptGameInstance ? scriptGameInstance->GetFunction(getterName.c_str()) : nullptr;
    if (!getter)
    {
        out << "vtable " << getterName << ": no such getter\n\n";
        return;
    }
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> system;
    RED4ext::StackArgs_t args;
    if (getter->params.Size() == 1)
    {
        args.emplace_back(nullptr, &gameInstance);
    }
    if (!RED4ext::ExecuteFunction(static_cast<void*>(nullptr), getter, &system, args) || !system)
    {
        out << "vtable " << getterName << ": getter gave nothing\n\n";
        return;
    }
    if (!stateOfIt)
    {
        WriteVtableSlots(out, getterName.c_str(), system.instance);
        return;
    }
    auto* getState = system.instance->GetType()->GetFunction("GetState");
    RED4ext::Handle<RED4ext::IScriptable> state;
    RED4ext::StackArgs_t noArgs;
    if (!getState || !RED4ext::ExecuteFunction(system.instance, getState, &state, noArgs) || !state)
    {
        auto* stateClass = RED4ext::CRTTISystem::Get()->GetClass("gameuiCharacterCustomizationState");
        auto* freshState = stateClass ? static_cast<RED4ext::IScriptable*>(stateClass->CreateInstance(true)) : nullptr;
        if (!freshState)
        {
            out << "statevtable " << getterName << ": no state and none could be made\n\n";
            return;
        }
        state = RED4ext::Handle<RED4ext::IScriptable>(freshState);
        out << "(no state in the system yet, this is a fresh one)\n";
    }
    WriteVtableSlots(out, "state", state.instance);
}

void WriteClass(std::ofstream& out, RED4ext::CRTTISystem* rtti, const std::string& className)
{
    auto* classType = rtti->GetClass(className.c_str());
    if (!classType)
    {
        out << "class " << className << ": not found\n\n";
        return;
    }
    out << "class " << className << " size " << classType->GetSize() << " parents:";
    for (auto* parent = classType->parent; parent; parent = parent->parent)
    {
        out << " " << parent->name.ToString();
    }
    out << "\n";
    for (uint32_t index = 0; index < classType->props.Size(); ++index)
    {
        const auto* property = classType->props[index];
        out << "  prop " << property->name.ToString() << ": " << TypeName(property->type) << " @0x" << std::hex
            << property->valueOffset << std::dec << "\n";
    }
    for (uint32_t index = 0; index < classType->funcs.Size(); ++index)
    {
        WriteFunction(out, "func", classType->funcs[index]);
    }
    for (uint32_t index = 0; index < classType->staticFuncs.Size(); ++index)
    {
        WriteFunction(out, "static", classType->staticFuncs[index]);
    }
    out << "\n";
}
}

bool WriteRttiDumpIfRequested(const std::filesystem::path& pluginFolder)
{
    std::ifstream request(pluginFolder / "rtti-dump-request.txt");
    if (!request)
    {
        return false;
    }
    auto* rtti = RED4ext::CRTTISystem::Get();
    std::ofstream out(pluginFolder / "rtti-dump.txt");
    std::string line;
    while (std::getline(request, line))
    {
        std::istringstream words(line);
        std::string kind;
        std::string name;
        if (!(words >> kind >> name) || kind[0] == '#')
        {
            continue;
        }
        if (kind == "class")
        {
            WriteClass(out, rtti, name);
        }
        else if (kind == "classes")
        {
            RED4ext::DynArray<RED4ext::CClass*> classes;
            rtti->GetClasses(rtti->GetClass("IScriptable"), classes, nullptr, true);
            uint32_t written = 0;
            for (auto* classType : classes)
            {
                const std::string className = classType->name.ToString();
                if (className.find(name) != std::string::npos && written++ < 40)
                {
                    WriteClass(out, rtti, className);
                }
            }
            out << "classes with " << name << " in the name: " << written << "\n\n";
        }
        else if (kind == "enum")
        {
            auto* enumType = rtti->GetEnum(name.c_str());
            if (!enumType)
            {
                out << "enum " << name << ": not found\n\n";
                continue;
            }
            out << "enum " << name << " size " << static_cast<int>(enumType->actualSize) << "\n";
            for (uint32_t index = 0; index < enumType->hashList.Size() && index < enumType->valueList.Size(); ++index)
            {
                out << "  " << enumType->hashList[index].ToString() << " = " << enumType->valueList[index] << "\n";
            }
            out << "\n";
        }
        else if (kind == "peek")
        {
            Dl_info info {};
            dladdr(reinterpret_cast<void*>(&WriteRttiDumpIfRequested), &info);
            uintptr_t gameBase = 0;
            for (uint32_t index = 0; index < _dyld_image_count(); ++index)
            {
                const char* imageName = _dyld_get_image_name(index);
                if (imageName && std::strstr(imageName, "/Contents/MacOS/Cyberpunk2077"))
                {
                    gameBase = reinterpret_cast<uintptr_t>(_dyld_get_image_header(index));
                }
            }
            const uintptr_t offset = std::stoull(name, nullptr, 16);
            const auto* words = reinterpret_cast<const uintptr_t*>(gameBase + offset);
            char line[200];
            std::snprintf(line, sizeof(line), "peek game+%#lx: %#lx %#lx %#lx %#lx (as game offsets: %#lx %#lx)\n", static_cast<unsigned long>(offset),
                          static_cast<unsigned long>(words[0]), static_cast<unsigned long>(words[1]), static_cast<unsigned long>(words[2]),
                          static_cast<unsigned long>(words[3]), static_cast<unsigned long>(words[0] - gameBase), static_cast<unsigned long>(words[2] - gameBase));
            out << line;
        }
        else if (kind == "vtable" || kind == "statevtable")
        {
            WriteSystemVtable(out, name, kind == "statevtable");
        }
        else if (kind == "function")
        {
            auto* function = rtti->GetFunction(name.c_str());
            if (function)
            {
                WriteFunction(out, "global", function);
            }
            else
            {
                out << "  global " << name << ": not found\n";
            }
        }
    }
    return true;
}
}
