# Bounded robo.stb combat backend

This is a hand transcription of the decoded PAL robo.stb control table, not
an STB interpreter and not a claim that every native is transplanted. The
private wave5 decoder disassembly is the authority: INIT offsets 944, 1076,
1208 register punch right/left/both on dcol00/dcol01/dcol00, radii 10/10/12,
power 1/1.1/1.2, ratios .36-.44/.25-.36/.38-.42. fd@4596 polls pad history
in .462-.5 and .341-.386 to chain. Donor key frames/steps are supplied by
the private converter; NormalDrive multiplies the key step by 1.2.

MAIN dispatches attack type 0 to that melee routine and 30 to fd@8288.
Machine gun counts three PAL logic steps while native button 56 is held;
_SHOT (actscript.cpp:1886-1890,1947-1949) gates with shot_wait=2 and slot-1
durability. The adapter selects equipped arm01 for Confirm and arm04 for
held Square. This is an explicit two-part mashup control, not the donor's
single equipped arm UI. Release resets the machine gun count. Confirm is
latched across host frames and consumed on the next 25 Hz step.

Only types 0 and 30 are implemented; other types fail without mutations.
No energy gate is invented: robo.stb has none. Arm and gun use independent
private current durability and raw stats, avoiding RefreshParamater's shared
part defect. Durability wear is the adapter's explicit one-unit-per-contact
or shot rule, not a transplant of the donor's full weapon upgrade system.

DC1 damage uses raw donor attack times punch power, rounded up, with 1:1
attack-unit conversion, then DC1 defense/minimum/anger handling. No Toan
weapon bonuses, elemental stats, distance scaling or sword wear apply.

The private host registers owner-6 records with monotonically increasing
sidecar tokens. Only a registered record with `DC_IXION_RIDEPOD=1` receives
that policy; every other record keeps retail CheckDmg conditions. The retail
script-visible `owner*10+attack` mapping publishes **60..63** for right, left,
both and gun, preserving native owners 0..5 and the -1 sentinel.

Arm wear is charged once when CheckDmg accepts a contact or guard, never on
expiry, slot reuse, pause, cancellation or scene retirement. Gun wear remains
one unit per shot. Combat, pending Confirm, private lock and owned records
retire on control loss and before monster-scene reset (including same-floor
reentry). New control binds the current dungeon, floor, character, map,
monster manager and collision pool. Current arm/gun durability survives
those transitions. Lock validity also checks the actor frame/model and life;
successful native actor creation invalidates that slot's old lock even when
the model/frame addresses are reused.
