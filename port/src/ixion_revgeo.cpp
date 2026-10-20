// Standard headers must precede the game's one-letter GS macros, so the order below is kept.
// clang-format off
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>

#include "common.h"
#include "ixion_revgeo.hpp"
// clang-format on

#include "character.hpp"
#include "dispctrl.hpp"
#include "editatra.hpp"
#include "editground.hpp"
#include "editloop.hpp"
#include "editmapscript.hpp"
#include "editpartsdata.hpp"
#include "editpartsinfo.hpp"
#include "mapparts.hpp"
#include "memcard.hpp"
#include "menu_draw.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "save_slots.hpp"
#include "savedata.hpp"
#include "snd.hpp"
#include "texture.hpp"

namespace fs = std::filesystem;

namespace ixion_revgeo {
namespace {

constexpr int    kTextureBlock = 71; // the Ridepod's block; the editor leaves it free
constexpr int    kCardCell = 17;     // part-picture sheet cell: u = (n / 6) * 84 + 260, v = (n % 6) * 84
constexpr int    kMaxStock = 16;
constexpr size_t kMaxFile = 8u * 1024 * 1024;
constexpr char   kSidecarMagic[8] = {'I', 'X', 'R', 'G', 'S', 'C', '0', '1'};

struct Picture {
    std::string                name;
    int                        width = 0;
    int                        height = 0;
    u_long                     tex1 = 0;
    std::vector<unsigned char> rgba;
};

struct Pack {
    bool                   ok = false;
    std::vector<u_long128> scene; // 16-byte aligned, as the editor addresses a scene
    size_t                 scene_bytes = 0;
    std::string            name;
    std::string            display;
    int                    stock = 4;
    int                    cells[2] = {1, 1};
    std::vector<Picture>   pictures;
    EDIT_PARTS_ATRA        atra;
    char                   empty_names[4][1] = {{0}, {0}, {0}, {0}};
};

Pack &Instance() {
    static Pack pack;
    return pack;
}

bool attempted = false;

std::vector<unsigned char> ReadFile(const fs::path &path, size_t limit) {
    std::error_code error;
    auto            size = fs::file_size(path, error);
    if (error || size == 0 || size > limit) {
        throw std::runtime_error("private file missing or out of bounds: " + path.string());
    }
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    std::ifstream              stream(path, std::ios::binary);
    if (!stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size))) {
        throw std::runtime_error("private file read failed: " + path.string());
    }
    return bytes;
}

