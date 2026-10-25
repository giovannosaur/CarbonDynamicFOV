#pragma once

#include <stdint.h>

extern int      cfg_toggleKey;
extern uint16_t cfg_initial_fov;
extern uint16_t cfg_max_fov;
extern float    cfg_max_speed;
extern int      cfg_graph_type;
extern bool     cfg_showHudMsg;
extern bool     cfg_permanentEnable;

extern float    cfg_fov_sensitivity;
extern float    cfg_expand_sensitivity;
extern float    cfg_recover_sensitivity;
extern float    cfg_effect_headroom;
extern float    cfg_effect_headroom_below;

extern bool     cfg_accel_enabled;
extern float    cfg_accel_reference;
extern float    cfg_accel_gain;
extern float    cfg_accel_sensitivity;

extern bool     cfg_nos_enabled;
extern float    cfg_nos_gain;
extern bool     cfg_shift_enabled;
extern float    cfg_shift_gain;
extern float    cfg_shift_duration;
extern float    cfg_shift_min_speed;
extern bool     cfg_engine_braking_enabled;
extern float    cfg_engine_braking_gain;
extern bool     cfg_debugEngineLookup;

extern bool     cfg_drivingCameraOnly;
extern bool     cfg_speedbreakerEnabled;
extern uint16_t cfg_speedbreakerFov;
extern float    cfg_speedbreakerEnterSensitivity;
extern float    cfg_speedbreakerExitSensitivity;

extern bool     cfg_speedShakeEnabled;
extern float    cfg_speedShakeMinSpeed;
extern float    cfg_speedShakeMaxSpeed;

extern float    cfg_speedShakeYawDegrees;
extern float    cfg_speedShakePitchDegrees;

extern float    cfg_speedShakeMinFrequency;
extern float    cfg_speedShakeMaxFrequency;
extern float    cfg_speedShakeSmoothing;

extern bool     cfg_speedShakeEngineBrakeModifierEnabled;
extern float    cfg_speedShakeEngineBrakeStrengthMultiplier;
extern float    cfg_speedShakeEngineBrakeFrequencyMultiplier;
extern float    cfg_speedShakeEngineBrakeSmoothing;

extern bool     cfg_speedShakeDebug;

void LoadConfig();
