#include "ridepod_visual.hpp"
#include "ridepod_combat.hpp"
#ifdef IXION_HAS_DC2_RIDEPOD
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Standard headers must precede the game's one-letter GS macros.
#include "ixion_dc1_ridepod.h"
#include "character.hpp"
#include "chararead.hpp"
#include "dataalloc.hpp"
#include "frame.hpp"
#include "framevu1.hpp"
#include "gameutil.hpp"
#include "mathutil.hpp"
#include "mdt.hpp"
#include "visualvu1.hpp"
#include "texture.hpp"
#include "texture_port.hpp"
#include "platform/clock.hpp"

namespace {
constexpr int kTextureBlock = 71;
constexpr size_t kArenaQuads = 131072; // private 2 MiB; no retail arena/reset ownership
struct Texture {
    std::string name;
    int width, height;
    u_long tex1;
    std::vector<unsigned char> rgba;
};
struct Part {
    std::vector<u_long128> storage = std::vector<u_long128>(kArenaQuads);
    std::vector<u_long128> pack;
    CDataAlloc2<1> arena;
    CCharacter character;
    Part() {
        arena.base = arena.buffer = reinterpret_cast<u_char *>(storage.data());
        arena.limit = static_cast<int>(storage.size());
        arena.used = 0;
    }
};
struct Rig {
    Part leg, mount, body, gun;
    std::vector<Texture> textures;
    float key_steps[3]{};
    ixdc2_robo_combat_input combat{};
    int attack = -1;
    float attack_frame = 0;
    bool gun_selected = false;
    bool loaded = false;
};
// Stable for the module's lifetime: DC1 visual records refer into these private
// arenas. Native scene arenas may clear without invalidating this rig.
std::unique_ptr<Rig> rig;
bool active = false, attempted = false;

// Donor mgInversMatrix's general affine inverse: DC1's SCE inverse assumes
// orthonormal matrices, whereas this private actor is scaled to DC1 units.
void SkinInverse(float out[4][4], const float m[4][4]) {
    const float *a=m[0],*b=m[1],*c=m[2],*d=m[3];
    float det=a[0]*b[1]*c[2]+c[0]*a[1]*b[2]+b[0]*c[1]*a[2]
             -a[0]*c[1]*b[2]-b[0]*a[1]*c[2]-c[0]*b[1]*a[2];
    const float *p[3]={b,c,a},*s[3]={c,a,b};
    for(int k=0;k<3;++k) {
        out[0][k]=(p[k][1]*s[k][2]-p[k][2]*s[k][1])/det;
        out[1][k]=(p[k][2]*s[k][0]-p[k][0]*s[k][2])/det;
        out[2][k]=(p[k][0]*s[k][1]-p[k][1]*s[k][0])/det;
    }
    for(int k=0;k<3;++k) {
        out[k][3]=0;
        out[3][k]=-(out[0][k]*d[0]+out[1][k]*d[1]+out[2][k]*d[2]);
    }
    out[3][3]=1;
}

// DC2 MotionProc2's weighted-skin formula, adapted only to private DC1 frames.
// No native DC1 animation hook is changed. Source inputs remain local vertices.
void ArmSkin(CCharacter &arm) {
    auto &motion=arm.motion_type;
    auto *frames=static_cast<CFrameVu1 *>(arm.frame);
    int cached=-1;
    static float accum[3000][3];
    sceVu0FMATRIX skin_inv,base_skin_inv,root_world;
    for(auto *list=motion.proc_list2;list;list=list->next) {
        if(!list->key_count)continue; // DC2's zero-key sentinels
        auto &skin=frames[list->frame];auto &bone=frames[list->target];
        auto *model=reinterpret_cast<MDT_HEADER *>(skin.GetVisual()->GetMDTDataAddress());
        auto *vertices=reinterpret_cast<float(*)[4]>(reinterpret_cast<unsigned char *>(model)+model->vertex_ofs);
        if(cached!=int(list->frame)) {
            cached=list->frame;
            std::memset(accum,0,sizeof(accum));
            sceVu0FMATRIX skin_world,base_skin;
            skin.GetLWMatrix(skin_world);arm.frame->GetLWMatrix(root_world);
            SkinInverse(skin_inv,skin_world);
            MulMatrix(base_skin,root_world,motion.base_matrices[list->frame]);
            SkinInverse(base_skin_inv,base_skin);
            skin.attr.remake_pending=1;
        }
        sceVu0FMATRIX bone_world,bone_base,bone_in_skin,bone_in_skin_inv,skin_bone,deform;
        bone.GetLWMatrix(bone_world);
        MulMatrix(bone_base,root_world,motion.base_matrices[list->target]);
        MulMatrix(bone_in_skin,base_skin_inv,bone_base);
        SkinInverse(bone_in_skin_inv,bone_in_skin);
        MulMatrix(skin_bone,skin_inv,bone_world);
        MulMatrix(deform,skin_bone,bone_in_skin_inv);
        for(unsigned i=0;i<list->key_count;++i) {
            unsigned vertex=list->keys[i].frame;
            if(vertex>=motion.frame_info[list->frame].vertex_count || vertex>=3000)continue;
            float moved[4];sceVu0ApplyMatrix(moved,deform,motion.frame_info[list->frame].base_vertices[vertex]);
            float weight=.01f*list->keys[i].value[0];
            for(int j=0;j<3;++j)vertices[vertex][j]=accum[vertex][j]+=moved[j]*weight;
            vertices[vertex][3]=1;
        }
    }
}

// DC2 MotionProc clamps the final key and uses its own quaternion convention.
// Evaluate the imported arm directly; DC1's animation API remains native elsewhere.
void ArmPose(CCharacter &arm, float time) {
    auto &motion=arm.motion_type;
    auto *frames=static_cast<CFrameVu1 *>(arm.frame);
    for(auto *list=motion.proc_list;list;list=list->next) {
        if(!list->key_count || list->type>2) continue;
        unsigned key=0;
        while(key+1<list->key_count && list->keys[key+1].frame<=time) ++key;
        unsigned next=std::min(key+1,list->key_count-1);
        float t=next==key ? 0 : (time-list->keys[key].frame)/(list->keys[next].frame-list->keys[key].frame);
        t=std::clamp(t,0.f,1.f);
        float value[4],a[4],b[4];
        std::memcpy(a,list->keys[key].value,16); std::memcpy(b,list->keys[next].value,16);
        auto &frame=frames[list->frame];
        if(list->type==0) {
            float dot=0;for(int j=0;j<4;++j) dot+=a[j]*b[j];
            if(dot<0){dot=-dot;for(float &v:b)v=-v;}
            float u=1-t,v=t;
            if(dot<.01f){u=0;v=1;}
            else if(1-dot>.01f){float angle=acosf(std::min(dot,1.f));float q=1/sinf(angle);u=q*sinf((1-t)*angle);v=q*sinf(t*angle);}
            for(int j=0;j<4;++j)value[j]=u*a[j]+v*b[j];
            float w=value[0],x=value[1],y=value[2],z=value[3];
            auto &m=frame.local;
            m[0][0]=1-2*(y*y+z*z); m[0][1]=2*(x*y-w*z); m[0][2]=2*(x*z+w*y);
            m[1][0]=2*(x*y+w*z); m[1][1]=1-2*(x*x+z*z); m[1][2]=2*(y*z-w*x);
            m[2][0]=2*(x*z-w*y); m[2][1]=2*(y*z+w*x); m[2][2]=1-2*(x*x+y*y);
        } else {
            for(int j=0;j<3;++j)value[j]=a[j]*(1-t)+b[j]*t;
            if(list->type==1) frame.SetScale(value[0],value[1],value[2]);
            else for(int j=0;j<3;++j)frame.local[3][j]=value[j];
        }
        frame.world_valid=false;
    }
    motion.state.time=time;
    sceVu0UnitMatrix(arm.frame->local);
    arm.frame->world_valid=false;
    ArmSkin(arm);
}

std::vector<unsigned char> Read(const std::filesystem::path &path, size_t max) {
    auto size = std::filesystem::file_size(path);
    if (size == 0 || size > max) throw std::runtime_error("private file size out of bounds");
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("private file read failed");
    return bytes;
}

void Textures() {
    bool missing = false;
    for (auto &texture : rig->textures) {
        auto *entry = TexManager.GetTexture(texture.name.data(), -1);
        missing |= !entry || !PortKeyLive(static_cast<unsigned>(entry->tex0 & 0x3fff));
    }
    if (!missing) return;
    TexManager.BeginEnterTextureBlock(kTextureBlock);
    for (auto &texture : rig->textures) {
        TexManager.EnterTextureEX(kTextureBlock, texture.name.data(), texture.rgba.data(),
            texture.width, texture.height, 4, nullptr, 0, 0, nullptr, nullptr, nullptr, texture.tex1, 0);
        auto *entry = TexManager.GetTexture(texture.name.data(), kTextureBlock);
        if (!entry || !entry->name[0]) throw std::runtime_error("private texture registration failed");
    }
    TexManager.EndEnterTextureBlock(kTextureBlock);
}

void LoadPart(Part &part, const std::filesystem::path &path) {
    auto bytes = Read(path, 4*1024*1024);
    part.pack.resize((bytes.size()+15)/16);
    std::memcpy(part.pack.data(), bytes.data(), bytes.size());
    char cfg[] = "info.cfg";
    ReadInfo(&part.character, reinterpret_cast<u_int *>(part.pack.data()), cfg,
        &part.arena, &part.arena, &part.arena, kTextureBlock, nullptr, 0, 1);
    if (!part.character.frame) throw std::runtime_error("private model not accepted by DC1 ReadInfo");
    part.character.SetPosition(0,0,0);
    part.character.SetRotation(0,0,0);
    part.character.SetScale(1,1,1);
    // DC2's actor transform replaces the imported root's authored matrix;
    // DC1 adds SRT to it. Start the private actor root at identity.
    sceVu0UnitMatrix(part.character.frame->local);
    part.character.frame->world_valid=false;
    part.character.FootSoundEnable(0);
    part.character.EventEnable(0);
}

void Load() {
    attempted = true;
    const char *directory = std::getenv("DC_IXION_RIDEPOD_PACK");
    if (!directory || !*directory) throw std::runtime_error("DC_IXION_RIDEPOD_PACK is required");
    std::filesystem::path root(directory);
    auto bytes = Read(root / "ridepod.json", 65536);
    auto manifest = nlohmann::json::parse(bytes.begin(), bytes.end());
    if (manifest.at("format") != "ixion-ridepod-dc1" || manifest.at("version") != 2 ||
        std::fabs(manifest.at("world_scale").get<float>() - IXDC2_ROBO_DC1_SCALE) > 1e-6f ||
        manifest.at("donor_logic_hz") != IXDC2_ROBO_LOGIC_HZ || manifest.at("textures").size() != 5)
        throw std::runtime_error("private pack contract mismatch");
    rig = std::make_unique<Rig>();
    auto &weapons = manifest.at("weapons");
    rig->combat.arm_attack = weapons.at("arm").at("attack");
    rig->combat.arm_whp = weapons.at("arm").at("durability");
    rig->combat.gun_attack = weapons.at("gun").at("attack");
    rig->combat.gun_whp = weapons.at("gun").at("durability");
    for (float v : {rig->combat.arm_attack,rig->combat.gun_attack,rig->combat.arm_whp,rig->combat.gun_whp})
        if (!std::isfinite(v) || v <= 0 || v > 10000) throw std::runtime_error("invalid weapon stat");
    auto &punch = manifest.at("provenance").at("arm01").at("keys");
    for (int i=0;i<3;++i) {
        auto &k = punch.at(i+3);
        rig->combat.punch[i] = {k.at("start"),k.at("end"),k.at("donor_step")};
    }
    if (manifest.at("keys").size() != 3) throw std::runtime_error("private motion contract mismatch");
    for (int i = 0; i < 3; ++i) {
        rig->key_steps[i] = manifest.at("keys").at(i).at("donor_step");
        if (!std::isfinite(rig->key_steps[i]) || rig->key_steps[i] <= 0 || rig->key_steps[i] > 10)
            throw std::runtime_error("private motion step invalid");
    }
    for (auto &entry : manifest.at("textures")) {
        std::string name = entry.at("name"), file = entry.at("file");
        int w = entry.at("width"), h = entry.at("height");
        if (!name.starts_with("ixrp_") || name.size() > 31 ||
            file != name+".rgba" || file.find_first_of("/\\:") != std::string::npos ||
            w <= 0 || h <= 0 || w > 2048 || h > 2048)
            throw std::runtime_error("private texture contract mismatch");
        auto rgba = Read(root/file, static_cast<size_t>(w)*h*4);
        if (rgba.size() != static_cast<size_t>(w)*h*4) throw std::runtime_error("private texture truncated");
        rig->textures.push_back({name,w,h,entry.at("tex1").get<u_long>(),std::move(rgba)});
    }
    Textures(); // material lookup happens while DC1 builds the visuals
    LoadPart(rig->leg, root/"leg01.chr");
    LoadPart(rig->mount, root/"arm01.chr");
    auto &arm_motion=rig->mount.character.motion_type;
    for(auto *list=arm_motion.proc_list2;list;list=list->next) {
        auto *frames=static_cast<CFrameVu1 *>(rig->mount.character.frame);
        auto *visual=frames[list->frame].GetVisual();
        if(!visual)throw std::runtime_error("private skin target has no visual");
        auto *model=reinterpret_cast<MDT_HEADER *>(visual->GetMDTDataAddress());
        auto *vertices=reinterpret_cast<unsigned char *>(model)+model->vertex_ofs;
        std::memcpy(arm_motion.frame_info[list->frame].base_vertices,vertices,model->vertex_num*16);
    }
    LoadPart(rig->gun, root/"arm04.chr");
    LoadPart(rig->body, root/"body01.chr");
    char joint_name[] = "body1", spine_name[] = "spine";
    auto *joint = rig->leg.character.frame->SearchFrame(joint_name);
    auto *spine = rig->mount.character.frame->SearchFrame(spine_name);
    if (!joint || !spine) throw std::runtime_error("native body1/spine assembly joint absent");
    rig->mount.character.frame->SetReference(joint);
    rig->gun.character.frame->SetReference(joint);
    rig->body.character.frame->SetReference(spine);
    rig->leg.character.SetScale(IXDC2_ROBO_DC1_SCALE,IXDC2_ROBO_DC1_SCALE,IXDC2_ROBO_DC1_SCALE);
    rig->loaded = true;
    std::fprintf(stderr,"ixdc2 visual loaded leg01/body01 textured=5 mount=arm01-skin gun=arm04 headlight=omitted "
        "seat=unverified rider=hidden scale=%.9f logic_hz=25 arenas=(%d,%d,%d)/%zu\n",
        IXDC2_ROBO_DC1_SCALE,rig->leg.arena.used,rig->mount.arena.used,rig->body.arena.used,kArenaQuads);
}
}

