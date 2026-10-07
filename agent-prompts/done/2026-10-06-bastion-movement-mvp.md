# Done: Bastion movement MVP (1v1 greybox)

**Result:** code `0eb8a03` on `main` (UE 5.8 C++ project at repo root). Logs, the report and
screenshots are in the commit that adds this file. Logs are in `done/2026-10-06-bastion-movement-mvp/`.
**Gates:** 16 of 18 automated checks pass. The two perf checks fail on this machine while it was
running other projects' jobs; see Gate 1. The human feel verdict is still open (Joseph).

How to play: build per `README.md`, then run `Play.bat`. Controls: WASD, mouse, Shift sprint,
Space thruster hop, V to cycle 1st → 3rd → observe.
How to re-run the gates:
`UnrealEditor.exe Temper.uproject -game -TemperScenario=all -ResX=1920 -ResY=1080 -windowed -log`
(results go to `Saved/TemperRuns/`).

## What was built

- **Arena** (`TemperArena`): a 96 m square block built at runtime from engine cubes with box
  collision. It has an open central yard, nine two-story buildings around it (5.5 m stories, so
  they read as two stories at mech scale), single and stacked 12 m containers, low walls,
  barriers, a 3 m loading dock with a 1.5 m step, and a tower. The ground has a painted 4 m grid
  so speed and stopping distance are readable. The perimeter is 14 m walls plus invisible blockers
  to 60 m and a lid. The layout is only loosely Rust-inspired, is internal greybox, and uses no
  copied assets.
- **Bastion** (`BastionMech`): about 4.3 m tall, a squat wide torso on two long legs, built from
  boxes. The rig is procedural:
  - Feet stay planted on the ground until they step, and the legs are two-bone IK chains.
  - The pelvis and torso are on springs driven by footfalls, landings, thruster ignition and input.
  - The torso and cockpit follow the mouse 1:1. The hull (legs) turns after them at a limited rate.
- **Movement** (`BastionMovementComponent`): CMC is kept for collision, floors and steps.
  Walking velocity is replaced with:
  - a speed-dependent acceleration curve;
  - finite braking;
  - lateral grip, so turns bleed speed instead of sliding;
  - a landing speed penalty;
  - thruster lift.
- **Cameras** (V or gamepad Back):
  - 1st person: cockpit view with frame struts. Head-bob comes from the pelvis spring that
    footfalls kick, so it is coupled to actual steps. Camera shake on steps and landings.
  - 3rd person: over-shoulder spring arm with translation lag, so the camera trails the mass.
    Aim has no rotation lag.
  - Observe: free-fly spectator camera.
- **Scenario runner**: scripted gates with per-frame CSV logs (see below).

## Acceptance gates