void Load() {
    attempted = true;
    const char *enable = std::getenv("DC_IXION_REVGEO");
    if (enable == nullptr || std::strcmp(enable, "1") != 0) {
        return;
    }
    Pack &pack = Instance();
    try {
        const char *directory = std::getenv("DC_IXION_REVGEO_PACK");
        if (directory == nullptr || *directory == '\0') {
            throw std::runtime_error("DC_IXION_REVGEO_PACK is required");
        }
        fs::path root(directory);
        auto     manifest_bytes = ReadFile(root / "revgeo.json", 1 << 20);
        auto     manifest = nlohmann::json::parse(manifest_bytes.begin(), manifest_bytes.end());
        if (manifest.at("format") != "ixion-revgeo-dc1" || manifest.at("version") != 1 || manifest.at("town") != kTown ||
            manifest.at("plot") != kPlot || manifest.at("scene") != "revgeo.scn") {
            throw std::runtime_error("private pack contract mismatch");
        }
        auto &part = manifest.at("part");
        pack.name = part.at("name").get<std::string>();
        pack.display = part.at("display").get<std::string>();
        pack.stock = part.at("stock").get<int>();
        pack.cells[0] = part.at("cells").at(0).get<int>();
        pack.cells[1] = part.at("cells").at(1).get<int>();
        if (pack.name.empty() || pack.name.size() > 15 || pack.stock < 1 || pack.stock > kMaxStock || pack.cells[0] < 1 ||
            pack.cells[1] < 1 || pack.cells[0] * pack.cells[1] > 32) {
            throw std::runtime_error("private part record invalid");
        }
        auto scene = ReadFile(root / "revgeo.scn", kMaxFile);
        if (scene.size() < 64 || std::memcmp(scene.data(), "SCN\0", 4) != 0) {
            throw std::runtime_error("private scene is not a scene archive");
        }
        pack.scene.assign((scene.size() + 15) / 16, u_long128{});
        std::memcpy(pack.scene.data(), scene.data(), scene.size());
        pack.scene_bytes = scene.size();
        for (auto &entry : manifest.at("textures")) {
            Picture     picture;
            std::string file = entry.at("file");
            picture.name = entry.at("name").get<std::string>();
            picture.width = entry.at("width").get<int>();
            picture.height = entry.at("height").get<int>();
            picture.tex1 = entry.at("tex1").get<u_long>();
            if (picture.name.rfind("ixrg_", 0) != 0 || picture.name.size() > 31 || file != picture.name + ".rgba" ||
                file.find_first_of("/\\:") != std::string::npos || picture.width <= 0 || picture.height <= 0 ||
                picture.width > 2048 || picture.height > 2048) {
                throw std::runtime_error("private picture contract mismatch");
            }
            picture.rgba = ReadFile(root / file, static_cast<size_t>(picture.width) * picture.height * 4);
            if (picture.rgba.size() != static_cast<size_t>(picture.width) * picture.height * 4) {
                throw std::runtime_error("private picture truncated");
            }
            pack.pictures.push_back(std::move(picture));
        }
        if (pack.pictures.empty()) {
            throw std::runtime_error("private pack has no pictures");
        }

        std::memset(&pack.atra, 0, sizeof(pack.atra));
        pack.atra.max = pack.stock;
        pack.atra.stackable = 0;
        // Retail has no picture or name for a part it never shipped. The card's picture is the pack's own
        // (CardPicture, drawn over the otherwise unused sheet cell 17 with the name baked in) and its two
        // messages are blank (GetAtraMsgNo below; a negative msg_no reads as message 0).
        pack.atra.tex_no = kCardCell;
        pack.atra.msg_no = -1;
        pack.atra.kind = 0;
        for (auto &element : pack.atra.elements) {
            element.id = -1;
            element.required_element = -1;
            for (auto &name : element.names) {
                name = pack.empty_names[0];
            }
            element.npc_no = -1;
        }
        pack.ok = true;
        std::fprintf(stderr, "ixion revgeo: pack loaded part=%s plot=%d cells=%dx%d stock=%d pictures=%zu scene=%zu bytes\n",
                     pack.name.c_str(), kPlot, pack.cells[0], pack.cells[1], pack.stock, pack.pictures.size(),
                     pack.scene_bytes);
    } catch (const std::exception &error) {
        pack.ok = false;
        std::fprintf(stderr, "ixion revgeo: refused: %s\n", error.what());
    }
}

Pack *Ready() {
    if (!attempted) {
        Load();
    }
    return Instance().ok ? &Instance() : nullptr;
}

uint32_t Checksum(const unsigned char *data, size_t size) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ data[i]) * 16777619u;
    }
    return hash;
}

fs::path SidecarPath() {
    int slot = static_cast<s32 *>(SaveData->GetConfigData())[17];
    if (slot < 0 || slot > 99999) {
        slot = 0;
    }
    return SaveSlotsRoot() / ("ixion-revgeo-slot" + std::to_string(slot) + ".bin");
}

bool ReadSidecar(std::vector<SV_GRD_PART> &out) {
    std::error_code error;
    fs::path        path = SidecarPath();
    if (!fs::exists(path, error)) {
        return false;
    }
    try {
        auto        bytes = ReadFile(path, 4096);
        std::string why;
        if (!DecodeSidecar(bytes.data(), bytes.size(), out, why)) {
            throw std::runtime_error(why);
        }
        return true;
    } catch (const std::exception &error_text) {
        std::fprintf(stderr, "ixion revgeo: sidecar refused: %s\n", error_text.what());
        out.clear();
        return false;
    }
}

} // namespace

