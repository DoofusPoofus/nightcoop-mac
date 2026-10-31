#include "plugin/Hud.hpp"

#include "plugin/GameCalls.hpp"

#include <RED4ext/RED4ext.hpp>

namespace nightcoop_mac::hud
{
void Show(const std::string& text, float seconds)
{
    static auto* show = game::FindFunction("NightCoopHud", "Show");
    if (!show || !game::ParamsAre(show, {"String", "Float"}))
    {
        return;
    }
    auto service = game::ScriptService("NightCoopHud");
    if (!service)
    {
        return;
    }
    RED4ext::CString message(text.c_str());
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &message);
    args.emplace_back(nullptr, &seconds);
    game::CallMethod(service.instance, show, nullptr, args);
}
}
