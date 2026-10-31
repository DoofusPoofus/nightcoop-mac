#include "plugin/GameCalls.hpp"

#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

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
    const RED4ext::CName wantedName(functionName);
    auto named = [&wantedName](RED4ext::CBaseFunction* function) { return function->fullName == wantedName || function->shortName == wantedName; };
    for (auto* function : classType->funcs)
    {
        if (named(function))
        {
            return function;
        }
    }
    for (auto* function : classType->staticFuncs)
    {
        if (named(function))
        {
            return function;
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

RED4ext::Handle<RED4ext::IScriptable> ScriptService(const char* serviceName)
{
    static auto* getContainer = FindFunction("ScriptGameInstance", "GetScriptableServiceContainer");
    static auto* getService = FindFunction("ScriptableServiceContainer", "GetService");
    RED4ext::Handle<RED4ext::IScriptable> container;
    RED4ext::Handle<RED4ext::IScriptable> service;
    if (!getContainer || !getService)
    {
        return service;
    }
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::StackArgs_t containerArgs;
    if (getContainer->params.Size() == 1)
    {
        containerArgs.emplace_back(nullptr, &gameInstance);
    }
    if (!CallStatic(getContainer, &container, containerArgs) || !container)
    {
        return service;
    }
    RED4ext::CName name(serviceName);
    RED4ext::StackArgs_t serviceArgs;
    serviceArgs.emplace_back(nullptr, &name);
    auto* wanted = RED4ext::CRTTISystem::Get()->GetClass(name);
    if (!CallMethod(container.instance, getService, &service, serviceArgs) || !service || !wanted || !service->GetType() ||
        !service->GetType()->IsA(wanted))
    {
        service.Reset();
    }
    return service;
}
}
