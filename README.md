# TEMPER

Mech PvP, Unreal Engine 5.8, C++. Current state: **Bastion movement MVP** — one heavy mech in a
greybox arena, three camera modes, movement only (`agent-prompts/2026-10-06-bastion-movement-mvp.md`).

## Build

Requires UE 5.8 and Visual Studio 2022 (or Build Tools) with the C++ game workload.

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" TemperEditor Win64 Development -Project="%CD%\Temper.uproject" -WaitMutex
```

If another UE 5.8 editor is open, UBT refuses with "Unable to build while Live Coding is active";
add `-NoHotReloadFromIDE` (safe: it only skips that engine-wide check).

## Play

`Play.bat` (windowed 1920x1080), or open `Temper.uproject` and press Play. The project runs on the
engine's empty `Entry` map; the game mode builds the arena at runtime, so there is no binary map.

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Move (camera-relative) | WASD | Left stick |
| Aim / look | Mouse | Right stick |
| Sprint (hold, mostly-forward only) | Left Shift | L3 |
| Thruster hop | Space | A |
| Camera: 1st person -> 3rd person -> observe | V | Back/View |
| Observe: fly up / down / fast | Space or E / Ctrl or Q / Shift | |

Aim is 1:1 with the mouse (the torso and cockpit follow it instantly); the legs turn to follow at a
limited rate. Mouse sensitivity is `MouseSensitivity` on `TemperPlayerController` (0.6 = 0.042°/count).

## Scripted acceptance runs

```
UnrealEditor.exe Temper.uproject -game -TemperScenario=all -ResX=1920 -ResY=1080 -windowed -log
```

Scenarios (`+`-separated): `latency`, `sequence` (walk 10 m, sprint 10 m, thruster hop, 180° turn, x3),
`collision`, `cameras`, `perf`. Each logs `SCENARIO <name> PASS/FAIL: <measurements>` and writes
per-run CSVs/screenshots to `Saved/TemperRuns/`, then quits. Input is injected through
`PlayerController::InputKey` at the start of the frame (plus one probe through the real Windows
message queue); real keyboard/mouse input is ignored while a scenario runs.

## Layout

- `Source/Temper/BastionMovementComponent` — the feel: speed-dependent acceleration curve, finite
  braking, lateral grip (no sliding), landing speed penalty, thruster lift. All tunables are
  `UPROPERTY`s at the top of the header.
- `BastionMech` — the pawn: procedural rig (planted feet + two-bone IK legs, pelvis/torso springs),
  hull turn rate, thruster hop, landing response, camera shake, first/third-person cameras.
- `TemperPlayerController` — Enhanced Input (built in code), camera mode cycling, observe free-fly.
- `TemperArena` — the greybox block (internal reference only), collision, lighting.
- `TemperScenarioRunner` — the scripted gates above.
