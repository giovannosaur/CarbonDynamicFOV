# CarbonDynamicFOV

Dynamic camera FOV mod for **Need for Speed: Carbon**

## How it works
The mod reads your car's speed and vehicle state (NOS, gear shifts, engine braking) in real-time and calculates a new camera FOV from them. While it's active, it temporarily disables the game's own FOV writer to prevent stuttering or flickering, and restores it when you leave the driving camera.
Noise (shaky effect) is added to the camera right after the game finishes its own camera calculations, so it doesn't interfere with the game's built-in camera effects.

## Features

### Dynamic FOV
Aside from player speed, the game's FOV now reacts to:
- how hard your car accelerates;
- gear shifts;
- NOS usage;
- engine braking/deceleration;
- speedbreaker

### Dynamic Camera Noise/Shake
- The camera shakes subtly at speed, getting stronger and faster the quicker you go. When decelerating, it goes subtle

## Installation
1. Install an ASI loader for NFS: Carbon (e.g. Ultimate ASI Loader), if you don't have one yet
2. Copy `CarbonDynamicFOV.asi` and `DFconfig.ini` into your game's `scripts` folder
3. (Recommended) Install the new camera angle nfsms script using OGVI

## Controls
- **Backspace:** toggle Dynamic FOV on/off

You can change the hotkey in `DFconfig.ini` under `[hotkeys]` (it uses Windows virtual-key codes)

## Configuration
Everything is adjustable in `DFconfig.ini`, these are some examples:

| Setting | What it does |
|---|---|
| `[settings] initial_fov` / `max_fov` | Camera FOV when standing still / at top speed |
| `[settings] permanentEnable` | `1` = mod is on automatically when the game starts |
| `[speed] max_speed` | Speed (km/h) at which the FOV reaches its maximum |
| `[smoothing] fov_sensitivity` | How quickly the camera reacts overall |
| `[effects]` | Turn NOS, gear-shift and engine-braking effects on/off and set their strength |
| `[speedbreaker] fov` | How far the camera zooms in during Speedbreaker |
| `[speed_shake] enabled` | Turn camera shake on/off |
| `[speed_shake] yaw_degrees` / `pitch_degrees` | Shake strength |

Each effect can be turned off individually by setting its `enabled` option to `0`

## Changelog

**Version 2.0**
- Complete rewrite with a modular codebase
- New FOV effects: acceleration kick, NOS, gearshift pulse and engine braking
- Speedbreaker zoom with smooth enter/exit transitions
- New speed-based camera shake, softened while engine braking
- FOV only changes in the driving camera; the game's original FOV is restored in situations when player is not in control
- Speed settings are now in km/h
- Default toggle hotkey changed to Backspace

**Version 1.0.9**
- Game state check: no longer interferes with NIS cutscenes and frontend menus
- Permanent/auto-enable option
- HUD message "Dynamic FOV Enabled/Disabled" when toggling
- More responsive hotkey reading
- Adjusted default FOV and max speed values
- Default hotkey changed to F1

**Version 1.0.5**
- Hotkey input reading fix, now more responsive

**Version 1.0**
- Initial release


## Credits and References
Zolika1351 - NFSC-SDK repo (references)
ARCHIE - NFSC Custom HUD (references)
ThirteenAG - Ultimate ASI Loader