bool RidepodVisualCombatInput(ixdc2_robo_combat_input &input) {
    if (!RidepodVisualReady()) return false;
    input = rig->combat;
    return true;
}
void RidepodVisualCombatPose(int attack, float frame, bool gun) {
    if (!RidepodVisualReady()) return;
    rig->attack = attack; rig->attack_frame = frame; rig->gun_selected = gun;
}
bool RidepodVisualMuzzle(int side, float position[4]) {
    if (!RidepodVisualReady()) return false;
    auto &arm = rig->gun_selected ? rig->gun.character : rig->mount.character;
    char names[2][8] = {"dcol00","dcol01"};
    auto *joint = arm.frame->SearchFrame(names[side&1]);
    if (!joint) return false;
    float origin[4] = {0,0,0,1};
    joint->GetWorldPosition(position,origin);
    return std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]);
}
void RidepodVisualAim(const float target[4], int side) {
    if(!RidepodVisualReady())return;
    auto &arm=rig->gun_selected ? rig->gun.character : rig->mount.character;
    float pivot[4],point[4],origin[4]={0,0,0,1};
    arm.frame->GetWorldPosition(pivot,origin);
    if(!RidepodVisualMuzzle(side,point))return;
    float angle=atan2f(target[0]-pivot[0],target[2]-pivot[2])-
                atan2f(point[0]-pivot[0],point[2]-pivot[2]);
    arm.SetRotation(0,angle,0);
}

