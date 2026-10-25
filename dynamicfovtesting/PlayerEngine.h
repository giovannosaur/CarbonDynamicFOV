#pragma once

// -----------------------------------------------------------------------------
// Player -> ISimable -> PVehicle -> IEngine lookup
// -----------------------------------------------------------------------------
// Based on the provided NFSC-SDK layouts:
//   PlayerList.Get(0) -> IPlayer*
//   IPlayer::GetSimable() is virtual slot 1
//   ISimable::GetParent() returns simable - 0x24
//   PVehicle::mEngine is at +0xF8 and stores IEngine*
// The engine/interface pointers are accepted only if all expected vtables match.
// If any check fails (menu/loading/other vehicle implementation), the getter
// effects are safely skipped while the speed-only FOV remains available.
struct PlayerEngineContext
{
    void* player = nullptr;
    void* simable = nullptr;
    void* pVehicle = nullptr;
    void* vehicleInterface = nullptr;
    void* engineInterface = nullptr;       // IEngine subobject, PVehicle::mEngine
    void* engineRacerBase = nullptr;       // engineInterface - 0x4C
    void* transmissionInterface = nullptr; // engineRacerBase + 0x44
    bool valid = false;
};

struct EngineSignals
{
    bool valid = false;
    bool nosEngaged = false;
    bool engineBraking = false;
    bool shifting = false;
};

bool TryGetPlayerEngineContext(PlayerEngineContext& out);
EngineSignals ReadEngineSignals(const PlayerEngineContext& context);
bool QueryEngineBrakeStateForShake(bool& engineBraking);
