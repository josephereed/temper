# Build prompt: Bastion movement MVP (1v1 greybox)

## Objective

First playable milestone for TEMPER (mech PvP, UE5). A single Bastion heavy mech
that the player can drive around a greybox arena, in first-person, third-person,
and observe camera modes. **Movement feel only** — no combat, no HUD, no sound,
no salvage, no menus.

## Deliverables

1. **Greybox arena** — a small, dense urban block inspired by CoD's Rust map
   (central open ground, surrounding two-story structures, scattered cover blocks).
   INTERNAL REFERENCE ONLY: greybox geometry for testing, never distributed, no
   copied art assets. Simple collision on all structures.
2. **Bastion mech pawn** — heavy biped, ~4.2m tall. Placeholder mesh is fine
   (capsule + boxes acceptable for this milestone); proportions should read
   "heavy mech," not human.
3. **Locomotion** — walk, sprint, and jump-jet burst (short thruster hop).
   - FEEL TARGET (binding): CoD-grade input responsiveness — zero perceptible
     input lag — wrapped around heavy physics: momentum, acceleration ramps,
     weighty deceleration, ground-shaking landings. Responsive controls, heavy
     machine. This is the entire point of the milestone.
   - Starting values (tune by feel, not by formula): top walk speed ~6-7 m/s,
     sprint ~9 m/s, acceleration ramp ~1.5s to full speed, thruster burst as a
     short repositioning hop, not flight.
4. **Camera modes** (toggle key, e.g. V): 
   - 1st person: cockpit viewpoint, slight head-bob coupled to footfalls, visible
     cockpit frame edges.
   - 3rd person: over-shoulder chase cam with lag/spring so the mech's weight
     reads in the camera.
   - Observe: free-fly spectate camera.
5. **Basic collision** — mech collides with structures and ground, cannot leave
   the arena bounds, cannot clip through geometry.

## Out of scope (do not build)

Weapons, firing, damage, shields, HUD elements, thermal overlay, radar, audio,
salvage/economy, round flow, menus, networking. Movement and cameras only.
A second mech/AI opponent is NOT required — empty arena.

## Acceptance gates (binding)

- [ ] Project builds clean in UE5 and runs at stable framerate on a mid-range PC.
- [ ] Player can switch between all three camera modes at runtime without errors.
- [ ] Mech walks, sprints, and thruster-bursts with collision; cannot escape the
      arena or clip through structures.
- [ ] Input latency is imperceptible: keypress to visible mech response with no
      discernible delay (measure or demonstrate; do not estimate).
- [ ] Movement visibly conveys mass: acceleration/deceleration ramps, landing
      impact, no ice-skating.
- [ ] Commit per-run logs of a scripted movement sequence (walk forward 10m,
      sprint 10m, thruster burst, 180° turn) run 3x with identical inputs;
      attach the logs. A single green run is not a pass.

## Feel verdict

The final gate is human: Joseph will drive it himself and judge "responsive but
weighty." Optimize for honest feel, not for numbers that look good on paper.
If a starting value fights the feel target, change the value and note it in
the done report.

## Done report

Write `agent-prompts/done/2026-10-06-bastion-movement-mvp.md` with: result
commit SHAs, the 3x movement logs, measured input latency, the final tuned
movement values, and anything that fought you.
