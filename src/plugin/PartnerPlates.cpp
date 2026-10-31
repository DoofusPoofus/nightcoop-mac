#include "plugin/PartnerPlates.hpp"

#include "plugin/GameCalls.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nightcoop_mac::partner_plates
{
namespace
{
constexpr float HeadHeight = 2.05f;
constexpr float HudWidth = 3840.0f;
constexpr float HudHeight = 2160.0f;
constexpr float FarthestMeters = 2000.0f;
constexpr uint64_t ReportEveryMs = 10000;

struct TransformLayout
{
    RED4ext::Vector4 Position;
    float Orientation[4];
};

struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* GetCameraSystem = nullptr;
    RED4ext::CBaseFunction* ProjectPoint = nullptr;
    RED4ext::CBaseFunction* CameraTransform = nullptr;
    RED4ext::CBaseFunction* CameraForward = nullptr;
    RED4ext::CBaseFunction* CameraRight = nullptr;
    RED4ext::CBaseFunction* CameraUp = nullptr;
    RED4ext::CBaseFunction* BeginFrame = nullptr;
    RED4ext::CBaseFunction* Place = nullptr;
    RED4ext::CBaseFunction* EndFrame = nullptr;
    RED4ext::CBaseFunction* HasLayer = nullptr;
    RED4ext::CBaseFunction* PlatesMade = nullptr;
};

Calls Game;
bool Calibrated = false;
float XSign = 1.0f;
float YSign = 1.0f;
uint64_t NextReportMs = 0;
std::string LastPlacement = "none";

bool Resolve(const LogFunction& log)
{
    if (Game.Resolved)
    {
        return Game.Usable;
    }
    Game.Resolved = true;
    Game.GetCameraSystem = game::FindFunction("ScriptGameInstance", "GetCameraSystem");
    Game.ProjectPoint = game::FindFunction("gameCameraSystem", "ProjectPoint");
    Game.CameraTransform = game::FindFunction("gameCameraSystem", "GetActiveCameraWorldTransform");
    Game.CameraForward = game::FindFunction("gameCameraSystem", "GetActiveCameraForward");
    Game.CameraRight = game::FindFunction("gameCameraSystem", "GetActiveCameraRight");
    Game.CameraUp = game::FindFunction("gameCameraSystem", "GetActiveCameraUp");
    Game.BeginFrame = game::FindFunction("NightCoopPartnerPlates", "BeginFrame");
    Game.Place = game::FindFunction("NightCoopPartnerPlates", "Place");
    Game.EndFrame = game::FindFunction("NightCoopPartnerPlates", "EndFrame");
    Game.HasLayer = game::FindFunction("NightCoopPartnerPlates", "HasLayer");
    Game.PlatesMade = game::FindFunction("NightCoopPartnerPlates", "PlatesMade");
    Game.Usable = game::ParamsAre(Game.GetCameraSystem, {"ScriptGameInstance"}) && game::ParamsAre(Game.ProjectPoint, {"Vector4"}) &&
                  game::ParamsAre(Game.CameraTransform, {"Transform"}) && game::ParamsAre(Game.CameraForward, {}) && game::ParamsAre(Game.CameraRight, {}) &&
                  game::ParamsAre(Game.CameraUp, {}) && game::ParamsAre(Game.BeginFrame, {}) &&
                  game::ParamsAre(Game.Place, {"Int32", "String", "Float", "Float", "Float", "Float", "String"}) && game::ParamsAre(Game.EndFrame, {}) &&
                  game::ParamsAre(Game.HasLayer, {}) && game::ParamsAre(Game.PlatesMade, {});
    log(Game.Usable ? "plates: partners get a name plate over their head" : "plates: the camera or plate script calls look different than expected, no name plates");
    return Game.Usable;
}

RED4ext::Vector4 Project(RED4ext::Handle<RED4ext::IScriptable>& cameras, const RED4ext::Vector4& point)
{
    RED4ext::Vector4 projected {};
    RED4ext::Vector4 worldPoint = point;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &worldPoint);
    game::CallMethod(cameras.instance, Game.ProjectPoint, &projected, args);
    return projected;
}

RED4ext::Vector4 CameraVector(RED4ext::Handle<RED4ext::IScriptable>& cameras, RED4ext::CBaseFunction* getter)
{
    RED4ext::Vector4 vector {};
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(cameras.instance, getter, &vector, noArgs);
    return vector;
}

std::string DistanceText(float meters)
{
    char text[32] = "";
    if (meters >= 1000.0f)
    {
        std::snprintf(text, sizeof(text), "%.1f km", meters / 1000.0f);
    }
    else if (meters >= 100.0f)
    {
        std::snprintf(text, sizeof(text), "%d m", static_cast<int>(std::round(meters / 5.0f)) * 5);
    }
    else if (meters >= 8.0f)
    {
        std::snprintf(text, sizeof(text), "%d m", static_cast<int>(std::round(meters)));
    }
    return text;
}
}

