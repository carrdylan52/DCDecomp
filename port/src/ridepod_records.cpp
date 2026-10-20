#include "collisiondata.hpp"
#include "ridepod_records.hpp"
#ifdef IXION_HAS_DC2_RIDEPOD
#include <cstdlib>
#include <cstring>
#include "dun/gameloop.hpp"
namespace {
struct Registration {
    CCollisionData *pool = nullptr;
    uint64_t token = 0;
    int attack = -1;
};
Registration records[96];
uint64_t next_token = 0;
void Reuse(CCollisionData *pool, int slot) {
    // Reallocation invalidates even a later record with identical owner/attack.
    if (records[slot].pool == pool) records[slot] = {};
}
}
bool RidepodEnabled() {
    const char *value = std::getenv("DC_IXION_RIDEPOD");
    return value && std::strcmp(value,"1") == 0;
}
uint64_t RidepodRecordToken(CCollisionData *pool, int slot) {
    if (!pool || slot < 0 || slot >= 96) return 0;
    const auto &r = records[slot];
    return r.pool == pool && pool->active[slot] && pool->hit[slot].owner == 6 &&
        pool->hit[slot].attack_no == r.attack ? r.token : 0;
}
bool RidepodRecordOwned(CCollisionData *pool, int slot) {
    return RidepodEnabled() && RidepodRecordToken(pool,slot) != 0;
}
int RidepodRecord(float *point, int damage, float radius, int life, int attack) {
    if (!RidepodEnabled() || !NowColData || attack < 0 || attack > 3) return -1;
    bool space = false;
    for (int active : NowColData->active) if (!active) { space = true; break; }
    if (!space) return -1;
    int slot = NowColData->Set(point,damage,life,radius,0,2,2,0,0);
    NowColData->SetUserID(6,attack);
    NowColData->hit[slot].weapon_flags = 0;
    records[slot] = {NowColData,++next_token,attack};
    return slot;
}
void RidepodRecordCancel(CCollisionData *pool, int slot, uint64_t token) {
    if (!pool || !token || slot < 0 || slot >= 96 || records[slot].pool != pool || records[slot].token != token) return;
    if (RidepodRecordToken(pool,slot) == token) pool->active[slot] = 0;
    records[slot] = {};
}
void RidepodRecordsRetire(CCollisionData *live_pool) {
    for (int slot=0; slot<96; ++slot) {
        // On pointer drift the old pool may already be gone. Never dereference it.
        if (records[slot].pool == live_pool && records[slot].token)
            RidepodRecordCancel(live_pool,slot,records[slot].token);
        records[slot] = {};
    }
}
#endif

// Retail allocator, with a private sidecar invalidation at slot reuse. All
// retail fields, selection order and default behavior remain unchanged.
PORT_OVERRIDE int CCollisionData::Set(float *position, int damage, int life, float radius, float scale_rate, int target_mask, int kind, int flags, int attribute) {
    for (int i = 0; i < 96; i++) {
        if (active[i] == 0) {
            // RIDEPOD_BEGIN
#ifdef IXION_HAS_DC2_RIDEPOD
            Reuse(this,i);
#endif
            // RIDEPOD_END
            active[i] = 1;
            sceVu0CopyVector(hit[i].pos, position);
            hit[i].base_point[0] = hit[i].base_point[1] = hit[i].base_point[2] = 0.0f;
            hit[i].base_point[3] = 1.0f;
            hit[i].velocity[0] = hit[i].velocity[1] = hit[i].velocity[2] = 0.0f;
            hit[i].velocity[0] = 1.0f;
            hit[i].damage = damage;
            hit[i].scale_rate = scale_rate;
            hit[i].life = life;
            hit[i].radius = radius;
            hit[i].target_mask = target_mask;
            hit[i].kind = kind;
            hit[i].flags = flags;
            hit[i].attribute = attribute;
            hit[i].owner = -1;
            hit[i].monster_no = -1;
            hit[i].attack_no = -1;
            hit[i].vs_monster = NULL;
            hit[i].weapon_flags = 1;
            hit[i].target_kind = -1;
            hit[i].knockback_origin[0] = hit[i].knockback_origin[1] = hit[i].knockback_origin[2] = 0.0f;
            hit[i].knockback_origin[3] = 1.0f;
            hit[i].knockback_speed = 0.0f;
            hit[i].knockback_decay = 0.0f;
            hit[i].knockback_mode = 0;
            hit[i].phase = hit[i].ready_phase = 0;
            now_hit = i;
            return i;
        }
    }
    // With every record in use nothing is set and no index is returned.
}