std::vector<unsigned char> EncodeSidecar(const std::vector<SV_GRD_PART> &placed) {
    std::vector<unsigned char> bytes(16 + placed.size() * sizeof(SV_GRD_PART) + 4);
    uint32_t                   version = 1, count = static_cast<uint32_t>(placed.size());
    std::memcpy(bytes.data(), kSidecarMagic, 8);
    std::memcpy(bytes.data() + 8, &version, 4);
    std::memcpy(bytes.data() + 12, &count, 4);
    if (!placed.empty()) {
        std::memcpy(bytes.data() + 16, placed.data(), placed.size() * sizeof(SV_GRD_PART));
    }
    uint32_t checksum = Checksum(bytes.data(), bytes.size() - 4);
    std::memcpy(bytes.data() + bytes.size() - 4, &checksum, 4);
    return bytes;
}

bool DecodeSidecar(const unsigned char *data, size_t size, std::vector<SV_GRD_PART> &out, std::string &why) {
    out.clear();
    if (data == nullptr || size < 20 || std::memcmp(data, kSidecarMagic, 8) != 0) {
        why = "bad sidecar header";
        return false;
    }
    uint32_t version, count, checksum;
    std::memcpy(&version, data + 8, 4);
    std::memcpy(&count, data + 12, 4);
    if (version != 1 || count > 64 || size != 16 + count * sizeof(SV_GRD_PART) + 4) {
        why = "bad sidecar extent";
        return false;
    }
    std::memcpy(&checksum, data + size - 4, 4);
    if (checksum != Checksum(data, size - 4)) {
        why = "sidecar checksum mismatch";
        return false;
    }
    std::vector<SV_GRD_PART> records(count);
    std::memcpy(records.data(), data + 16, count * sizeof(SV_GRD_PART));
    for (const SV_GRD_PART &record : records) {
        if (record.part_id != kPlot || record.rot_y < -1 || record.rot_y > 3 || !std::isfinite(record.pos_x) ||
            !std::isfinite(record.pos_y) || !std::isfinite(record.pos_z)) {
            why = "sidecar record invalid";
            return false;
        }
    }
    out = std::move(records);
    return true;
}

bool Active(int town) {
    return town == kTown && Ready() != nullptr;
}

u_int *Scene() {
    Pack *pack = Ready();
    return pack != nullptr ? reinterpret_cast<u_int *>(pack->scene.data()) : nullptr;
}

void OnMapData(EDIT_MAP_INFO *info, int town) {
    Pack *pack = town == kTown ? Ready() : nullptr;
    if (pack == nullptr || info == nullptr) {
        return;
    }
    MAP_PARTS_INFO &tree = info->parts_work.general.parts[13];
    MAP_PARTS_INFO &part = info->parts_work.general.parts[kPlot];
    std::memset(&part, 0, sizeof(part));
    std::snprintf(part.name[0], sizeof(part.name[0]), "%s", pack->name.c_str());
    part.parts_no = kPlot;
    part.subtype = 0;
    part.kind = tree.kind;
    part.lift = 0.0f;
    std::fprintf(stderr, "ixion revgeo: plot %d named %s (tree plot 13: subtype=%d kind=%d lift=%.2f)\n", kPlot,
                 part.name[0], tree.subtype, tree.kind, tree.lift);
}

EDIT_PARTS_ATRA *PartsAtra(int ground, int plot) {
    if (ground < 0 || ground >= 6 || plot < 0 || plot >= 24) {
        return nullptr;
    }
    if (ground == kTown && plot == kPlot) {
        if (Pack *pack = Ready()) {
            return &pack->atra;
        }
    }
    return &EditPartsData[ground].parts[plot];
}

