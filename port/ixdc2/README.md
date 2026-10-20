# DC2 Ridepod walk inside Chronicle, R1

Configure the DC1 port with `IXION_DC2_SOURCE_DIR` pointing at the reviewed
ChronicleTwo checkout. `helpers.json` records the donor commit, Git blob IDs,
line ranges and exact snippet hashes. `helpers.inc` copies `unitRotation`
and `CActionChara::RoboWalkMoveIF` without changing their bodies. The donor
path is a provenance dependency, not an engine translation unit or include
root. The archive is compiled without DC1/DC2 headers or DC1's `dc_options`.

`ridepod.cpp` supplies a deliberately small surrogate in `namespace ixdc2`:
frame yaw, velocity, camera/pad input and motion output. A rename macro makes
the donor class `ixdc2_RidepodChara`. No `CCPoly`, `MoveCheckInfo` or DC1
`unitRotation` declaration enters this unit. `mgDistVector` is the scalar
sqrt equivalent of DC2's port implementation. `SearchChara` and scene lookup
return null; `RockOn` is a no-op. The public step rejects lock-on and accepts
only mode 0/1, frame_rate 2, finite canonical yaw and bounded stick input.
The boundary in `port/include/ixion_dc1_ridepod.h` uses caller-owned plain C
records and original Shift-JIS motion bytes. It is synchronous and
non-reentrant; the surrogate pad/camera slots are private scratch state for
the single game logic thread.

Set `DC_IXION_RIDEPOD=1` before launching a build configured with the donor
path. In Divine Beast Cave, visible Toan uses the existing DC1 eye-task
collision/event path with the third-person follow camera. The donor supplies
horizontal velocity, yaw and stand/walk/run selection. DC1 pad calibration,
script/action/death/hold/pad locks, monster collision and `MoveCheck` remain
DC1-owned. The vertical update after collision uses Ridepod's -3 decrement
and -5 clamp. The bridge maps donor motion enums to Toan's native keys
(stand 0, walk 2, run 1); it does not pretend DC2 motion names exist in DC1.
Recoil and invincibility timers advance in the borrowed task. Pause/menu and
damage exits keep DC1 ownership. R1 exposes walking only; it is not a Ridepod
combat, energy, lock-on, arm or vehicle-model implementation.

Each active step logs the raw requested velocity, velocity after monster
collision, solved displacement, position, yaw, motion and grounded state.
The current four-unit velocity is deliberately unscaled: `max_speed` is
the donor's animation divisor, not a movement cap. No dt is introduced.
DC1's default 60 Hz loop differs from the donor's expected 30 Hz cadence;
wall-clock movement tuning and cross-game world scale remain R2 work.

Focused targets: `ixdc2_ridepod_tests` (seven closed-form cases), and
`ixdc2_dc1_collision_tests --gtest_filter=FirstPersonWalk.Ridepod*` (actual
DC1 collision, adapter locks and timer upkeep). No full game suite is needed
for this round. On Windows, the archive gate permits only private strong
symbols and runtime math/string references. The post-link gate compares all
shared and entry objects, requires the donor bodies and C boundary to remain
linked, and requires no executable exports / only `ixion_get_api` from the
module. `symbol_gate_test.py` compiles real negative COFF objects and a DLL
for leaks, foreign MoveCheck/mgFrameRate references, a prefixed collision and
an accidental export. The linked-binary gate is Windows-specific.

R2: add a procedural rigid leg/body visual first; verify a seat frame and
Toan transform copy, then convert untextured leg01/body01 geometry. Validate
face indices/materials before adding the texture bridge. Leave the skinned
arm, donor animation integration and lock-on for later rounds.
