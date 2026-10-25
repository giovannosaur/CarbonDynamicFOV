#include "pch.h"
#include "PlayerEngine.h"
#include "GameAddresses.h"
#include "Utils.h"

#include <windows.h>
#include <stdint.h>

bool TryGetPlayerEngineContext(PlayerEngineContext& out)
{
    out = {};

    if (!IsReadableMemory(reinterpret_cast<const void*>(kPlayerListAddress),
        3 * sizeof(uint32_t)))
        return false;

    const uintptr_t listBegin =
        *reinterpret_cast<const uint32_t*>(kPlayerListAddress);
    const uint32_t listCapacity =
        *reinterpret_cast<const uint32_t*>(kPlayerListAddress + 4);
    const uint32_t listSize =
        *reinterpret_cast<const uint32_t*>(kPlayerListAddress + 8);

    if (!listBegin || listSize == 0 || listSize > 64 ||
        listCapacity == 0 || listSize > listCapacity)
        return false;

    if (!IsReadableMemory(reinterpret_cast<const void*>(listBegin),
        sizeof(uint32_t)))
        return false;

    const uintptr_t playerAddress =
        *reinterpret_cast<const uint32_t*>(listBegin);
    void* player = reinterpret_cast<void*>(playerAddress);
    if (!IsReadableMemory(player, sizeof(uint32_t)))
        return false;

    // IPlayer::GetSimable(): virtual slot [1], (__thiscall).
    void** playerVtable = *reinterpret_cast<void***>(player);
    if (!playerVtable ||
        !IsReadableMemory(playerVtable, 2 * sizeof(uint32_t)) ||
        !IsExecutableAddress(reinterpret_cast<uintptr_t>(playerVtable[1])))
        return false;

    using GetSimableFn = void* (__thiscall*)(void*);
    const auto getSimable = reinterpret_cast<GetSimableFn>(playerVtable[1]);
    void* simable = getSimable(player);
    if (!IsReadableMemory(simable, sizeof(uint32_t)))
        return false;

    const uintptr_t simableAddress = reinterpret_cast<uintptr_t>(simable);
    if (simableAddress < kISimableInParentOffset)
        return false;

    // SDK ISimable::GetParent() is pointer arithmetic: ISimable - 0x24.
    void* pVehicle = reinterpret_cast<void*>(
        simableAddress - kISimableInParentOffset);

    // PVehicle embeds IVehicle at +0xB8. Use its vtable to sanity-check the parent.
    void* vehicleInterface = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(pVehicle) + kIVehicleInPVehicle);
    if (!IsReadableMemory(vehicleInterface, sizeof(uint32_t)))
        return false;

    void** vehicleVtable = *reinterpret_cast<void***>(vehicleInterface);
    if (!vehicleVtable ||
        !IsReadableMemory(vehicleVtable, 25 * sizeof(uint32_t)) ||
        !IsExecutableAddress(reinterpret_cast<uintptr_t>(vehicleVtable[24])))
        return false;

    // PVehicle::mEngine at +0xF8
    const uintptr_t engineField =
        reinterpret_cast<uintptr_t>(pVehicle) + kEngineInPVehicle;
    if (!IsReadableMemory(reinterpret_cast<const void*>(engineField),
        sizeof(uint32_t)))
        return false;

    void* engineInterface = *reinterpret_cast<void**>(engineField);
    if (!HasExpectedVtable(engineInterface, kIEngineVtable, 13))
        return false;

    const uintptr_t engineInterfaceAddress =
        reinterpret_cast<uintptr_t>(engineInterface);
    if (engineInterfaceAddress < kIEngineInEngineRacer)
        return false;

    void* engineRacerBase = reinterpret_cast<void*>(
        engineInterfaceAddress - kIEngineInEngineRacer);
    if (!HasExpectedVtable(engineRacerBase, kEngineRacerVtable, 18))
        return false;

    void* transmissionInterface = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(engineRacerBase) +
        kITransmissionInEngine);
    if (!HasExpectedVtable(transmissionInterface,
        kITransmissionVtable, 5))
        return false;

    void** engineVtable = *reinterpret_cast<void***>(engineInterface);
    void** racerVtable = *reinterpret_cast<void***>(engineRacerBase);
    void** transmissionVtable =
        *reinterpret_cast<void***>(transmissionInterface);

    if (reinterpret_cast<uintptr_t>(engineVtable[8]) != kIsNosEngagedAddress ||
        reinterpret_cast<uintptr_t>(racerVtable[10]) != kIsEngineBrakingAddress ||
        reinterpret_cast<uintptr_t>(racerVtable[11]) != kIsShiftingGearAddress ||
        reinterpret_cast<uintptr_t>(transmissionVtable[4]) != kIsGearChangingAddress)
        return false;

    out.player = player;
    out.simable = simable;
    out.pVehicle = pVehicle;
    out.vehicleInterface = vehicleInterface;
    out.engineInterface = engineInterface;
    out.engineRacerBase = engineRacerBase;
    out.transmissionInterface = transmissionInterface;
    out.valid = true;
    return true;
}

EngineSignals ReadEngineSignals(const PlayerEngineContext& context)
{
    EngineSignals signals = {};
    if (!context.valid)
        return signals;

    // All vtables/function slots were validated by TryGetPlayerEngineContext()
    using BoolGetterFn = bool(__thiscall*)(void*);

    void** engineVtable =
        *reinterpret_cast<void***>(context.engineInterface);
    void** racerVtable =
        *reinterpret_cast<void***>(context.engineRacerBase);
    void** transmissionVtable =
        *reinterpret_cast<void***>(context.transmissionInterface);

    const auto isNosEngaged =
        reinterpret_cast<BoolGetterFn>(engineVtable[8]);
    const auto isEngineBraking =
        reinterpret_cast<BoolGetterFn>(racerVtable[10]);
    const auto isShifting =
        reinterpret_cast<BoolGetterFn>(racerVtable[11]);
    const auto isGearChanging =
        reinterpret_cast<BoolGetterFn>(transmissionVtable[4]);

    signals.nosEngaged = isNosEngaged(context.engineInterface);
    signals.engineBraking = isEngineBraking(context.engineRacerBase);
    // Both getters refer to the shift timer through their respective interfaces.
    signals.shifting =
        isShifting(context.engineRacerBase) ||
        isGearChanging(context.transmissionInterface);
    signals.valid = true;
    return signals;
}

// camera-shake query independent of the Dynamic FOV toggle
bool QueryEngineBrakeStateForShake(bool& engineBraking)
{
    engineBraking = false;

    PlayerEngineContext context = {};
    if (!TryGetPlayerEngineContext(context))
        return false;

    void** racerVtable =
        *reinterpret_cast<void***>(context.engineRacerBase);
    if (!racerVtable ||
        !IsReadableMemory(racerVtable, 11 * sizeof(uint32_t)) ||
        reinterpret_cast<uintptr_t>(racerVtable[10]) !=
        kIsEngineBrakingAddress ||
        !IsExecutableAddress(
            reinterpret_cast<uintptr_t>(racerVtable[10])))
        return false;

    using IsEngineBrakingFn = bool(__thiscall*)(void*);
    const auto isEngineBraking =
        reinterpret_cast<IsEngineBrakingFn>(racerVtable[10]);

    engineBraking = isEngineBraking(context.engineRacerBase);
    return true;
}
