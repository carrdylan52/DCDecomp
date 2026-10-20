#include "ixion_delivery_lock.hpp"
#include "ixion_delivery.hpp"
#include "ixion_ledger.hpp"

#include "dngstatusdata.hpp"
#include "dispctrl.hpp"
#include "dun/gameloop.hpp"
#include "edit.hpp"
#include "editground.hpp"
#include "editloop3.hpp"
#include "gameloop.hpp"
#include "main.hpp"
#include "savedata.hpp"
#include "sysmes.hpp"
#include "platform/files.hpp"
#include "platform/paths.hpp"
#include <cstdio>
#include <cstdlib>

extern s32 mode;
extern s32 gameTask;
extern s32 existFlag;

bool IxionLedgerDungeonSafePointOpen(const IxionLedgerDungeonState &s) {
    return s.mode == GAME_MODE_DUNGEON && s.task == GAME_TASK_PLAY &&
           s.battle_mode == BT_GAME_MODE_PLAY && !s.system_message && !s.overflow &&
           s.event_mode == 0 && s.exit_request == 0 && s.alive && !s.fading;
}

namespace {
int CountWater(const CDngStatusData &status) {
    int count = 0;
    for (auto item : status.item_pack.item) if (item == ITEM_REGULAR_WATER) ++count;
    for (int i = 0; i < 3; ++i) {
        if (status.item_pack.quick_item_slot[i] == ITEM_REGULAR_WATER)
            count += status.item_pack.quick_item_qty[i];
    }
    return count;
}
}

int IxionLedgerGrantWater(CDngStatusData &status, uint64_t seq) {
    static_assert(ITEM_REGULAR_WATER == ixion::delivery::kRegularWater);
    const int before = CountWater(status);
    // GetItem has a silent-loss overflow case. The persisted cursor is
    // never rewound, even if its return value did not store the item.
    const int result = status.GetItem(ITEM_REGULAR_WATER, 0);
    const int after = CountWater(status);
    const int stored = after == before + 1 ? 1 : 0;
    std::fprintf(stderr, "ixion ledger consumed seq=%llu item=145 cursor_committed=1 before=%d after=%d gate_result=%d stored=%d\n",
                 static_cast<unsigned long long>(seq), before, after, result, stored);
    if (!stored)
        std::fprintf(stderr, "ixion ledger LOST item seq=%llu: native inventory full (overflow); delivery stays consumed, no retry\n",
                     static_cast<unsigned long long>(seq));
    return stored;
}

int IxionLedgerDeliverWater(CDngStatusData &status, uint64_t seq, IxionLedgerItemMessage message) {
    const int stored = IxionLedgerGrantWater(status, seq);
    if (stored && !status.overflow_flag) {
        message(ITEM_REGULAR_WATER, -1, 0x78, 0);
        std::fprintf(stderr, "ixion ledger pickup-message seq=%llu item=145 stored=1\n",
                     static_cast<unsigned long long>(seq));
    }
    return stored;
}

void IxionLedgerApplyAtSafePoint() {
    static const char *setting = std::getenv("IXION_LEDGER");
    if (!setting || !*setting || !SaveData) return;
    auto &status = *SaveData->GetDngStatus();
    // Town retains its existing mode gate. Both modes must leave a native
    // message and pending inventory overflow alone.
    if (SystemMesCheck() || status.overflow_flag) return;
    if (mode != GAME_MODE_EDIT) {
        if (mode != GAME_MODE_DUNGEON || UserStatus != &status) return;
        float fade[4];
        EdGetFadeColor(fade);
        if (!IxionLedgerDungeonSafePointOpen({mode, gameTask, BtGameModeFlag, false,
                status.overflow_flag, BtEventMode, existFlag, UserStatus->CheckLife() > 0,
                fade[3] > 0.0f || DispFade__3.GetRate() > 0.0f})) return;
    }
    static int last_poll = -30;
    const int frame = GameFrameCount();
    if (frame >= last_poll && frame - last_poll < 30) return;
    last_poll = frame;
    const auto path = PathsFromUtf8(setting);
    const auto cursor = PathsSaveRoot() / "ixion-delivery.cursor.json";
    // Same order in every participant: ledger, then recipient cursor. Keep
    // both locks until the native grant returns; another process cannot race.
    ixion::delivery::Lock ledger_lock(path);
    if (!ledger_lock.ok()) return;
    ixion::delivery::Lock cursor_lock(cursor);
    if (!cursor_lock.ok()) return;
    auto write = [](const auto &p, const std::string &text) {
        return FilesWrite(p, text.data(), text.size(), true) == FilesResult::kWritten;
    };
    auto grant = [](const ixion::delivery::Record &record) {
        IxionLedgerDeliverWater(*SaveData->GetDngStatus(), record.seq, ItemGetMes);
    };
    uint64_t applied = 0;
    std::string error;
    static std::string last_error;
    static bool reported = false;
    // One record at a time: a stored grant opens its native message, so the
    // next record waits until that message (or an overflow menu) has closed.
    if (!ixion::delivery::Consume(path, cursor, write, grant, applied, error, 1)) {
        ixion::delivery::LogConsumeRefusal(error, last_error);
    } else {
        if (!reported || !last_error.empty() || applied) {
            std::fprintf(stderr, "ixion ledger safe-point applied=%llu inventory_water=%d mode=%d task=%d frame=%lld\n",
                         static_cast<unsigned long long>(applied), CountWater(status), mode, gameTask,
                         static_cast<long long>(GameFrameCount()));
            reported = true;
        }
        last_error.clear();
    }
}