void EnterTextures() {
    Pack *pack = Ready();
    if (pack == nullptr) {
        return;
    }
    TexManager.BeginEnterTextureBlock(kTextureBlock);
    for (Picture &picture : pack->pictures) {
        TexManager.EnterTextureEX(kTextureBlock, picture.name.data(), picture.rgba.data(), picture.width, picture.height, 4,
                                  nullptr, 0, 0, nullptr, nullptr, nullptr, picture.tex1, 0);
    }
    TexManager.EndEnterTextureBlock(kTextureBlock);
    for (Picture &picture : pack->pictures) {
        CTexture *entry = TexManager.GetTexture(picture.name.data(), kTextureBlock);
        std::fprintf(stderr, "ixion revgeo: picture %s %dx%d %s\n", picture.name.c_str(), picture.width, picture.height,
                     entry != nullptr && entry->name[0] != '\0' ? "entered" : "MISSING");
    }
}

void AfterLoad() {
    Pack *pack = Ready();
    if (pack == nullptr || NowEditMap != kTown) {
        return;
    }
    EDITPARTS_INFO *info = EditPartsInfo.GetPartsInfo(kPlot);
    if (info == nullptr) {
        return;
    }
    info->obtained = 1;
    std::fprintf(stderr, "ixion revgeo: plot %d obtained stock=%d placed=%d cells=%dx%d header=%s\n", kPlot, info->stock,
                 info->placed, info->width, info->height, info->header != nullptr ? "built" : "NONE");
    std::vector<SV_GRD_PART> saved;
    if (!ReadSidecar(saved)) {
        std::fprintf(stderr, "ixion revgeo: no sidecar placements\n");
        return;
    }
    int placed = 0;
    for (const SV_GRD_PART &record : saved) {
        // CEditGround::Load nudges the stored grid position into its cell before it places.
        int id = pEditGround->SetMapParts(record.part_id, record.pos_x + 1.0f, record.pos_y, record.pos_z + 1.0f, record.rot_y);
        std::fprintf(stderr, "ixion revgeo: sidecar placed id=%d pos=%.1f,%.1f,%.1f rot=%d slot=%d\n", record.part_id,
                     record.pos_x, record.pos_y, record.pos_z, record.rot_y, id);
        if (id >= 0) {
            placed++;
        }
    }
    std::fprintf(stderr, "ixion revgeo: sidecar loaded count=%zu placed=%d\n", saved.size(), placed);
}

void Trace() {
    static const bool enabled = [] {
        const char *value = std::getenv("DC_IXION_REVGEO_TRACE");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    static int frame = 0;
    static int last_placed = -1;
    static int last_mode = -1;
    frame++;
    if (!enabled || pEditGround == nullptr) {
        return;
    }
    const bool      active = Active(NowEditMap);
    EDITPARTS_INFO *info = active ? EditPartsInfo.GetPartsInfo(kPlot) : nullptr;
    int             placed = info != nullptr ? info->placed : -1;
    if (active && placed != last_placed) {
        last_placed = placed;
        std::fprintf(stderr, "ixion revgeo: trace frame=%d placed=%d stock=%d\n", frame, placed,
                     info != nullptr ? info->stock : -1);
        for (int i = 0; i < 128; i++) {
            CMapParts &object = pEditGround->parts[i];
            if (object.handle == kPlot) {
                float position[4];
                object.GetPosition(position);
                std::fprintf(stderr, "ixion revgeo: trace part slot=%d pos=%.1f,%.1f,%.1f rot_y=%d area=%d\n", i, position[0],
                             position[1], position[2], object.rot_y, object.area);
            }
        }
    }
    if (GameMode != last_mode) {
        last_mode = GameMode;
        std::fprintf(stderr, "ixion revgeo: trace frame=%d mode=%d\n", frame, GameMode);
    }
    if (GameMode == ED_MODE_WALK && Chara != nullptr && frame % 10 == 0) {
        float position[4];
        Chara->GetPosition(position);
        std::fprintf(stderr, "ixion revgeo: trace frame=%d walk pos=%.2f,%.2f,%.2f\n", frame, position[0], position[1],
                     position[2]);
    }
}

bool Captures(int town, int part_id) {
    return part_id == kPlot && Active(town);
}

bool CardPicture(const CTexture *sheet, const CRect_i_ &texel, CTexture *&texture, CRect_i_ &cell) {
    if (sheet == nullptr || texel.width != 0x54 || texel.height != 0x54 || !Active(kTown) ||
        MenuAtoraSel.map_no != kTown || std::strcmp(sheet->name, "sozai") != 0) {
        return false;
    }
    if (texel.x != (kCardCell / 6) * 0x54 + 0x104 || texel.y != (kCardCell + 6) % 6 * 0x54) {
        return false;
    }
    CTexture *own = TexManager.GetTexture(const_cast<char *>("ixrg_icon"), -1);
    if (own == nullptr || own->name[0] == '\0') {
        return false;
    }
    texture = own;
    cell = CRect_i_(0, 0, 0x54, 0x54);
    return true;
}

bool BlankName(int map_no, int element) {
    return map_no == kTown && element == kPlot && Active(kTown);
}

void WriteSidecar(int town, const std::vector<SV_GRD_PART> &placed) {
    if (!Active(town)) {
        return;
    }
    std::vector<unsigned char> bytes = EncodeSidecar(placed);
    fs::path                   path = SidecarPath();
    std::error_code            error;
    fs::create_directories(path.parent_path(), error);
    fs::path temporary = path;
    temporary += ".new";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            std::fprintf(stderr, "ixion revgeo: sidecar write failed\n");
            return;
        }
    }
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(path, error);
        fs::rename(temporary, path, error);
    }
    std::fprintf(stderr, "ixion revgeo: sidecar written count=%zu path=%s\n", placed.size(), path.string().c_str());
}

} // namespace ixion_revgeo

