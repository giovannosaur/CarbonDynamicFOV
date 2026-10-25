#pragma once

#include <windows.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Game addresses (NFSC v1.4)
// -----------------------------------------------------------------------------
static constexpr uintptr_t kHudShowMessageAddress = 0x0065B1B0;
static constexpr uintptr_t kGameStateAddress = 0x00A99BBC; // 6 = gameplay
static constexpr uintptr_t kNisStateAddress = 0x00B4D964;
static constexpr uintptr_t kSpeedAddress = 0x00A8E178;
static constexpr float kRawSpeedToKmh = 3.594f;
static constexpr uintptr_t kFovAddress = 0x00B1D604;

// Camera gate / speedbreaker state
static constexpr uintptr_t kGetIsDrivingCameraAddress = 0x004B6D50;
static constexpr uintptr_t kSpeedbreakerStateAddress = 0x00A9922C;

// SDK Lists.h: ListableSet<IPlayer, 0xA9FF5C> PlayerList;
// List layout: begin at +0x0, capacity at +0x4, size at +0x8.
static constexpr uintptr_t kPlayerListAddress = 0x00A9FF5C;

static constexpr uintptr_t kISimableInParentOffset = 0x24;
static constexpr uintptr_t kIVehicleInPVehicle = 0xB8;
static constexpr uintptr_t kEngineInPVehicle = 0xF8;
static constexpr uintptr_t kITransmissionInEngine = 0x44;
static constexpr uintptr_t kIEngineInEngineRacer = 0x4C;

static constexpr uintptr_t kEngineRacerVtable = 0x009E5A18;
static constexpr uintptr_t kIEngineVtable = 0x009E59A0;
static constexpr uintptr_t kITransmissionVtable = 0x009E59E0;

// Direct functions confirmed in IDA and also present in the vtables above.
static constexpr uintptr_t kIsNosEngagedAddress = 0x006F1050;
static constexpr uintptr_t kIsEngineBrakingAddress = 0x006F1110;
static constexpr uintptr_t kIsShiftingGearAddress = 0x006F1120;
static constexpr uintptr_t kIsGearChangingAddress = 0x006F11F0;

using HudShowMessageFn = void(__cdecl*)(char*);
static HudShowMessageFn Hud_ShowMessage =
reinterpret_cast<HudShowMessageFn>(kHudShowMessageAddress);

static volatile int* GameState =
reinterpret_cast<volatile int*>(kGameStateAddress);
static volatile uint8_t* NisState =
reinterpret_cast<volatile uint8_t*>(kNisStateAddress);
static volatile float* speedAddr =
reinterpret_cast<volatile float*>(kSpeedAddress);
static volatile uint16_t* fovAddr =
reinterpret_cast<volatile uint16_t*>(kFovAddress);
