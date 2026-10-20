#pragma once
#ifdef IXION_HAS_DC2_RIDEPOD
#include <cstdint>
class CCollisionData;
bool RidepodEnabled();
// Only records minted by the bridge receive private policy. Owner 6 alone is
// insufficient. Script ids are the retail owner*10+attack: 60..63, never -1.
int RidepodRecord(float *point, int damage, float radius, int life, int attack);
bool RidepodRecordOwned(CCollisionData *pool, int slot);
uint64_t RidepodRecordToken(CCollisionData *pool, int slot);
void RidepodRecordCancel(CCollisionData *pool, int slot, uint64_t token);
void RidepodRecordsRetire(CCollisionData *live_pool);
#endif
