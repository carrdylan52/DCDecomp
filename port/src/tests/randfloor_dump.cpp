// Dumps the layout that the game's own CDungeonMap::buildRandomMap builds for one map seed.
//
// randfloor_dump <map_seed 0..21474> <floor 1..> <out.json> [dungeon]
//
// A floor that is built for the first time picks `map_seed = rand() / 100000` and reseeds with it
// (ps2/src/dungeonmap.cpp:3454-3458), so the main floor's grid is a function of that one number.
// The port's rand() is a plain LCG (port/src/runtime.cpp:35-41), which makes the seed search below
// exact: it finds the srand() argument whose first rand() yields the wanted map seed, then runs
// the real generator through the same call the game makes (ps2/src/dun/gameloop.cpp:6382).
//
// What is dumped is the main floor only. The back floor is built later by buildRandomMap(6, 0)
// from an RNG state that buildEventData has advanced (gameloop.cpp:6386-6432), so it is not a
// function of the seed alone and is left out. The treasure boxes, atla balls and trap circles are
// what buildEventData draws next from the same RNG stream, so they are dumped as well (the game
// runs nothing between the two calls).
//
// Round 2 adds the monsters. The floor script (d01 event.stb program 100) runs _SET_RANDOM_MAP and
// then _LOAD_MONSTOR, which places the monsters with CMonstorUnit::ArrangementPos(&MainDungeonMap,
// 15, -1, 0) (ps2/src/btsysscript.cpp:638-641, ps2/src/dun/gameloop.cpp:6487-6490). Between the two
// the game finishes BtCleatRandomMap, whose only RNG users are the back floor's buildRandomMap(6, 0)
// and buildEventData(.., 0) (gameloop.cpp:6394-6428; FlushCheckMask, DrawMapCalc and SetupEvent draw
// nothing). The tool runs both for real, so the RNG stream reaches ArrangementPos in the game's state.
// ArrangementPos itself cannot run without the PS2 model and script loads, so Arrange() below is
// a line-for-line copy of it (monstorunit.cpp:550-627) and of SetKey (monstorunit.cpp:454-537) that
// asks the real map for SearchiDoPutArea, CheckTreasureBox, CheckAtra and CheckTrapCircle. The one
// side effect of SetupViewMonstor that moves a monster is the Cave Bat's program 1, which lifts it
// 21 units (see kVerifiedModels). The generator is run twice in one process, and the two dumps must
// be identical.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "boxvu0.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "dungeonparts.hpp"
#include "identity.hpp"
#include "monstorunit.hpp"
#include "savedata.hpp"
#include "userstatus.hpp"

// buildMapDat is a global of ps2/src/dungeonmap.cpp whose struct is private to that file. The
// mirror below has the same layout (BUILD_MAP_INFO, STATIC_ASSERT size 0x4C there).
struct RfBuildCell {
    s32 kind;
    s32 direction;
    s32 link[16];
    s32 content;
};
static_assert(sizeof(RfBuildCell) == 0x4C, "BUILD_MAP_INFO layout");
extern RfBuildCell buildMapDat[400];

