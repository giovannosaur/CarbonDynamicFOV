#include "pch.h"
#include "Config.h"
#include "Utils.h"
#include "DynamicFov.h"

#include <windows.h>
#include <stdint.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// -----------------------------------------------------------------------------
// Config
// -----------------------------------------------------------------------------
int      cfg_toggleKey = 8;       // VK_BACK
uint16_t cfg_initial_fov = 15000;
uint16_t cfg_max_fov = 25000;
float    cfg_max_speed = 305.49f;
int      cfg_graph_type = 3;
bool     cfg_showHudMsg = true;
bool     cfg_permanentEnable = true;

float    cfg_fov_sensitivity = 7.0f;
float    cfg_expand_sensitivity = 7.0f;
float    cfg_recover_sensitivity = 3.0f;
float    cfg_effect_headroom = 6000.0f;
float    cfg_effect_headroom_below = 1500.0f;

bool     cfg_accel_enabled = true;
float    cfg_accel_reference = 43.128f;
float    cfg_accel_gain = 3000.0f;
float    cfg_accel_sensitivity = 12.0f;

bool     cfg_nos_enabled = true;
float    cfg_nos_gain = 1000.0f;
bool     cfg_shift_enabled = true;
float    cfg_shift_gain = 500.0f;
float    cfg_shift_duration = 0.22f;
// Minimum in-game speed value required to trigger/use a shifting FOV pulse
float    cfg_shift_min_speed = 22.2828f;
bool     cfg_engine_braking_enabled = true;
// Signed additive FOV adjustment while engine braking is active. Negative narrows
float    cfg_engine_braking_gain = -250.0f;
bool     cfg_debugEngineLookup = true;

// Camera control
bool     cfg_drivingCameraOnly = true;
bool     cfg_speedbreakerEnabled = true;
uint16_t cfg_speedbreakerFov = 12000;  // lower value = zoom in
// Independent speedbreaker transition response rates (per second)
// Lower values create slower, more gradual FOV transitions
float    cfg_speedbreakerEnterSensitivity = 2.0f;
float    cfg_speedbreakerExitSensitivity = 1.2f;

// External speed-based shake applied to the final camera view matrix
// The effect applies a local yaw/pitch rotation while preserving camera position
bool     cfg_speedShakeEnabled = true;
float    cfg_speedShakeMinSpeed = 22.2828f;
float    cfg_speedShakeMaxSpeed = 305.49f;

// maximum local camera rotation at full shake intensity, in degrees.
float cfg_speedShakeYawDegrees = 2.6f;
float cfg_speedShakePitchDegrees = 2.6f;

float    cfg_speedShakeMinFrequency = 1.0f;
float    cfg_speedShakeMaxFrequency = 11.5f;
float    cfg_speedShakeSmoothing = 9.0f;

// Optional engine-braking modifier. Multipliers below 1 make the shake subtler and slower while the car engine-braking state is active
bool  cfg_speedShakeEngineBrakeModifierEnabled = true;
float cfg_speedShakeEngineBrakeStrengthMultiplier = 0.55f;
float cfg_speedShakeEngineBrakeFrequencyMultiplier = 0.55f;
float cfg_speedShakeEngineBrakeSmoothing = 9.0f;

bool     cfg_speedShakeDebug = true;

static float ReadIniFloat(const char* section, const char* key, float fallback)
{
    char defaultText[48] = {};
    char buffer[96] = {};
    std::snprintf(defaultText, sizeof(defaultText), "%.7g", fallback);
    GetPrivateProfileStringA(section, key, defaultText,
        buffer, static_cast<DWORD>(sizeof(buffer)),
        ".\\DFconfig.ini");

    char* end = nullptr;
    const float value = std::strtof(buffer, &end);
    if (end == buffer || !std::isfinite(value))
        return fallback;
    return value;
}