void Tick(uint64_t nowMs, bool inSave, const std::vector<PartnerSpot>& partners, const LogFunction& log)
{
    if (!inSave || !Resolve(log))
    {
        return;
    }
    auto plates = game::ScriptService("NightCoopPartnerPlates");
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> cameras;
    RED4ext::StackArgs_t cameraArgs;
    cameraArgs.emplace_back(nullptr, &gameInstance);
    if (!plates || !game::CallStatic(Game.GetCameraSystem, &cameras, cameraArgs) || !cameras)
    {
        return;
    }
    RED4ext::StackArgs_t noArgs;
    TransformLayout camera {};
    RED4ext::StackArgs_t transformArgs;
    transformArgs.emplace_back(nullptr, &camera);
    bool haveCamera = false;
    game::CallMethod(cameras.instance, Game.CameraTransform, &haveCamera, transformArgs);
    const RED4ext::Vector4 forward = CameraVector(cameras, Game.CameraForward);
    if (!haveCamera)
    {
        return;
    }
    if (!Calibrated)
    {
        const RED4ext::Vector4 right = CameraVector(cameras, Game.CameraRight);
        const RED4ext::Vector4 up = CameraVector(cameras, Game.CameraUp);
        const RED4ext::Vector4 ahead {camera.Position.X + forward.X * 10.0f, camera.Position.Y + forward.Y * 10.0f, camera.Position.Z + forward.Z * 10.0f, 1.0f};
        const RED4ext::Vector4 aheadRight {ahead.X + right.X, ahead.Y + right.Y, ahead.Z + right.Z, 1.0f};
        const RED4ext::Vector4 aheadUp {ahead.X + up.X, ahead.Y + up.Y, ahead.Z + up.Z, 1.0f};
        const RED4ext::Vector4 projectedRight = Project(cameras, aheadRight);
        const RED4ext::Vector4 projectedUp = Project(cameras, aheadUp);
        if (projectedRight.X != 0.0f || projectedUp.Y != 0.0f)
        {
            XSign = projectedRight.X < 0.0f ? -1.0f : 1.0f;
            YSign = projectedUp.Y < 0.0f ? -1.0f : 1.0f;
            Calibrated = true;
            char line[160];
            std::snprintf(line, sizeof(line), "plates: projection checked, 1 m right gives x %.3f, 1 m up gives y %.3f", projectedRight.X, projectedUp.Y);
            log(line);
        }
    }
    game::CallMethod(plates.instance, Game.BeginFrame, nullptr, noArgs);
    for (const auto& partner : partners)
    {
        const RED4ext::Vector4 anchor {partner.X, partner.Y, partner.Z + HeadHeight, 1.0f};
        const float deltaX = anchor.X - camera.Position.X;
        const float deltaY = anchor.Y - camera.Position.Y;
        const float deltaZ = anchor.Z - camera.Position.Z;
        const float depth = deltaX * forward.X + deltaY * forward.Y + deltaZ * forward.Z;
        const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
        if (depth < 0.1f || !std::isfinite(distance) || distance > FarthestMeters)
        {
            continue;
        }
        const RED4ext::Vector4 projected = Project(cameras, anchor);
        const float screenX = (projected.X * XSign + 1.0f) * 0.5f * HudWidth;
        const float screenY = (1.0f - projected.Y * YSign) * 0.5f * HudHeight;
        if (std::fabs(projected.X) > 1.0f || std::fabs(projected.Y) > 1.0f)
        {
            continue;
        }
        float scale = 1.0f - std::clamp((distance - 15.0f) / 285.0f, 0.0f, 1.0f) * 0.28f;
        float opacity = distance < 6.0f ? std::clamp((distance - 2.5f) / 3.5f, 0.0f, 1.0f) * 0.65f + 0.35f : 1.0f;
        int32_t peer = static_cast<int32_t>(partner.PeerNumber);
        RED4ext::CString name(partner.Name.c_str());
        RED4ext::CString distanceText(DistanceText(distance).c_str());
        float x = screenX;
        float y = screenY;
        RED4ext::StackArgs_t placeArgs;
        placeArgs.emplace_back(nullptr, &peer);
        placeArgs.emplace_back(nullptr, &name);
        placeArgs.emplace_back(nullptr, &x);
        placeArgs.emplace_back(nullptr, &y);
        placeArgs.emplace_back(nullptr, &scale);
        placeArgs.emplace_back(nullptr, &opacity);
        placeArgs.emplace_back(nullptr, &distanceText);
        game::CallMethod(plates.instance, Game.Place, nullptr, placeArgs);
        char placement[96];
        std::snprintf(placement, sizeof(placement), "peer %u at %.0f %.0f (%.1f m)", partner.PeerNumber, screenX, screenY, distance);
        LastPlacement = placement;
    }
    game::CallMethod(plates.instance, Game.EndFrame, nullptr, noArgs);
    if (nowMs >= NextReportMs)
    {
        NextReportMs = nowMs + ReportEveryMs;
        bool hasLayer = false;
        int32_t made = 0;
        game::CallMethod(plates.instance, Game.HasLayer, &hasLayer, noArgs);
        game::CallMethod(plates.instance, Game.PlatesMade, &made, noArgs);
        log(std::string("plates: hud layer ") + (hasLayer ? "there" : "not found yet") + ", " + std::to_string(made) + " plate(s) made, last " + LastPlacement);
    }
}
}