void RidepodVisualActivate(bool on) {
    if (!on) RidepodCombatRetireScene();
    active = on;
}
bool RidepodVisualReady() { return active && rig && rig->loaded; }
float RidepodVisualLegFrame() {
    return RidepodVisualReady() ? rig->leg.character.motion_type.state.time : -1.0f;
}
void RidepodVisualStep(const float position[4], float yaw, int motion) {
    if (!active) return;
    try {
        if (!attempted) Load();
        if (!RidepodVisualReady()) return;
        Textures();
        auto &leg = rig->leg.character;
        leg.SetPosition(position[0],position[1],position[2]);
        leg.SetRotation(0,yaw,0);
        int key = motion == IXDC2_ROBO_RUN ? 2 : motion == IXDC2_ROBO_WALK ? 1 : 0;
        float step = rig->key_steps[key] * 1.2f *
            IXDC2_ROBO_LOGIC_HZ / static_cast<float>(ClockTickRate());
        leg.SetMotion(key,0,step);
        leg.Step();
        sceVu0UnitMatrix(leg.frame->local);
        leg.frame->world_valid=false;
        auto &arm = rig->gun_selected ? rig->gun.character : rig->mount.character;
        if (!rig->gun_selected && rig->attack >= 0) {
            ArmPose(arm,rig->attack_frame);
        } else {
            int arm_key = rig->gun_selected ? 3 : key;
            auto k=arm.motion_type.motion_info[arm_key];
            float time=arm.motion_type.state.time;
            if(time<k.start || time>=k.end)time=k.start;
            float arm_step = rig->gun_selected ? .75f*1.2f*25/ClockTickRate() : step;
            ArmPose(arm,time);
            arm.motion_type.state.time=time+arm_step;
        }
        arm.frame->SetPosition(0,0,0);
        arm.SetRotation(0,0,0);
        sceVu0UnitMatrix(arm.frame->local);
        arm.frame->world_valid=false;
        char spine_name[] = "spine";
        rig->body.character.frame->SetReference(arm.frame->SearchFrame(spine_name));
    } catch (const std::exception &error) {
        if (rig) rig->loaded = false;
        std::fprintf(stderr,"ixdc2 visual refused: %s\n",error.what());
    }
}
bool RidepodVisualDraw() {
    if (!RidepodVisualReady()) return false;
    rig->leg.character.Draw();
    auto &arm = rig->gun_selected ? rig->gun.character : rig->mount.character;
    arm.frame->SetPosition(0,0,0);
    arm.Draw();
    rig->body.character.Draw();
    return true;
}

#endif
