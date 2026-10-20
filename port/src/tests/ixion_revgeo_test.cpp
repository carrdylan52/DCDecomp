#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "../eparts_port.hpp"
#include "../ixion_revgeo.hpp"
#include "collision.hpp"
#include "dataalloc.hpp"
#include "dataset.hpp"
#include "dispctrl.hpp"
#include "editpartsdata.hpp"
#include "editpartsinfo.hpp"
#include "framevu1.hpp"
#include "mds.hpp"

namespace {

SV_GRD_PART Record(int part, int rot, float x, float y, float z) {
    SV_GRD_PART record{};
    record.part_id = static_cast<s16>(part);
    record.rot_y = static_cast<s16>(rot);
    record.pos_x = x;
    record.pos_y = y;
    record.pos_z = z;
    return record;
}

} // namespace

TEST(IxionRevgeo, SidecarRoundTripsAndRefusesEverythingElse) {
    std::vector<SV_GRD_PART> placed = {Record(17, 1, -350.1f, 50.0f, -350.1f), Record(17, 3, 250.0f, 0.0f, 150.5f)};
    auto                     bytes = ixion_revgeo::EncodeSidecar(placed);
    std::vector<SV_GRD_PART> out;
    std::string              why;
    ASSERT_TRUE(ixion_revgeo::DecodeSidecar(bytes.data(), bytes.size(), out, why)) << why;
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(std::memcmp(out.data(), placed.data(), 2 * sizeof(SV_GRD_PART)), 0);

    // The empty sidecar is how "nothing placed" is saved.
    auto empty = ixion_revgeo::EncodeSidecar({});
    ASSERT_TRUE(ixion_revgeo::DecodeSidecar(empty.data(), empty.size(), out, why)) << why;
    EXPECT_TRUE(out.empty());

    for (size_t at : {size_t{0}, size_t{9}, size_t{13}, size_t{20}, bytes.size() - 1}) {
        auto damaged = bytes;
        damaged[at] ^= 0x40;
        EXPECT_FALSE(ixion_revgeo::DecodeSidecar(damaged.data(), damaged.size(), out, why)) << "byte " << at;
        EXPECT_TRUE(out.empty());
    }
    EXPECT_FALSE(ixion_revgeo::DecodeSidecar(bytes.data(), bytes.size() - 1, out, why));
    EXPECT_FALSE(ixion_revgeo::DecodeSidecar(bytes.data(), 8, out, why));
    EXPECT_FALSE(ixion_revgeo::DecodeSidecar(nullptr, 0, out, why));

    // A well-formed file that names another plot, an impossible turn or a non-finite place is refused.
    for (const SV_GRD_PART &bad : {Record(13, 0, 0, 0, 0), Record(17, 4, 0, 0, 0), Record(17, 0, NAN, 0, 0),
                                   Record(17, 0, 0, INFINITY, 0)}) {
        auto forged = ixion_revgeo::EncodeSidecar({bad});
        EXPECT_FALSE(ixion_revgeo::DecodeSidecar(forged.data(), forged.size(), out, why)) << why;
    }
    // And the good one still decodes after all that.
    EXPECT_TRUE(ixion_revgeo::DecodeSidecar(bytes.data(), bytes.size(), out, why)) << why;
    EXPECT_EQ(out.size(), 2u);
}

TEST(IxionRevgeo, NothingIsActiveWithoutTheOptInAndNothingLeaksIntoRetailTables) {
    // The sandbox is off unless DC_IXION_REVGEO=1 names a pack; retail's attribute record for the plot is
    // the zero-filled one and its part is never captured out of the campaign save.
    ASSERT_EQ(std::getenv("DC_IXION_REVGEO"), nullptr);
    EXPECT_FALSE(ixion_revgeo::Active(0));
    EXPECT_EQ(ixion_revgeo::Scene(), nullptr);
    EXPECT_FALSE(ixion_revgeo::Captures(0, 17));
    EXPECT_FALSE(ixion_revgeo::BlankName(0, 17));
    EDIT_PARTS_ATRA *plot = ixion_revgeo::PartsAtra(0, 17);
    ASSERT_NE(plot, nullptr);
    EXPECT_EQ(plot->max, 0);
    EXPECT_EQ(plot->kind, 0);
    EXPECT_EQ(ixion_revgeo::PartsAtra(0, 24), nullptr);
    EXPECT_EQ(ixion_revgeo::PartsAtra(6, 0), nullptr);
}

