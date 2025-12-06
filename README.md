# CarbonDynamicFOV
A simple-coded dynamic camera FOV mod for NFS: Carbon
## How it works
The code reads your car's speed ingame, calculates a new FOV value based on how fast you go, then assigns it to the game's FOV, all in real-time.

It also temporarily patches a game code that always checks and writes FOV, in order to avoid stuttering/flickering issues due
to conflict.
