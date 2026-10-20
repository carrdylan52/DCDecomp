#pragma once

#include "common.h"

#include <cstddef>
#include <string>
#include <vector>

#include "rect.hpp"
#include "savedata.hpp"
#include "texture.hpp"

// Reverse Georama (Ixion): one Dark Chronicle building offered by Dark Cloud's own Georama editor in
// Norune. Opt-in with DC_IXION_REVGEO=1 and DC_IXION_REVGEO_PACK=<private pack directory>; with
// either unset every function below does nothing and the editor is retail's. The pack is a private
// DC1 scene (one EPARTS_ARCHIVE), the part's RGBA pictures and a manifest, written by Ixion's
// tools/revgeo_converter.py. The part takes Norune's unused plot 17.

struct EDIT_MAP_INFO;
struct EDIT_PARTS_ATRA;

namespace ixion_revgeo {

constexpr int kTown = 0;
constexpr int kPlot = 17;

// True once the pack has loaded and the editor is Norune's.
bool Active(int town);

// The pack's one-entry scene, or null.
u_int *Scene();

// After LoadEditMapData: gives plot 17 its model name so LoadObjectParts builds it.
void OnMapData(EDIT_MAP_INFO *info, int town);

// Retail's attribute record for (ground, plot), or the sandbox plot's.
EDIT_PARTS_ATRA *PartsAtra(int ground, int plot);

// Enters the part's pictures into the texture manager. Called inside LoadTexture, while the
// manager's buffer is still open.
void EnterTextures();

// After the campaign save has loaded: obtains plot 17 and places what the sidecar holds.
void AfterLoad();

// True for a placed part that belongs in the sidecar and not in the campaign save.
bool Captures(int town, int part_id);

// With DC_IXION_REVGEO_TRACE=1: logs Toan's position every 10 frames while he walks and the sandbox
// part's placements whenever their count changes. Evidence only; it changes nothing.
void Trace();

// The part-card picture: true, with the pack's own picture and cell, when the part-picture sheet's
// cell for the sandbox plot is being drawn on Norune's board.
bool CardPicture(const CTexture *sheet, const CRect_i_ &texel, CTexture *&texture, CRect_i_ &cell);

// True where GetAtraMsgNo's name for (map, plot) is the sandbox part's, which has none.
bool BlankName(int map_no, int element);

// The sidecar's bytes: marker, version, count, SV_GRD_PART records, FNV-1a checksum. Decoding refuses
// anything that is not exactly what Encode writes (any part but the sandbox plot, a count over 64, a
// rotation outside -1..3, a non-finite position, a wrong checksum or length) and leaves `out` empty.
std::vector<unsigned char> EncodeSidecar(const std::vector<SV_GRD_PART> &placed);
bool                       DecodeSidecar(const unsigned char *data, size_t size, std::vector<SV_GRD_PART> &out, std::string &why);

// Writes the sidecar (an empty one when nothing is placed); called by the ground's save.
void WriteSidecar(int town, const std::vector<SV_GRD_PART> &placed);

} // namespace ixion_revgeo
