#include "pch.h"
#include "Utils.h"
#include "GameAddresses.h"

#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <cmath>

LARGE_INTEGER g_qpcFrequency = {};

// -----------------------------------------------------------------------------
// Utilities
// -----------------------------------------------------------------------------
float ClampFloat(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

float SafePositive(float value, float fallback)
{
    return (std::isfinite(value) && value > 0.0f) ? value : fallback;
}

bool IsReadableMemory(const void* address, size_t bytes)
{
    if (!address || bytes == 0)
        return false;

    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(address, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) ||
        (mbi.Protect & PAGE_NOACCESS))
        return false;

    const uintptr_t begin = reinterpret_cast<uintptr_t>(address);
    const uintptr_t regionBegin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    const uintptr_t regionEnd = regionBegin + mbi.RegionSize;
    if (begin < regionBegin || begin > regionEnd)
        return false;
    return bytes <= regionEnd - begin;
}

bool IsExecutableAddress(uintptr_t address)
{
    if (!address)
        return false;

    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi))
        != sizeof(mbi))
        return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD))
        return false;

    const DWORD protection = mbi.Protect & 0xFF;
    return protection == PAGE_EXECUTE ||
        protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
}

// GetIsDrivingCamera
bool QueryIsDrivingCamera(int playerNum, bool& isDriving)
{
    isDriving = false;

    if (!IsExecutableAddress(kGetIsDrivingCameraAddress))
        return false;

    using GetIsDrivingCameraFn = bool(__thiscall*)(void*, int*, const int);
    const auto getIsDrivingCamera =
        reinterpret_cast<GetIsDrivingCameraFn>(kGetIsDrivingCameraAddress);

    int output = 0;
    const bool querySucceeded =
        getIsDrivingCamera(nullptr, &output, playerNum);

    isDriving = querySucceeded && (output != 0);
    return querySucceeded;
}

bool IsSpeedbreakerActive()
{
    if (!IsReadableMemory(
        reinterpret_cast<const void*>(kSpeedbreakerStateAddress),
        sizeof(uint8_t)))
        return false;

    return *reinterpret_cast<volatile const uint8_t*>(
        kSpeedbreakerStateAddress) != 0;
}

float GetPlayerSpeedKmh()
{
    const float rawSpeed = *speedAddr;

    if (!std::isfinite(rawSpeed))
        return 0.0f;

    if (rawSpeed < 0.0f)
        return 0.0f;

    return rawSpeed * kRawSpeedToKmh;
}

bool HasExpectedVtable(void* object, uintptr_t expectedVtable,
    size_t slotsToValidate)
{
    if (!IsReadableMemory(object, sizeof(uint32_t)))
        return false;

    const uintptr_t actualVtable =
        *reinterpret_cast<const uint32_t*>(object);
    if (actualVtable != expectedVtable)
        return false;

    return IsReadableMemory(reinterpret_cast<const void*>(actualVtable),
        slotsToValidate * sizeof(uint32_t));
}

float SmoothTowards(float current, float target,
    float responseRate, float dt)
{
    responseRate = SafePositive(responseRate, 1.0f);
    dt = ClampFloat(dt, 0.0f, 0.05f);
    const float alpha = 1.0f - expf(-responseRate * dt);
    return current + (target - current) * alpha;
}