// ---------------------------------------------------------------------------------------------
// Overrides of the retail functions the sandbox part has to pass through. With the sandbox off each
// body is retail's.

namespace {
struct PtsHeader {
    int unk_00;
    int count;
    int unk_08[2];
};

struct PtsEntry {
    char name[16];
    int  offset;
    int  unk_14[7];
};
} // namespace

PORT_OVERRIDE u_int *SearchPTS(u_int *archive, char *name) {
    char *base_name = name;
    for (char *c = name; *c != 0; c++) {
        if (*c == '/') {
            base_name = c + 1;
        }
    }
    if (archive != nullptr) {
        PtsHeader *header = reinterpret_cast<PtsHeader *>(archive);
        PtsEntry  *entry = reinterpret_cast<PtsEntry *>(header + 1);
        for (int i = 0; i < header->count; i++, entry++) {
            if (strcmp(base_name, entry->name) == 0) {
                return reinterpret_cast<u_int *>(reinterpret_cast<char *>(header) + entry->offset);
            }
        }
    }
    // The sandbox part is not in the retail scene; its own one-entry scene answers for its name.
    if (u_int *overlay = ixion_revgeo::Scene()) {
        PtsHeader *header = reinterpret_cast<PtsHeader *>(overlay);
        PtsEntry  *entry = reinterpret_cast<PtsEntry *>(header + 1);
        for (int i = 0; i < header->count; i++, entry++) {
            if (strcmp(base_name, entry->name) == 0) {
                return reinterpret_cast<u_int *>(reinterpret_cast<char *>(header) + entry->offset);
            }
        }
    }
    return nullptr;
}

PORT_OVERRIDE EDIT_PARTS_ATRA *GetEditAtraPartsData(int ground, int number) {
    return ixion_revgeo::PartsAtra(ground, number);
}

// Retail's DrawMenu2DSprite; the sandbox part's card picture is the pack's own.
PORT_OVERRIDE void DrawMenu2DSprite(CTexture *texture, CRect_i_ screen, CRect_i_ texel, int alpha) {
    CTexture *own = nullptr;
    CRect_i_  cell;
    if (ixion_revgeo::CardPicture(texture, texel, own, cell)) {
        texture = own;
        texel = cell;
    }
    set2DSprite(GetVif1Packet(), texture, screen, texel, alpha);
}

