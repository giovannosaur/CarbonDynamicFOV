#pragma once

#include <windows.h>
#include <stdint.h>
#include <stddef.h>

extern LARGE_INTEGER g_qpcFrequency;

float ClampFloat(float value, float minimum, float maximum);
float SafePositive(float value, float fallback);
bool IsReadableMemory(const void* address, size_t bytes);
bool IsExecutableAddress(uintptr_t address);
bool QueryIsDrivingCamera(int playerNum, bool& isDriving);
bool IsSpeedbreakerActive();
float GetPlayerSpeedKmh();
bool HasExpectedVtable(void* object, uintptr_t expectedVtable,
    size_t slotsToValidate);
float SmoothTowards(float current, float target,
    float responseRate, float dt);
