#include "plugin/GameCalls.hpp"

namespace nightcoop_mac::game
{
namespace
{
std::string TypeName(RED4ext::rtti::IType* type)
{
    return type ? type->GetName().ToString() : std::string("void");
}
}

RED4ext::CBaseFunction* FindFunction(const char* className, const char* functionName)
{
    auto* classType = RED4ext::CRTTISystem::Get()->GetClass(className);
    if (!classType)
    {
        return nullptr;
    }
    if (auto* function = classType->GetFunction(functionName))
    {
        return function;
    }
    const RED4ext::CName shortName(functionName);
    for (uint32_t index = 0; index < classType->funcs.Size(); ++index)
    {
        if (classType->funcs[index]->shortName == shortName)
        {
            return classType->funcs[index];
        }
    }
    for (uint32_t index = 0; index < classType->staticFuncs.Size(); ++index)
    {
        if (classType->staticFuncs[index]->shortName == shortName)
        {
            return classType->staticFuncs[index];
        }
    }
    return nullptr;
}

bool ParamsAre(RED4ext::CBaseFunction* function, std::initializer_list<const char*> typeNames)
{
    if (!function || function->params.Size() != typeNames.size())
    {
        return false;
    }
    uint32_t index = 0;
    for (const char* expected : typeNames)
    {
        if (TypeName(function->params[index++]->type) != expected)
        {
            return false;
        }
    }
    return true;
}

bool ReturnIs(RED4ext::CBaseFunction* function, const char* typeName)
{
    if (!function)
    {
        return false;
    }
    const std::string actual = function->returnType ? TypeName(function->returnType->type) : std::string("void");
    return actual == typeName;
}

bool CallStatic(RED4ext::CBaseFunction* function, void* out, RED4ext::StackArgs_t& args)
{
    return function && RED4ext::ExecuteFunction(static_cast<void*>(nullptr), function, out, args);
}

bool CallMethod(void* instance, RED4ext::CBaseFunction* function, void* out, RED4ext::StackArgs_t& args)
{
    return instance && function && RED4ext::ExecuteFunction(instance, function, out, args);
}

std::string Describe(RED4ext::CBaseFunction* function)
{
    if (!function)
    {
        return "missing";
    }
    std::string text = std::string(function->fullName.ToString()) + "(";
    for (uint32_t index = 0; index < function->params.Size(); ++index)
    {
        text += std::string(index ? ", " : "") + TypeName(function->params[index]->type);
    }
    return text + ") -> " + (function->returnType ? TypeName(function->returnType->type) : std::string("void"));
}
}
