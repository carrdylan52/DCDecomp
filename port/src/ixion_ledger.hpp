#pragma once
#include <cstdint>
class CDngStatusData;
// Values sampled on the game thread, before ModeLoop; never during a Hop switch.
struct IxionLedgerDungeonState {
    int mode, task, battle_mode;
    bool system_message, overflow;
    int event_mode, exit_request;
    bool alive, fading;
};
bool IxionLedgerDungeonSafePointOpen(const IxionLedgerDungeonState &state);
void IxionLedgerApplyAtSafePoint();
using IxionLedgerItemMessage = void (*)(int, int, int, int);
// The message callback runs only after a stored, non-overflow grant.
int IxionLedgerDeliverWater(CDngStatusData &status, uint64_t seq, IxionLedgerItemMessage message);
// Grants one Regular Water through the native gate and returns 1 when the
// inventory count rose. The native full/latched-overflow path can return
// success without storing; that loss is logged loudly (the cursor is
// already committed and is never rewound).
int IxionLedgerGrantWater(CDngStatusData &status, uint64_t seq);
