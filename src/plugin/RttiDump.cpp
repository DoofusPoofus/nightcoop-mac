#include "plugin/RttiDump.hpp"

#include <RED4ext/RED4ext.hpp>

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
