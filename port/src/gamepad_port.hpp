#pragma once

// Buttons CGamePad::Down ignores until cleared with 0.
void PadHoldDown(int mask);

// Port movement adapters share the lock set by the real CGamePad::KeyLock.
bool PadMovementLocked();