// Retail's GetAtraMsgNo; the sandbox part's name is blank (its picture carries it).
PORT_OVERRIDE int GetAtraMsgNo(int map_no, int element) {
    int mes_no;

    if (element < 0 || element >= 100 || ixion_revgeo::BlankName(map_no, element)) {
        mes_no = 999;
    } else {
        EDIT_ELEMENT_ATRA *atra = GetEditAtraData(map_no, element);
        mes_no = atra->msg_no + (map_no * 200 + 1000);

        switch (map_no) {
            case TOWN_QUEENS:
                if (element == 1) {
                    int count = SaveData->GetGameIntFlag(1);

                    if (count > 0) {
                        mes_no += count + 0x1C;
                    }
                }

                break;
        }

        if (element >= 40) {
            mes_no += 40;
        }

        if (element >= 80) {
            mes_no += 40;
        }
    }

    return mes_no;
}

// Retail's CEditPartsInfo::Save; the sandbox plot's record stays out of the campaign save.
PORT_OVERRIDE void CEditPartsInfo::Save(int georama_no, CSaveData *save_data) {
    int              plot;
    SV_GEORAMA_DATA *georama = save_data->GetGrdData(georama_no);

    if (georama == NULL) {
        return;
    }

    georama->request_count = parts_max;

    for (plot = 0; plot < 24; plot++) {
        georama->request_complete[plot] = request[plot];

        if (plot == ixion_revgeo::kPlot && ixion_revgeo::Active(georama_no)) {
            continue;
        }

        SV_EDIT_PARTS_INFO *saved_part = save_data->GetEditPartsInfo(georama_no, plot);

        if (saved_part == NULL) {
            continue;
        }

        saved_part->obtained = parts[plot].obtained;
        saved_part->completion_flags = parts[plot].completion_flags;
        saved_part->placed = parts[plot].placed;
        saved_part->stock = parts[plot].stock;

        for (int element = 0; element < 6; element++) {
            saved_part->element_enabled[element] = parts[plot].elements[element].enabled;
        }
    }
}

// Retail's CEditGround::Save(int, CSaveData *); a part of the sandbox plot is recorded in the sidecar and
// never in the campaign save.
PORT_OVERRIDE void CEditGround::Save(int town, CSaveData *save) {
    sceVu0FVECTOR position;
    int           count;
    int           i;

    SV_GRD_PART *record = save->GetParts(town, &count);

    if (record == NULL) {
        return;
    }

    std::vector<SV_GRD_PART> sandbox;

    for (i = 0; i < 128; i++) {
        CMapParts *object = &parts[i];
        int        parts_id = parts[i].handle;

        if (parts_id < 0) {
            continue;
        }

        int j;
        int kind = object->subtype;

        switch (kind) {
            case MAP_PARTS_SUBTYPE_ROAD:
            case MAP_PARTS_SUBTYPE_RIVER:
            case MAP_PARTS_SUBTYPE_BRIDGE:
                for (j = 0; j < 24; j++) {
                    if (kind == plot_parts[j].subtype) {
                        parts_id = plot_parts[j].handle;
                        break;
                    }
                }

                break;
        }

        object->GetPosition(position);

        if (ixion_revgeo::Captures(town, parts_id)) {
            SV_GRD_PART entry = {};
            entry.part_id = static_cast<s16>(parts_id);
            entry.rot_y = static_cast<s16>(object->rot_y);
            entry.pos_x = position[0];
            entry.pos_y = position[1];
            entry.pos_z = position[2];
            sandbox.push_back(entry);
            continue;
        }

        record->part_id = parts_id;
        record->rot_y = object->rot_y;
        record->pos_x = position[0];
        record->pos_y = position[1];
        record->pos_z = position[2];
        record++;
    }

    record->part_id = -1;
    record->rot_y = -1;
    record->pos_x = 0.0f;
    record->pos_y = 0.0f;
    record->pos_z = 0.0f;

    if (ixion_revgeo::Active(town)) {
        std::fprintf(stderr, "ixion revgeo: ground save campaign_records=%d sandbox_records=%zu\n",
                     static_cast<int>(record - save->GetParts(town, &count)), sandbox.size());
    }

    ixion_revgeo::WriteSidecar(town, sandbox);
}
