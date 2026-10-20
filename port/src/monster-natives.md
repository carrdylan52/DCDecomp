# Vanguard DC2 natives in the DC1 host

Donor authority: ChronicleTwo `87cf4efa4ed17c4f075a03a4efdbca0303cac670`.
All `ro` citations below mean its `ps2/src/runscript_opcodes.cpp`.
The actual SB2 remains private and unchanged. The review's corrected traversal
follows unconditional JMP targets and establishes **43** native numbers. The
earlier 39-number inventory missed 78, 81, 184 and 185 beyond those jumps.

`ixion_dc2_monster.cpp` owns the private 256-entry table. Every entry is a
number-specific trampoline into this dispatcher, including unsupported ids.
There is no pointer to the DC1 native table. Only one opted-in donor actor is
installed per Divine Beast Cave floor 1 lifecycle. It replaces an existing
DC1 encounter in the active 140-unit neighborhood; it is not teleported.
The remaining monsters, scene, movement collision and player remain DC1's.

| Id | Native | DC2 authority | Round-1 adapter |
|---:|---|---|---|
|35|GET_RND|ro:1594|Donor float-range times retail 31-bit rand; typed integer output|
|40|GET_MONSTER_INDEX|ro:249|Private host slot identity|
|42|GET_USERID|ro:295|DC2 scene identity convention 24 + host slot; effects using it are stubs|
|43|GET_USER_MONS_ID|ro:3635|−1: Toan has no DC2 monster transformation|
|47|RUN_EVENT_SCRIPT|ro:4201|**Logged Unimplemented**; no DC1 event 2510 fallthrough|
|53|GET_BIT_FLAG|ro:4284|Private tutorial bit 51; other bits read 0|
|54|SET_BIT_FLAG|ro:4301|Private tutorial bit 51, no campaign flags|
|55|GET_ATT_TYPE|ro:4317|0 for native Toan's melee type; no donor transformation|
|58|GET_STS_ATTR|ro:4373|DC1 stop timer → bit 8; stone bit 32 not supported|
|75|GET_POS|ro:2368|Self xyz divided by reciprocal scale; reached 3-output form|
|77|GET_ROT|ro:2404|Self radians; reached 3-output form|
|78|SET_ROT|ro:2435|Set xyz radians immediately and clear pending turn speed|
|79|SET_NEXT_ROT|ro:2453|Next yaw and radian step; donor ticks, wrapped difference|
|80|SET_NEXT_POS|ro:2468|Donor target/speed/arrival multiplied by reciprocal scale; actual DC1 geometry still applies|
|81|CHK_MOVE_END|ro:2503|Typed integer: strict xyz distance < arrival, both in host units after reciprocal scaling; stopping alone does not imply arrival|
|82|RESET_MOVE|ro:2527|Clears donor speed and host output|
|83|GET_TARGET_POS|ro:2537|Toan xyz and optional distance in donor units|
|84|GET_TARGET_DIST|ro:2569|Euclidean xyz distance in donor units|
|85|GET_TARGET_ANGLE|ro:2594|atan2(target.x−self.x, target.z−self.z)|
|113|SET_DMG2|ro:1900|Cfg group-3 index → named frame; radius 2×arg; absolute motion-ratio window; DC1 effect2 record|
|116|LOAD_EFFECT_SCRIPT|ro:4017|**Logged Unimplemented**|
|117|SET_SCOOP|ro:2087|**Logged Unimplemented**; close-range grab/shockwave not transplanted|
|129|SET_INT_FLAG|ro:3169; actionchara.cpp:176|Private bit mask set/clear; no donor event/interrupt manager|
|130|SET_ACT_STATUS|ro:3155|Private action-status value|
|131|SET_MUTEKI|ro:3607|Donor ticks converted to host invincibility-timer duration|
|134|GET_GEKIRIN|ro:3621|Bounded donor rage counter; full DC2 rage reactions deferred|
|135|GET_PRIORITY|ro:3659|0 for the sole donor; no cross-game priority manager|
|137|SET_PIYORI_MARK|ro:4388|**Logged Unimplemented**|
|138|CHECK_PIYORI|ro:4399|0: DC1 has no donor stun-mark lifecycle|
|150|BLOW_START|ro:3138|Donor blow speed/rate/deceleration/duration adapted to native knockback direction|
|151|SET_DEAD_START|ro:3184|Keeps collapse rendered, disables movement/lock-on; DC1 alone owns kill/rewards|
|152|SET_DEAD_OFF|ro:3344|Retires at HP 0 once; does not pay rewards or decrement alive_count again|
|160|GET_BASE_ATTACK|ro:4413; monster.cpp:1202|Initial donor row attack 13; rage's 1.3 factor at monster.cpp:2451 is deferred|
|171|GET_MONS_GRADE|ro:1091|Bound Vanguard row's grade 0|
|180|RESET_MOTION|ro:315; character.cpp:725|Clears pending donor playback through native restart; blends/sequences deferred|
|182|SET_MOS|ro:3488|Shift-JIS name lookup from private cfg; flags HOLD/RESTART preserved; per-tick donor step×1.2|
|183|CHECK_MOS_END|ro:3528; character.cpp:740|Donor completion margin, independent of DC1's half-open last-frame test|
|184|NOW_MOS_WAIT|ro:3555; actionchara.cpp:740; character.cpp:1259-1260|Typed float frame ratio (frame-start)/(end-start), not a remaining duration|
|185|GET_MOS_STATUS|ro:3581; actionchara.cpp:808; character.cpp:1190-1280; include/character.hpp:60-64|Typed DC2 NONE=0, START=1, PLAY=2, BLEND=3, END=4. END uses frame+1.2*donor_step>end, including DC1's held frame just below end|
|191|ESM_CREATE|ro:3799|**Logged Unimplemented**|
|194|ESM_SET_VECT1|ro:3847|**Logged Unimplemented**|
|200|ESM_SET_USER_ID|ro:3958|**Logged Unimplemented**; never DC1 SET_MOTION|
|202|ESM_SET_VALUE|ro:3986|**Logged Unimplemented**|

