#include "dngstatusdata.hpp"
#include "ixion_ledger.hpp"
#include "dun/gameloop.hpp"
#include "main.hpp"
#include <gtest/gtest.h>
#include <algorithm>

TEST(IxionLedgerGate, DungeonRefusesEveryNonPlayTaskAndBattleMode) {
    IxionLedgerDungeonState s{GAME_MODE_DUNGEON, GAME_TASK_PLAY, BT_GAME_MODE_PLAY,
                             false, false, 0, 0, true, false};
    ASSERT_TRUE(IxionLedgerDungeonSafePointOpen(s));
    // The gate is an allowlist: every other task value (including unknown
    // values between the retail tasks) must refuse, not just named menus.
    for (int task = -1; task <= GAME_TASK_FLOOR_FADE_RESET + 1; ++task) {
        s.task = task;
        EXPECT_EQ(IxionLedgerDungeonSafePointOpen(s), task == GAME_TASK_PLAY) << task;
    }
    s.task = GAME_TASK_PLAY;
    for (int battle = -1; battle <= BT_GAME_MODE_ESCAPE_MSG + 1; ++battle) {
        s.battle_mode = battle;
        EXPECT_EQ(IxionLedgerDungeonSafePointOpen(s), battle == BT_GAME_MODE_PLAY) << battle;
    }
}

TEST(IxionLedgerGate, DungeonBlockingStateTable) {
    const IxionLedgerDungeonState open{GAME_MODE_DUNGEON, GAME_TASK_PLAY, BT_GAME_MODE_PLAY,
                                      false, false, 0, 0, true, false};
    struct Case { const char *name; IxionLedgerDungeonState state; };
    auto message = open; message.system_message = true;
    auto overflow = open; overflow.overflow = true;
    auto event = open; event.event_mode = 1;
    auto exiting = open; exiting.exit_request = 1;
    auto thrown_out = open; thrown_out.exit_request = 2;
    auto dead = open; dead.alive = false;
    auto fade = open; fade.fading = true;
    auto town = open; town.mode = GAME_MODE_EDIT;
    for (const Case &c : {Case{"message", message}, {"overflow", overflow}, {"event", event},
                         {"exit", exiting}, {"thrown out", thrown_out}, {"dead before task transition", dead},
                         {"fade", fade}, {"town is not dungeon", town}}) {
        EXPECT_FALSE(IxionLedgerDungeonSafePointOpen(c.state)) << c.name;
    }
}

TEST(IxionLedgerGate, OverflowNeverShowsPickupMessage) {
    CDngStatusData status{};
    status.item_pack.num = 50;
    std::fill(std::begin(status.item_pack.item), std::end(status.item_pack.item), -1);
    std::fill_n(status.item_pack.item, 50, ITEM_BREAD);
    std::fill(std::begin(status.item_pack.quick_item_slot), std::end(status.item_pack.quick_item_slot), -1);
    static int messages;
    messages = 0;
    const auto message = +[](int, int, int, int) { ++messages; };
    // Native temporary overflow stores a Water but must open its menu, not
    // a success message. Full/latched overflow then silently loses the item.
    EXPECT_EQ(IxionLedgerDeliverWater(status, 1, message), 1);
    EXPECT_TRUE(status.overflow_flag);
    EXPECT_EQ(messages, 0);
    std::fill_n(status.item_pack.item, 53, ITEM_BREAD);
    EXPECT_EQ(IxionLedgerDeliverWater(status, 2, message), 0);
    EXPECT_EQ(messages, 0);
}

TEST(IxionLedgerGate, RegularWaterUsesTheRealDungeonItemGrant) {
    CDngStatusData status{};
    status.item_pack.num = 50;
    std::fill(std::begin(status.item_pack.item), std::end(status.item_pack.item), -1);
    std::fill(std::begin(status.item_pack.quick_item_slot), std::end(status.item_pack.quick_item_slot), -1);
    status.overflow_flag = false;
    EXPECT_EQ(status.GetItem(ITEM_REGULAR_WATER, 0), 0);
    EXPECT_EQ(status.item_pack.item[0], ITEM_REGULAR_WATER);
    EXPECT_EQ(std::count(std::begin(status.item_pack.item), std::end(status.item_pack.item), ITEM_REGULAR_WATER), 1);
}

TEST(IxionLedgerGate, LatchedFullOverflowCanReportSuccessWithoutStoring) {
    CDngStatusData status{};
    status.item_pack.num = 50;
    std::fill(std::begin(status.item_pack.item), std::end(status.item_pack.item), -1);
    std::fill_n(status.item_pack.item, 53, ITEM_BREAD);
    std::fill(std::begin(status.item_pack.quick_item_slot), std::end(status.item_pack.quick_item_slot), -1);
    status.overflow_flag = true;
    EXPECT_GE(status.GetItem(ITEM_REGULAR_WATER, 0), 0);
    EXPECT_EQ(std::count(std::begin(status.item_pack.item), std::end(status.item_pack.item), ITEM_REGULAR_WATER), 0);
}

TEST(IxionLedgerGate, OverflowLossIsLoggedLoudly) {
    CDngStatusData status{};
    status.item_pack.num = 50;
    std::fill(std::begin(status.item_pack.item), std::end(status.item_pack.item), -1);
    std::fill(std::begin(status.item_pack.quick_item_slot), std::end(status.item_pack.quick_item_slot), -1);
    status.overflow_flag = false;
    testing::internal::CaptureStderr();
    EXPECT_EQ(IxionLedgerGrantWater(status, 1), 1);
    std::string log = testing::internal::GetCapturedStderr();
    EXPECT_NE(log.find("stored=1"), std::string::npos) << log;
    EXPECT_EQ(log.find("LOST"), std::string::npos) << log;
    std::fill_n(status.item_pack.item, 53, ITEM_BREAD);
    status.overflow_flag = true;
    testing::internal::CaptureStderr();
    EXPECT_EQ(IxionLedgerGrantWater(status, 2), 0);
    log = testing::internal::GetCapturedStderr();
    EXPECT_NE(log.find("ixion ledger LOST item seq=2: native inventory full"), std::string::npos) << log;
}