| # | Gate | Result |
|---|---|---|
| 1 | Builds clean; stable framerate on a mid-range PC | **Build: PASS** (0 warnings). **Framerate: NOT VERIFIED.** On this box the GPU cost is 1.65 ms p50 / 4.0 ms p99 and the game thread is 3.8 ms p50. But the wall-clock p99 was 85 ms, because 4 other UE 5.8 instances (thorax-pressure scenario runs, EstateSale), 2 Godot processes and a batch job were sharing this 16 GB machine (2.8 GB free) the whole session. I did not stop other projects' jobs. The machine is high-end (RTX 5070 Ti, Ryzen 9 5900XT), so I haven't shown "mid-range" either. Needs a re-run on a quiet machine: `-TemperScenario=perf`. |
| 2 | Switch all three camera modes at runtime without errors | **PASS**: 9 V-presses cycle `3P OBS 1P` ×3. The view target is correct every time and there are 0 log errors. Observe flies 23 m in 1.5 s while the mech stays still. |
| 3 | Walk/sprint/burst with collision; can't escape or clip | **PASS**, with a capsule penetration test every frame. Sprinting and jet-spamming into the west wall and then the corner for 11 s: 0 penetrating frames, 0 frames outside the arena. Sprint and hop into a building for 6 s: 0 penetrating, stops 165 cm (one capsule radius) from the face. Hopping step → dock: lands on top (z 302). |
| 4 | Input latency imperceptible, measured | **PASS (in-engine)**: 0 extra frames from input to visible response on every probe. Details below. |
| 5 | Mass: accel/decel ramps, landing impact, no ice-skating | **PASS (numbers; feel is Joseph's call)**: 0→full walk in 1.39–1.45 s, walk stop 1.7–1.8 m, sprint stop 2.5–2.6 m. A 3 m drop lands at 645 cm/s (hard), which slams the pelvis, shakes the camera and cuts speed. Feet are planted while on the ground (by construction in the rig, not an automated check). |
| 6 | Scripted sequence ×3 with identical inputs, logs attached | **PASS**: `movement_run1..3.csv`. See the spread table below. |

## Measured input latency

Method: input events are injected into `PlayerController::InputKey` at the start of the frame, the
way OS input arrives. Each probe runs 5 times. The runner then records the first frame in which:

- the simulation responds;
- the torso is visibly displaced (≥ 1 cm or ≥ 0.25°, about 5 px at 1080p);
- the rendered camera view changes.

Timing is from the input to the render thread finishing that frame. The `os_W` probe sends a real
Windows `WM_KEYDOWN` through the window's message queue, then Slate, then the viewport. Full data
is in `latency.csv`.

| Probe | Extra frames to visible response (worst of 5) | Input → render thread done with that frame | Capsule moved ≥ 1 cm |
|---|---|---|---|
| W (move) | 0 | 9.5–21 ms (one 72 ms hitch) | 37–74 ms |
| Space (thruster) | 0 | 11–12 ms (two hitches: 49, 177 ms) | same frame |
| Mouse look | 0 | 9.5–11 ms (one 74 ms hitch) | n/a |
| V (camera) | 0 | 9.7–13.7 ms | n/a |
| W via Windows message queue | 0 after the controller receives it; the message is picked up 1 frame later because it was posted mid-frame | 18–28 ms | 51–58 ms |

What this means, plainly:

- **The machine answers on the frame the key is read.** The torso lurches, the hips drop, the
  first foot lifts, and in 1st person the camera moves with the torso.
- **The mass takes ~40–55 ms to move its first centimetre and ~1.4 s to reach walking speed.**
  That is deliberate: it is the "responsive controls, heavy machine" split.
- Mouse aim is 1:1 on the same frame (r.OneFrameThreadLag=0, no mouse smoothing, no camera
  rotation lag).
- **Not measured:**
  - GPU completion and display scanout. Add roughly 1 frame plus monitor latency.
  - True hardware keypress. I couldn't use SendInput because I won't steal focus from someone
    using the desktop, so the OS probe posts to the window queue instead.
  - A camera/LDAT test of the full chain is still worth doing.

## The 3× scripted sequence

Each run: reset at the west street, then walk 10 m from rest, release; sprint 10 m from rest,
release; thruster hop with W held; then a 180° mouse flick. All with identical injected inputs.
Real-time variable frame rate, so the runs are close but not bit-identical.

| Metric | Run 1 | Run 2 | Run 3 | Spread |
|---|---|---|---|---|
| Walk 10 m (s) | 2.082 | 2.079 | 2.066 | 0.015 |
| 95% walk speed (s) | 1.287 | 1.288 | 1.294 | 0.007 |
| Walk stop distance (cm) | 175.8 | 172.4 | 176.2 | 3.8 |
| Sprint 10 m from rest (s) | 2.016 | 2.009 | 2.007 | 0.008 |
| Sprint stop distance (cm) | 252.6 | 258.0 | 259.7 | 7.1 |
| Hop peak height (cm) | 213.6 | 219.3 | 229.2 | 15.6 |
| Hop air time (s) | 1.091 | 1.032 | 0.997 | 0.094 |
| Hop distance from standstill (cm) | 158.9 | 162.3 | 177.8 | 18.8 |
| 180° turn: view done / hull done (s) | 0.066 / 1.760 | 0.082 / 1.725 | 0.087 / 1.826 | 0.10 (hull) |

Honest caveat on the "consistent" check: its tolerance is tied to the worst frame in the runs (two
frames of time, one frame of travel). Hitches from the other processes reached 292 ms in this run,
which made the tolerance loose. The raw spreads above are the real evidence. The hop and turn
spreads are the frame-quantised ones: the hop's apex and touchdown land on whichever frame
happens, and the hull-finish time has a 1° threshold.

## Final tuned values

Movement (`BastionMovementComponent.h`):

| Value | Setting |
|---|---|
| Walk top speed | 6.5 m/s |
| Sprint top speed | 9.0 m/s |
| Acceleration | 9.0 m/s² from a standstill, tapering linearly to 1.8 m/s² at walk speed (full walk speed at ~1.4 s); 2.4 m/s² from walk to sprint |
| Braking | 11.5 m/s² |
| Lateral grip | 17 m/s² |
| Sprint condition | input within ~53° of the hull's facing |
| Gravity | ×1.6 |
| Air control | 0.12 |
| Max step | 0.6 m |
| Landing | hard from 3.5 m/s, maximal at 14 m/s; top speed drops to 35% and recovers over 0.55 s |
| Thruster lift during burn | 9 m/s² |

Mech (`BastionMech.h`):

| Value | Setting |
|---|---|
| Hull turn | 170°/s max, 700°/s² spin-up |
| Thruster hop | 6.2 m/s up; +4.5 m/s along input; horizontal capped at 11 m/s; 0.3 s burn |
| Thruster timing | 1.6 s cooldown; a press up to 0.15 s before touchdown is buffered |
| Hop result | ~2.2 m peak, ~1.0 s airtime (measured); from a full sprint ~10 m (estimated, not measured) |

Camera and input:

| Value | Setting |
|---|---|
| Chase arm | 11 m, over-shoulder offset (0, 2.3 m, 1.1 m), lag speed 6, max lag 2.6 m |
| Cockpit FOV | 95° |
| Pitch limits | −55° / +50° |
| Mouse | 0.042°/count |

Changed from the prompt's starting values, and why:

- **Acceleration is a curve, not a flat ramp.** A linear 1.5 s ramp feels like lag on the first
  step. Strong accel from a standstill that tapers toward top speed gives an immediate first step
  and still lands near the 1.5 s full-speed target.
- **Thruster up-speed went from 5.6 to 6.2 m/s.** At 5.6 the hop peaked around 1.6 m and couldn't
  clear the dock's 1.5 m step reliably.
- **Mouse sensitivity.** The engine's legacy 2.5× yaw/pitch multiplier (`bEnableLegacyInputScales`)
  is turned off so `MouseSensitivity` is the whole story. The legacy multiplier had silently made
  the mouse ~4× slower than intended.

## What fought me

1. **Making the first frame visible without faking the mass.** At 9 m/s² the hull moves under 1 mm
   in the first frame, so physics alone can't show a keypress. The fix is an anticipation layer:
   the input kick displaces the torso and pelvis springs on the same frame, while the capsule
   accelerates honestly. One bug here: the thruster's squat kick (−4 cm) cancelled the hull's
   first-frame rise (+5 cm), so the cockpit didn't move on frame 0. Ignition now pitches the torso
   instead.
2. **Simulated mouse axis values stick.** A simulated MouseX value persists until it is replaced,
   while a real mouse reports zero when still. Early runs had the view spinning forever. The
   runner now zeroes it.
3. **Real desk input leaked into the tests.** Once the game window had focus, keyboard and mouse
   from the person using the desktop steered the mech mid-run. Scenario mode now ignores
   non-simulated input, and the runner never steals focus.
4. **The machine was shared with other projects' jobs all session.** That caused frame hitches up
   to 292 ms (perf gate unverified, see Gate 1). Another project's open editor also held UE's
   engine-wide Live Coding lock; `-NoHotReloadFromIDE` gets past it (README notes this).
5. **Stride vs. leg reach at sprint.** The legs are 3 m and the hips 2.35 m up, so a sprint stride
   needs a quick cadence (0.24 s per step). The feet stay planted, but the cadence may read light
   for a heavy mech. This is the first thing I'd look at if sprint feels "trotty".

## For Joseph's drive

These are the knobs most likely to need your hands, all `UPROPERTY`s:

- `AccelFromStop` / `AccelAtWalk`: punch vs. heft of the first step.
- `BrakeDecel`: how far it coasts.
- `HullTurnRate`: how fast the legs follow your aim.
- `JetUpSpeed` / `JetForwardBoost`: the hop.
- `ChaseArm->CameraLagSpeed`: how much the camera trails.
- `MouseSensitivity`.