namespace {

// BuildCellFlag, ps2/src/dungeonmap.cpp:38-49.
constexpr int kStairDown = 0x080;
constexpr int kStairUp = 0x100;
constexpr int kUraEntrance = 0x200;

constexpr int kRoomMax = 6;

long FindSrandArgument(int map_seed) {
    for (long x = 0; x < 200000000L; x++) {
        srand(static_cast<unsigned int>(x));
        if (static_cast<int>(static_cast<float>(rand()) / static_cast<float>(100000)) == map_seed) {
            return x;
        }
    }
    return -1;
}

void AppendFormat(std::string &out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void AppendFormat(std::string &out, const char *fmt, ...) {
    char    buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);
    out += buffer;
}

void AppendGrid(std::string &out, const char *name, const int *values, bool last) {
    AppendFormat(out, "  \"%s\": [\n", name);
    for (int row = 0; row < 20; row++) {
        out += "    ";
        for (int col = 0; col < 20; col++) {
            AppendFormat(out, "%d%s", values[row * 20 + col], (row == 19 && col == 19) ? "" : ",");
        }
        out += "\n";
    }
    out += last ? "  ]\n" : "  ],\n";
}

struct Located {
    int col = -1;
    int row = -1;
    int count = 0;
};

Located Locate(const int *content, int flag) {
    Located found;
    for (int i = 0; i < 400; i++) {
        if ((content[i] & flag) == flag) {
            found.count++;
            if (found.count == 1) {
                found.col = i % 20;
                found.row = i / 20;
            }
        }
    }
    return found;
}

void AppendLocated(std::string &out, const char *name, const Located &at, const int *dir) {
    if (at.count == 0) {
        AppendFormat(out, "  \"%s\": null,\n", name);
        return;
    }
    AppendFormat(out, "  \"%s\": {\"col\": %d, \"row\": %d, \"direction\": %d, \"count\": %d},\n", name, at.col, at.row,
                 dir[at.row * 20 + at.col], at.count);
}


// ---- Monster arrangement (round 2) -------------------------------------------------------------

// The monsters a floor of dungeon 0 can lay out all run a program 1 (init) that was read from
// dun/monstor/*.stb with the Ixion STB disassembler: none of them calls _GET_RAND, so
// SetupViewMonstor draws no RNG for them. The only one that moves its monster is e60a (Cave Bat,
// MonstorTable[52]): `_GET_POSITION; L1 = L1 + 18 + 3; _SET_POSITION`, a lift of 21. Dran (78)
// sets (200,40,0) but is a boss placed by script, never by ArrangementPos. Models outside this
// list are not verified, so the tool refuses a layout that uses one.
struct VerifiedModel {
    int   monster_no;
    float lift;
};
constexpr VerifiedModel kVerifiedModels[] = {
    {0, 0.0f},  {1, 0.0f},   {2, 0.0f},  {3, 0.0f},  {29, 0.0f}, {30, 0.0f}, {36, 0.0f},
    {51, 0.0f}, {52, 21.0f}, {69, 0.0f}, {96, 0.0f}, {97, 0.0f}, {98, 0.0f},
};

struct ArrMonster {
    int   model_no = -1; // MonstorTable index
    int   base = -1;     // slot in the floor's model list (CMonstorUnit::model)
    float pos[4] = {0, 0, 0, 1};
    float lift = 0.0f;
    int   state = -1;
    int   attempts = 0;
    int   drop_item = -1;
    int   drops_items = 0;
};

struct ArrResult {
    std::vector<ArrMonster> monsters;
    std::vector<int>        model_nos;
    int                     key_holder = -1; // index into monsters
    int                     key_item = -1;
    bool                    key_set = false;
    std::string             error;
};

void ArrSetKey(ArrResult &out, int dungeon, int cur_floor, int alive_count) {
    int       key;
    int       key_index = 0;
    const int keys[3] = {ITEM_SHINY_STONE, ITEM_RED_BERRY, ITEM_POINTY_CHESTNUT};
    if (dungeon == DUNGEON_SHIPWRECK && cur_floor == 16) return;
    int remaining = 1;
    if (dungeon == DUNGEON_WISE_OWL_FOREST) remaining = 3;
    if (alive_count <= 1) return;
    int   attempts = 0;
    auto &monster = out.monsters;
    for (;;) {
        int index = (int) ((float) alive_count * (float) rand() / 2147483648.0f);
        if (index < 0 || index >= alive_count) index = 0;
        if (monster[index].state == -1) continue;
        switch (dungeon) {
            case DUNGEON_DIVINE_BEAST_CAVE: key = ITEM_DRAN_S_CREST; break;
            case DUNGEON_WISE_OWL_FOREST: key = keys[key_index]; break;
            case DUNGEON_SHIPWRECK: key = ITEM_HOOK; break;
            case DUNGEON_SUN_MOON_TEMPLE: key = ITEM_KING_S_SLATE; break;
            case DUNGEON_MOON_SEA: key = ITEM_GUN_POWDER; break;
            case DUNGEON_GALLERY_OF_TIME: key = ITEM_CLOCK_HANDS; break;
            case DUNGEON_DEMON_SHAFT: key = ITEM_BLACK_KNIGHT_CREST; break;
            default: key = -1; break;
        }
        if (monster[index].drop_item == -1 && monster[index].drops_items != 0) {
            // A new game holds no gate key and the floor's random-item table is empty, so the
            // SearchItemIndexNo / CheckItemNo guard (monstorunit.cpp:494) always passes.
            monster[index].drop_item = key;
            if (!out.key_set) {
                out.key_set = true;
                out.key_holder = index;
                out.key_item = key;
            }
            key_index++;
            if (--remaining <= 0) return;
        }
        attempts++;
        if (attempts >= 9999) monster[index].drops_items = 1;
    }
}

// CMonstorUnit::ArrangementPos(map, count, -1, 0) for the floor's own model list.
bool Arrange(CDungeonMap *map, int dungeon, int cur_floor, int count, ArrResult &out) {
    const BT_ENEMY_FLOOR *layout = BtEnemyLayoutList[dungeon] + cur_floor;
    for (int i = 0; i < 9; i++) {
        if (layout->monster[i].monster_no == -1) break;
        out.model_nos.push_back(layout->monster[i].monster_no);
    }
    std::vector<MONSTOR_MODEL> model;
    std::vector<float>         lifts;
    for (int no : out.model_nos) {
        bool  ok = false;
        float lift = 0.0f;
        for (const VerifiedModel &v : kVerifiedModels) {
            if (v.monster_no == no) {
                ok = true;
                lift = v.lift;
            }
        }
        if (!ok) {
            out.error = "monster " + std::to_string(no) + " has no verified program 1";
            return false;
        }
        model.push_back(MonstorTable[no]);
        lifts.push_back(lift);
    }
    const int model_count = static_cast<int>(model.size());
    if (model_count == 0 || model_count > 9) {
        out.error = "floor lays out no monsters";
        return false;
    }
    out.monsters.assign(16, ArrMonster());
    int used[10] = {};
    int alive_count = 0;
    for (int n = 0; n < count; n++) {
        bool  placed = false;
        int   attempts = 0;
        float position[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        while (placed == 0) {
            attempts++;
            if (attempts >= 65000) placed = true;
            SearchiDoPutArea(map->cells, 0, 0, 20, 20, position);
            if (map->CheckTreasureBox(position, 20.0f) != 0 && map->CheckAtra(position, 20.0f) != 0 &&
                map->CheckTrapCircle(position, 20.0f) == nullptr) {
                int close = 0;
                for (int i = 0; i < 16; i++) {
                    if (out.monsters[i].state != -1) {
                        if (DistVector(out.monsters[i].pos, position) <= 25.0f) close = 1;
                    }
                }
                if (close == 0) {
                    int nearby = 0;
                    for (int i = 0; i < 16; i++) {
                        if (out.monsters[i].state != -1) {
                            if (DistVector(out.monsters[i].pos, position) <= 480.0f) nearby++;
                        }
                    }
                    if (nearby < 3) {
                        int selected = (int) ((float) model_count * (float) rand() / 2147483648.0f);
                        if (model[selected].kind != MONSTER_KIND_NORMAL && model[selected].kind != MONSTER_KIND_MIMIC_SMALL) {
                            if (used[selected] != 0) continue;
                            used[selected] = 1;
                        }
                        // SetupViewMonstor: first free slot, then program 1 (the Cave Bat's lift).
                        int slot = -1;
                        for (int i = 0; i < 16; i++) {
                            if (out.monsters[i].state == -1) {
                                slot = i;
                                break;
                            }
                        }
                        if (slot >= 0) {
                            ArrMonster &m = out.monsters[slot];
                            m.model_no = out.model_nos[selected];
                            m.base = selected;
                            std::memcpy(m.pos, position, sizeof m.pos);
                            m.lift = lifts[selected];
                            m.pos[1] += m.lift;
                            m.state = 1;
                            m.attempts = attempts;
                            m.drops_items = model[selected].drops_items;
                            alive_count++;
                        }
                        placed = true;
                    }
                }
            }
        }
    }
    ArrSetKey(out, dungeon, cur_floor, alive_count);
    return true;
}

// Runs the real generator and renders the dump. Returns false if the seed cannot be produced.
bool Generate(int map_seed, int floor_no, int dungeon, std::string &out, long &srand_arg) {
    srand_arg = FindSrandArgument(map_seed);
    if (srand_arg < 0) {
        return false;
    }

    // A new game's save: PresetSmallItemNo_Get asks it which weapons the player holds.
    alignas(64) static CSaveData save;
    std::memset(static_cast<void *>(&save), 0, sizeof save);
    save.Initialize();
    SaveData = &save;
    CUserStatus &status = *reinterpret_cast<CUserStatus *>(save.GetDngStatus());
    status.cur_floor = static_cast<s8>(floor_no - 1);
    status.party_size = 1;
    status.res_limit_zone_current = -1;
    UserStatus = &status;
    selectMapNo = dungeon;

    auto map = std::make_unique<CDungeonMap>();
    map->map_type = 1;

    srand(static_cast<unsigned int>(srand_arg));
    map->buildRandomMap(kRoomMax, 1);
    if (map->map_seed != map_seed) {
        std::fprintf(stderr, "map_seed %d != wanted %d\n", map->map_seed, map_seed);
        return false;
    }

    int parts[400];
    int dir[400];
    int content[400];
    int links[400];
    for (int i = 0; i < 400; i++) {
        parts[i] = map->cells[i].parts_no;
        dir[i] = map->cells[i].direction;
        content[i] = buildMapDat[i].content;
        int mask = 0;
        for (int r = 0; r < 16; r++) {
            if (buildMapDat[i].link[r] == 1) {
                mask |= 1 << r;
            }
        }
        links[i] = mask;
    }

    // What the game does next with the same RNG stream (gameloop.cpp:6384-6386): the treasure
    // boxes, trap circles and atla balls of the main floor. The positions are world units; a
    // cell's centre is (160 * col, 160 * row).
    map->buildEventData(status.cur_floor, map->map_type, 1);

    // The rest of BtCleatRandomMap (gameloop.cpp:6394-6428): the back floor is a copy of the main
    // one rebuilt with first_build = 0, so for this dump it only advances the RNG stream.
    auto ura = std::make_unique<CDungeonMap>();
    *ura = *map;
    ura->buildRandomMap(kRoomMax, 0);
    ura->buildEventData(status.cur_floor, ura->map_type, 0);

    // _LOAD_MONSTOR: BtLoadMonstor (no RNG) and BtArrengeMonstor.
    ArrResult arrangement;
    const bool arranged = dungeon == 0 && Arrange(map.get(), dungeon, status.cur_floor, 15, arrangement);

    out.clear();
    out += "{\n";
    out += "  \"format\": \"ixion-dc1-randfloor\",\n  \"version\": 1,\n";
    AppendFormat(out, "  \"dungeon\": %d,\n  \"floor\": %d,\n  \"cur_floor\": %d,\n", dungeon, floor_no, floor_no - 1);
    AppendFormat(out, "  \"map_seed\": %d,\n  \"srand_argument\": %ld,\n", map->map_seed, srand_arg);
    AppendFormat(out, "  \"room_max\": %d,\n  \"first_build\": 1,\n", kRoomMax);
    out += "  \"grid\": {\"width\": 20, \"height\": 20, \"playable\": 16, \"cell_size\": 160},\n";
    AppendFormat(out, "  \"gate_key_no\": %d,\n", map->gate_key_no);
    AppendFormat(out, "  \"room_num\": %d,\n  \"rooms\": [", map->room_num);
    for (int i = 0; i < map->room_num; i++) {
        AppendFormat(out, "%s{\"x\": %d, \"y\": %d, \"width\": %d, \"height\": %d}", i ? ", " : "", map->rooms[i].x,
                     map->rooms[i].y, map->rooms[i].width, map->rooms[i].height);
    }
    out += "],\n";
    AppendLocated(out, "stair_down", Locate(content, kStairDown), dir);
    AppendLocated(out, "stair_up", Locate(content, kStairUp), dir);
    AppendLocated(out, "ura_entrance", Locate(content, kUraEntrance), dir);
    out += "  \"chests\": [";
    int listed = 0;
    for (int i = 0; i < 24; i++) {
        if (map->boxes[i].used == 0) {
            continue;
        }
        AppendFormat(out, "%s{\"x\": %.3f, \"y\": %.3f, \"z\": %.3f, \"item_no\": %d, \"kind\": %d, \"trap_no\": %d}",
                     listed++ ? ", " : "", map->boxes[i].pos[0], map->boxes[i].pos[1], map->boxes[i].pos[2],
                     map->boxes[i].item_no, map->boxes[i].kind, map->boxes[i].trap_no);
    }
    out += "],\n  \"atla\": [";
    listed = 0;
    for (int i = 0; i < 8; i++) {
        if (map->atra[i].used == 0) {
            continue;
        }
        AppendFormat(out, "%s{\"x\": %.3f, \"y\": %.3f, \"z\": %.3f, \"atra_no\": %d}", listed++ ? ", " : "",
                     map->atra[i].pos[0], map->atra[i].pos[1], map->atra[i].pos[2], map->atra[i].atra_no);
    }
    out += "],\n  \"trap_circles\": [";
    listed = 0;
    for (int i = 0; i < 3; i++) {
        if (map->trap_circle[i].state == 0) {
            continue;
        }
        AppendFormat(out, "%s{\"x\": %.3f, \"z\": %.3f, \"kind\": %d}", listed++ ? ", " : "", map->trap_circle[i].pos[0],
                     map->trap_circle[i].pos[2], map->trap_circle[i].kind);
    }
    out += "],\n";
    out += "  \"monsters\": {\"source\": \"CMonstorUnit::ArrangementPos(&MainDungeonMap, 15, -1, 0)\", ";
    if (!arranged) {
        AppendFormat(out, "\"available\": false, \"reason\": \"%s\"},\n",
                     dungeon != 0 ? "only dungeon 0 is verified" : arrangement.error.c_str());
        out += "  \"gate_key\": null,\n";
    } else {
        out += "\"available\": true, \"requested\": 15, \"model_nos\": [";
        for (size_t i = 0; i < arrangement.model_nos.size(); i++) {
            AppendFormat(out, "%s%d", i ? ", " : "", arrangement.model_nos[i]);
        }
        out += "], \"list\": [";
        int n = 0;
        for (int i = 0; i < 16; i++) {
            const ArrMonster &m = arrangement.monsters[i];
            if (m.state == -1) continue;
            AppendFormat(out,
                         "%s{\"slot\": %d, \"monster_no\": %d, \"model\": \"%s\", \"x\": %.3f, \"y\": %.3f, \"z\": %.3f, "
                         "\"lift\": %.1f, \"drops_items\": %d, \"attempts\": %d, \"holds_gate_key\": %d}",
                         n++ ? ", " : "", i, m.model_no, MonstorTable[m.model_no].model_name[0], m.pos[0], m.pos[1],
                         m.pos[2], m.lift, m.drops_items, m.attempts, arrangement.key_holder == i ? 1 : 0);
        }
        AppendFormat(out, "], \"placed\": %d},\n", n);
        AppendFormat(out,
                     "  \"gate_key\": {\"item_no\": %d, \"holder_slot\": %d, \"rule\": \"CMonstorUnit::SetKey: one random live monster that drops items holds the key, chosen at arrangement (monstorunit.cpp:454-537)\"},\n",
                     arrangement.key_item, arrangement.key_holder);
    }
    AppendGrid(out, "parts_no", parts, false);
    AppendGrid(out, "direction", dir, false);
    AppendGrid(out, "content", content, false);
    AppendGrid(out, "room_links", links, true);
    out += "}\n";
    return true;
}

} // namespace

int main(int argc, char **argv) {
    GameSetIdentity();
    if (argc < 4) {
        std::fprintf(stderr, "usage: randfloor_dump <map_seed> <floor> <out.json> [dungeon]\n");
        return 2;
    }
    const int map_seed = std::atoi(argv[1]);
    const int floor_no = std::atoi(argv[2]);
    const int dungeon = argc > 4 ? std::atoi(argv[4]) : 0;

    std::string first;
    std::string second;
    long        arg_first = 0;
    long        arg_second = 0;
    if (!Generate(map_seed, floor_no, dungeon, first, arg_first) || !Generate(map_seed, floor_no, dungeon, second, arg_second)) {
        std::fprintf(stderr, "randfloor_dump: cannot build seed %d\n", map_seed);
        return 1;
    }
    const bool identical = first == second;
    std::fprintf(stderr, "randfloor_dump: seed=%d srand_argument=%ld in_process_identical=%d bytes=%zu\n", map_seed,
                 arg_first, identical ? 1 : 0, first.size());
    if (!identical) {
        return 3;
    }
    std::FILE *file = std::fopen(argv[3], "wb");
    if (!file) {
        std::fprintf(stderr, "randfloor_dump: cannot open %s\n", argv[3]);
        return 1;
    }
    std::fwrite(first.data(), 1, first.size(), file);
    std::fclose(file);
    return 0;
}