void LoadConfig()
{
    cfg_toggleKey = GetPrivateProfileIntA(
        "hotkeys", "toggle_fov", 8, ".\\DFconfig.ini");

    const int initialFov = GetPrivateProfileIntA(
        "settings", "initial_fov", 15000, ".\\DFconfig.ini");
    const int maxFov = GetPrivateProfileIntA(
        "settings", "max_fov", 25000, ".\\DFconfig.ini");

    cfg_initial_fov = static_cast<uint16_t>(
        initialFov < 0 ? 0 : (initialFov > 65535 ? 65535 : initialFov));
    cfg_max_fov = static_cast<uint16_t>(
        maxFov < 0 ? 0 : (maxFov > 65535 ? 65535 : maxFov));

    cfg_graph_type = GetPrivateProfileIntA(
        "settings", "graph_type", 3, ".\\DFconfig.ini");
    cfg_showHudMsg = GetPrivateProfileIntA(
        "settings", "showHudMessage", 1, ".\\DFconfig.ini") != 0;
    cfg_permanentEnable = GetPrivateProfileIntA(
        "settings", "permanentEnable", 1, ".\\DFconfig.ini") != 0;

    cfg_max_speed = ReadIniFloat("speed", "max_speed", 305.49f);

    cfg_fov_sensitivity = ReadIniFloat(
        "smoothing", "fov_sensitivity", 7.0f);
    cfg_expand_sensitivity = ReadIniFloat(
        "smoothing", "expand_sensitivity", 7.0f);
    cfg_recover_sensitivity = ReadIniFloat(
        "smoothing", "recover_sensitivity", 3.0f);
    cfg_effect_headroom = ReadIniFloat(
        "smoothing", "effect_headroom", 6000.0f);
    cfg_effect_headroom_below = ReadIniFloat(
        "smoothing", "effect_headroom_below", 1500.0f);

    cfg_accel_enabled = GetPrivateProfileIntA(
        "acceleration", "enabled", 1, ".\\DFconfig.ini") != 0;
    cfg_accel_reference = ReadIniFloat(
        "acceleration", "reference", 43.128f);
    cfg_accel_gain = ReadIniFloat(
        "acceleration", "gain", 3000.0f);
    cfg_accel_sensitivity = ReadIniFloat(
        "acceleration", "sensitivity", 12.0f);

    cfg_nos_enabled = GetPrivateProfileIntA(
        "effects", "nos_enabled", 1, ".\\DFconfig.ini") != 0;
    cfg_nos_gain = ReadIniFloat("effects", "nos_gain", 1000.0f);
    cfg_shift_enabled = GetPrivateProfileIntA(
        "effects", "shift_enabled", 1, ".\\DFconfig.ini") != 0;
    cfg_shift_gain = ReadIniFloat("effects", "shift_gain", 500.0f);
    cfg_shift_duration = ReadIniFloat(
        "effects", "shift_duration_ms", 220.0f) / 1000.0f;
    cfg_shift_min_speed = ReadIniFloat(
        "effects", "shift_min_speed", 22.2828f);
    if (!std::isfinite(cfg_shift_min_speed) || cfg_shift_min_speed < 0.0f)
        cfg_shift_min_speed = 0.0f;
    cfg_engine_braking_enabled = GetPrivateProfileIntA(
        "effects", "engine_braking_enabled", 1, ".\\DFconfig.ini") != 0;
    cfg_engine_braking_gain = ReadIniFloat(
        "effects", "engine_braking_gain", -250.0f);
    cfg_debugEngineLookup = GetPrivateProfileIntA(
        "diagnostics", "engine_lookup_messages", 1, ".\\DFconfig.ini") != 0;

    cfg_drivingCameraOnly = GetPrivateProfileIntA(
        "camera", "driving_camera_only", 1, ".\\DFconfig.ini") != 0;
    cfg_speedbreakerEnabled = GetPrivateProfileIntA(
        "speedbreaker", "enabled", 1, ".\\DFconfig.ini") != 0;
    const int speedbreakerFov = GetPrivateProfileIntA(
        "speedbreaker", "fov", 12000, ".\\DFconfig.ini");
    cfg_speedbreakerFov = static_cast<uint16_t>(
        speedbreakerFov < 0 ? 0 : (speedbreakerFov > 65535 ? 65535 : speedbreakerFov));
    cfg_speedbreakerEnterSensitivity = ReadIniFloat(
        "speedbreaker", "enter_sensitivity", 2.0f);
    cfg_speedbreakerExitSensitivity = ReadIniFloat(
        "speedbreaker", "exit_sensitivity", 1.2f);

    cfg_speedShakeEnabled = GetPrivateProfileIntA(
        "speed_shake", "enabled", 1, ".\\DFconfig.ini") != 0;
    cfg_speedShakeMinSpeed = ReadIniFloat(
        "speed_shake", "min_speed", 22.2828f);
    cfg_speedShakeMaxSpeed = ReadIniFloat(
        "speed_shake", "max_speed", cfg_max_speed);
    cfg_speedShakeYawDegrees = ReadIniFloat(
        "speed_shake", "yaw_degrees", 2.6f);
    cfg_speedShakePitchDegrees = ReadIniFloat(
        "speed_shake", "pitch_degrees", 2.6f);
    cfg_speedShakeMinFrequency = ReadIniFloat(
        "speed_shake", "min_frequency_hz", 1.0f);
    cfg_speedShakeMaxFrequency = ReadIniFloat(
        "speed_shake", "max_frequency_hz", 11.5f);
    cfg_speedShakeSmoothing = ReadIniFloat(
        "speed_shake", "smoothing", 9.0f);

    cfg_speedShakeEngineBrakeModifierEnabled = GetPrivateProfileIntA(
        "speed_shake", "engine_brake_modifier_enabled", 1,
        ".\\DFconfig.ini") != 0;
    cfg_speedShakeEngineBrakeStrengthMultiplier = ReadIniFloat(
        "speed_shake", "engine_brake_strength_multiplier", 0.55f);
    cfg_speedShakeEngineBrakeFrequencyMultiplier = ReadIniFloat(
        "speed_shake", "engine_brake_frequency_multiplier", 0.55f);
    cfg_speedShakeEngineBrakeSmoothing = ReadIniFloat(
        "speed_shake", "engine_brake_smoothing", 9.0f);

    cfg_speedShakeDebug = GetPrivateProfileIntA(
        "speed_shake", "debug_speed", 1, ".\\DFconfig.ini") != 0;

    cfg_max_speed = SafePositive(cfg_max_speed, 305.49f);
    cfg_fov_sensitivity = SafePositive(cfg_fov_sensitivity, 7.0f);
    cfg_expand_sensitivity = SafePositive(cfg_expand_sensitivity, 7.0f);
    cfg_recover_sensitivity = SafePositive(cfg_recover_sensitivity, 3.0f);
    cfg_accel_reference = SafePositive(cfg_accel_reference, 43.128f);
    cfg_accel_sensitivity = SafePositive(cfg_accel_sensitivity, 12.0f);
    cfg_speedbreakerEnterSensitivity =
        SafePositive(cfg_speedbreakerEnterSensitivity, 2.0f);
    cfg_speedbreakerExitSensitivity =
        SafePositive(cfg_speedbreakerExitSensitivity, 1.2f);

    if (!std::isfinite(cfg_speedShakeMinSpeed) || cfg_speedShakeMinSpeed < 0.0f)
        cfg_speedShakeMinSpeed = 0.0f;
    if (!std::isfinite(cfg_speedShakeMaxSpeed) ||
        cfg_speedShakeMaxSpeed <= cfg_speedShakeMinSpeed + 0.001f)
        cfg_speedShakeMaxSpeed = cfg_speedShakeMinSpeed + 1.0f;
    if (!std::isfinite(cfg_speedShakeYawDegrees))
        cfg_speedShakeYawDegrees = 0.0f;
    cfg_speedShakeYawDegrees = ClampFloat(
        cfg_speedShakeYawDegrees, 0.0f, 5.0f);

    if (!std::isfinite(cfg_speedShakePitchDegrees))
        cfg_speedShakePitchDegrees = 0.0f;
    cfg_speedShakePitchDegrees = ClampFloat(
        cfg_speedShakePitchDegrees, 0.0f, 5.0f);
    cfg_speedShakeMinFrequency = SafePositive(cfg_speedShakeMinFrequency, 1.0f);
    cfg_speedShakeMaxFrequency = SafePositive(cfg_speedShakeMaxFrequency, 11.5f);
    cfg_speedShakeMinFrequency = ClampFloat(cfg_speedShakeMinFrequency, 0.1f, 40.0f);
    cfg_speedShakeMaxFrequency = ClampFloat(cfg_speedShakeMaxFrequency,
        cfg_speedShakeMinFrequency, 40.0f);
    cfg_speedShakeSmoothing = SafePositive(cfg_speedShakeSmoothing, 9.0f);
    cfg_speedShakeSmoothing = ClampFloat(cfg_speedShakeSmoothing, 0.1f, 30.0f);

    if (!std::isfinite(cfg_speedShakeEngineBrakeStrengthMultiplier))
        cfg_speedShakeEngineBrakeStrengthMultiplier = 0.55f;
    cfg_speedShakeEngineBrakeStrengthMultiplier = ClampFloat(
        cfg_speedShakeEngineBrakeStrengthMultiplier, 0.0f, 1.0f);

    if (!std::isfinite(cfg_speedShakeEngineBrakeFrequencyMultiplier))
        cfg_speedShakeEngineBrakeFrequencyMultiplier = 0.55f;
    cfg_speedShakeEngineBrakeFrequencyMultiplier = ClampFloat(
        cfg_speedShakeEngineBrakeFrequencyMultiplier, 0.0f, 1.0f);

    cfg_speedShakeEngineBrakeSmoothing =
        SafePositive(cfg_speedShakeEngineBrakeSmoothing, 9.0f);
    cfg_speedShakeEngineBrakeSmoothing = ClampFloat(
        cfg_speedShakeEngineBrakeSmoothing, 0.1f, 30.0f);

    // Keep the speedbreaker target at or below the normal minimum FOV so it
    // cannot accidentally widen the camera. Current FOV may recover from it smoothly.
    cfg_speedbreakerFov = static_cast<uint16_t>(
        ClampFloat(static_cast<float>(cfg_speedbreakerFov),
            0.0f, static_cast<float>(cfg_max_fov)));

    if (cfg_max_fov < cfg_initial_fov)
        cfg_max_fov = cfg_initial_fov;

    const float availableHeadroom =
        static_cast<float>(cfg_max_fov - cfg_initial_fov);
    if (!std::isfinite(cfg_effect_headroom) || cfg_effect_headroom < 0.0f)
        cfg_effect_headroom = 0.0f;
    cfg_effect_headroom = ClampFloat(
        cfg_effect_headroom, 0.0f, availableHeadroom);

    // Separate lower headroom lets negative transient effects dip below initial_fov
    if (!std::isfinite(cfg_effect_headroom_below) ||
        cfg_effect_headroom_below < 0.0f)
        cfg_effect_headroom_below = 0.0f;
    cfg_effect_headroom_below = ClampFloat(
        cfg_effect_headroom_below, 0.0f, static_cast<float>(cfg_initial_fov));

    cfg_shift_duration = ClampFloat(cfg_shift_duration, 0.05f, 2.0f);

    userWantsEnabled = cfg_permanentEnable;
}