The optional name on 184/185 selects an attached character, not a motion key.
This rigid adapter exposes only root `e202a`; missing attachments produce
typed zero as in DC2. DC1's existing blend state supplies BLEND; exact donor
blend timing and loop-boundary status parity remain unqualified. The eight
explicit optional-effect/event stubs are 47, 116, 117, 137, 191, 194, 200,
202; none is a movement/motion control native. Other unknown ids still log
and refuse without entering DC1's table.

Vanguard textures reserve block **69** only in this bounded opted-in DBC
adapter. DC1 has 72 blocks (valid 0..71); 70 belongs to retail character
effects (`ps2/src/dun/gameloop.cpp:6936-6949`) and 71 to Ridepod. Reservation
checks reject foreign block metadata/textures before any mutation. Reentry
after the retail manager reset recreates only donor textures and keeps the
fallback/neighbor records intact; no ps2 structure or default allocation changes.

`SET_DMG2`'s second argument names a collision radius, not damage. Source
`actionchara.cpp:305` stores that radius; `monster.cpp:2830` sets the actual
primitive damage to the row attack (initial 13). The two reached hit names
are mapped to native DC1 kind 2 (weak) and 3 (blow), flags 0. DC1 defense,
player damage floor and invincibility own the resulting HP change. Segment
form (7 arguments) is not qualified; Vanguard's six registrations use the
single-frame form. Hurt/push cfg object radii are doubled because DC2 uses
size×2 (`collisiondata.hpp:146`), then scaled to host units. Donor elemental
resistances, stagger thresholds and rage policy are not transplanted.

Scale is 1/1.5584790683 = 0.641651222875. Donor VM WAIT/turn/blow ticks run
at 25 Hz using host ClockTickRate; motion and movement advance smoothly at
the host rate with the same rate conversion. No DC2 engine types or globals
cross into the existing isolated ixdc2 Ridepod archive.

Program invocations are adapted in the DC1 lifecycle override:
DC1 setup 1 → DC2 INIT 100 (installed after native encounter initialization),
DC1 MAIN 100 → DC2 MAIN 200; HIT 110 → DAMAGE 500; DEAD 120 → 1000.
The source SB2 program table is unchanged. MAIN resumes via the VM's pc and
call/operand stacks at WAIT; it is restarted only after a hit program ends.
Authority: DC1 `ps2/src/monstorunit.cpp:1976-2008`; DC2
`ps2/src/monster.cpp:1249-1254`, `:2456-2506`, and
`ps2/src/runscript_opcodes.cpp:81-101`.

DC1 CheckDmg decrements alive_count and AddKills on the lethal hit. Donor
DEAD_START never pays; DEAD_OFF sets state −1 once, allowing the existing
DC1 Step reward block (`monstorunit.cpp:2179`, `:2216`) to pay weapon XP and
drop the native floor's assigned key/attachment/money. No second donor
reward or donor save/progression system is invented.