// With IXION_REVGEO_TEST_PACK set to a converted pack: DC1's own loaders accept it.
TEST(IxionRevgeo, ConvertedPackLoadsThroughDc1Loaders) {
    const char *directory = std::getenv("IXION_REVGEO_TEST_PACK");
    if (directory == nullptr) {
        GTEST_SKIP() << "no converted pack";
    }
    std::ifstream stream(std::filesystem::path(directory) / "revgeo.scn", std::ios::binary);
    ASSERT_TRUE(stream.good());
    std::vector<char>      bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::vector<u_long128> scene((bytes.size() + 15) / 16);
    std::memcpy(scene.data(), bytes.data(), bytes.size());
    u_int *scn = reinterpret_cast<u_int *>(scene.data());

    char   name[] = "ixrg_shed";
    u_int *entry = SearchPTS(scn, name);
    ASSERT_NE(entry, nullptr);
    char missing[] = "e01h02";
    EXPECT_EQ(SearchPTS(scn, missing), nullptr);
    char path_name[] = "gedit/e01/ixrg_shed";
    EXPECT_EQ(SearchPTS(scn, path_name), entry);

    auto *record = reinterpret_cast<EPARTS_ARCHIVE *>(entry);
    ASSERT_GT(record->lod0_size, 0);
    ASSERT_GT(record->collision_size, 0);
    EXPECT_EQ(record->lod1_size, 0);
    EXPECT_EQ(record->shadow_size, 0);
    EXPECT_EQ(record->camera_size, 0);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(entry) % 16, 0u);
    EXPECT_EQ(record->lod0_offset % 16, 0);
    EXPECT_EQ(record->collision_offset % 16, 0);

    // The part definition: a 1 x 1 footprint, every cell occupied, no elements, no function records.
    const char      *definition = reinterpret_cast<const char *>(record) + record->info_offset;
    EPartsDiscHeader head = EPartsReadHeader(definition);
    EXPECT_EQ(head.header_size, 0x78);
    EXPECT_GE(head.width, 1);
    EXPECT_GE(head.height, 1);
    EXPECT_LE(head.width * head.height, 32);
    EXPECT_EQ(head.func_count, 0);
    for (int i = 0; i < 6; i++) {
        EXPECT_EQ(head.element_id[i], -1);
        EXPECT_EQ(head.element_name[i], 0u);
    }
    for (int cell = 0; cell < head.width * head.height; cell++) {
        EXPECT_EQ(static_cast<unsigned char>(definition[head.cell + cell]), 0x80);
    }
    std::vector<u_long128> storage(4096);
    CDataAlloc2<1>         arena;
    arena.base = arena.buffer = reinterpret_cast<u_char *>(storage.data());
    arena.limit = static_cast<int>(storage.size());
    arena.used = 0;
    EPARTS_INFO_HEADER *built = EPartsLoad(definition, &arena);
    ASSERT_NE(built, nullptr);
    EXPECT_EQ(built->width, head.width);
    EXPECT_EQ(built->height, head.height);
    EXPECT_EQ(built->cell[0], 0x80);

    // Collision: retail's own loader builds the triangles and its own segment query meets the wall.
    std::vector<u_long128> collision_storage(16384);
    CDataAlloc2<1>         collision_arena;
    collision_arena.base = collision_arena.buffer = reinterpret_cast<u_char *>(collision_storage.data());
    collision_arena.limit = static_cast<int>(collision_storage.size());
    collision_arena.used = 0;
    u_int     *collision_file = reinterpret_cast<u_int *>(reinterpret_cast<char *>(record) + record->collision_offset);
    CFrameVu1 *frames = LoadCollisionFile(collision_file, &collision_arena);
    ASSERT_NE(frames, nullptr);
    auto *mdt = dynamic_cast<CCollisionMDT *>(frames[0].collision);
    ASSERT_NE(mdt, nullptr);
    EXPECT_EQ(mdt->mesh_count, 24);
    EXPECT_NEAR(mdt->max[0] - mdt->min[0], 61.4f, 1.0f); // the Shed's width at DC1 scale
    EXPECT_NEAR(mdt->max[1], 90.0f, 1.0f);
    EXPECT_NEAR(mdt->min[1], 0.15f, 0.5f);
    for (int i = 0; i < mdt->mesh_count; i++) {
        const CCPolyBox &box = mdt->mesh[i];
        EXPECT_TRUE(std::isfinite(box.poly.normal[0]) && std::isfinite(box.poly.normal[1]) && std::isfinite(box.poly.normal[2]));
        EXPECT_EQ(box.poly.attr.area_kind, 0);
        EXPECT_EQ(box.poly.attr.ignore_mask, 0);
    }
    // Near the foot, where the wall is widest (the volume tapers up to the dome: at 30 units it is 5 inside).
    float from[4] = {-200.0f, 3.0f, 0.0f, 1.0f};
    float to[4] = {0.0f, 3.0f, 0.0f, 1.0f};
    float hit[4] = {};
    ASSERT_NE(mdt->Intersection(from, to, hit), 0);
    EXPECT_NEAR(hit[0], mdt->min[0], 2.5f);
}
