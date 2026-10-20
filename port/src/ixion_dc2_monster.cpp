// DC2 monster natives adapted to DC1 ownership. Source citations in monster-natives.md.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
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
#include "ixion_dc2_monster.hpp"
#include "character.hpp"
#include "chararead.hpp"
#include "dataalloc.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "frame.hpp"
#include "gameutil.hpp"
#include "gameloop.hpp"
#include "monstorunit.hpp"
#include "mathutil.hpp"
#include "texture.hpp"
#include "texture_port.hpp"
#include "platform/clock.hpp"

namespace ixdc2::dc1mon {
namespace {
// DBC donor reservation: 70 is retail character effects, 71 is Ridepod.
// Never take an occupied block from another owner, including after a scene reset.
constexpr int TextureBlock=69;
constexpr char TextureOwner[]="ixion_vanguard";
static_assert(TextureBlock>=0 && TextureBlock<72);
struct Key { std::string name; int start,end; float step; };
struct Texture { std::string name; int w,h; u_long tex1; std::vector<unsigned char> rgba; };
struct Pack {
    std::vector<u_long128> arena_bytes=std::vector<u_long128>(131072), model, script;
    CDataAlloc2<1> arena;
    CCharacter character;
    std::vector<Key> keys;
    std::vector<Texture> textures;
    nlohmann::json collisions;
    Pack(){arena.base=arena.buffer=reinterpret_cast<u_char*>(arena_bytes.data());arena.limit=131072;arena.used=0;}
};
std::unique_ptr<Pack> pack;
struct State {
    CMonstorUnit *owner=nullptr; int slot=-1;
    RS_PROG_HEADER *script=nullptr;
    unsigned mask=0; int status=0,bit51=0,blow_ticks=0;
    double cadence=0;
    float next_yaw=0,rot_speed=0,move_speed=0,arrival=0,blow_speed=0,blow_decel=0;
    float motion_step=0,gekirin=10;
    float destination[4]{},hp_before=0; int player_before=0;
    bool dead=false,paid=false,attack_started=false;
    int main_addr=-1;
    std::array<bool,256> logged{};
};
State state;
bool consumed=false;
State &S(){return state;}
MONSTOR &M(){return state.owner->monster[state.slot];}
CCharacter &C(){return state.owner->chara[state.slot][0];}
int I(const RS_STACKDATA &v){return v.type==RS_FLOAT?int(v.f):v.i;}
float F(const RS_STACKDATA &v){return v.type==RS_INT?float(v.i):v.f;}
void Put(RS_STACKDATA *v,int x){if(v->type==RS_PTR && v->p){v->p->type=RS_INT;v->p->i=x;}}
void Put(RS_STACKDATA *v,float x){if(v->type==RS_PTR && v->p){v->p->type=RS_FLOAT;v->p->f=x;}}
float HostHz(){float hz=ClockTickRate();return hz>0?hz:60;}
float Rate(){return 25.0f/HostHz();}
std::vector<unsigned char> Read(const std::filesystem::path &p,size_t max){
    auto n=std::filesystem::file_size(p);if(!n||n>max)throw std::runtime_error("file extent");
    std::vector<unsigned char>b(n);std::ifstream f(p,std::ios::binary);
    if(!f.read(reinterpret_cast<char*>(b.data()),n))throw std::runtime_error("file read");return b;
}
void Textures(){
    auto &block=TexManager.blocks[TextureBlock];
    if((block.name[0]&&std::strcmp(block.name,TextureOwner))||
       (!block.name[0]&&(block.buffer||block.buffer_end||block.loaded||block.extend)))
        throw std::runtime_error("Vanguard texture block occupied");
    for(auto &t:TexManager.textures)if(t.block==TextureBlock&&t.name[0]&&
        std::none_of(pack->textures.begin(),pack->textures.end(),[&](const Texture &own){return own.name==t.name;}))
        throw std::runtime_error("Vanguard texture block has foreign texture");
    bool missing=false;
    for(auto &t:pack->textures){auto *e=TexManager.GetTexture(t.name.data(),TextureBlock);missing|=!e||!PortKeyLive(unsigned(e->tex0&0x3fff));}
    if(!missing)return;
    TexManager.DeleteTextureBlock(TextureBlock);
    std::strcpy(block.name,TextureOwner);
    TexManager.BeginEnterTextureBlock(TextureBlock);
    for(auto &t:pack->textures)TexManager.EnterTextureEX(TextureBlock,t.name.data(),t.rgba.data(),t.w,t.h,4,nullptr,0,0,nullptr,nullptr,nullptr,t.tex1,0);
    TexManager.EndEnterTextureBlock(TextureBlock);
}
void Load(){
    if(pack){Textures();return;}
    const char *dir=std::getenv("DC_IXION_DC2_MONSTER_PACK");if(!dir)throw std::runtime_error("DC_IXION_DC2_MONSTER_PACK required");
    std::filesystem::path root(dir);auto b=Read(root/"monster.json",65536);auto j=nlohmann::json::parse(b.begin(),b.end());
    if(j.at("format")!="ixion-dc2-monster-dc1"||j.at("version")!=1||j.at("donor")!="e202a"||
        std::fabs(j.at("world_scale").get<float>()-Scale)>1e-6f||j.at("script").at("globals_referenced")!=false)
        throw std::runtime_error("pack contract");
    pack=std::make_unique<Pack>();
    for(auto &k:j.at("keys")){
        std::string hex=k.at("sjis_hex"),name;for(size_t n=0;n<hex.size();n+=2)name+=char(std::stoi(hex.substr(n,2),nullptr,16));
        pack->keys.push_back({name,k.at("start"),k.at("end"),k.at("donor_step")});
    }
    if(pack->keys.size()!=12)throw std::runtime_error("key contract");
    pack->collisions=j.at("collision_commands");
    for(auto &t:j.at("textures")){
        std::string name=t.at("name"),file=t.at("file");int w=t.at("width"),h=t.at("height");
        if(!name.starts_with("ixvg_")||name.size()>31||file!=name+".rgba"||file.find_first_of("/\\:")!=std::string::npos||w<=0||h<=0||w>2048||h>2048)
            throw std::runtime_error("texture contract");
        auto pixels=Read(root/file,size_t(w)*h*4);if(pixels.size()!=size_t(w)*h*4)throw std::runtime_error("texture extent");
        pack->textures.push_back({name,w,h,t.at("tex1").get<u_long>(),std::move(pixels)});
    }
    Textures();
    b=Read(root/"e202a.chr",4*1024*1024);pack->model.resize((b.size()+15)/16);std::memcpy(pack->model.data(),b.data(),b.size());
    char cfg[]="info.cfg";ReadInfo(&pack->character,reinterpret_cast<u_int*>(pack->model.data()),cfg,
        &pack->arena,&pack->arena,&pack->arena,TextureBlock,nullptr,0,1);
    if(!pack->character.frame)throw std::runtime_error("model rejected");
    sceVu0UnitMatrix(pack->character.frame->local);pack->character.frame->world_valid=false;
    pack->character.SetScale(Scale,Scale,Scale);pack->character.FootSoundEnable(0);pack->character.EventEnable(0);
    b=Read(root/"e202a.stb",1024*1024);if(std::memcmp(b.data(),"SB2",4))throw std::runtime_error("script magic");
    pack->script.resize((b.size()+15)/16);std::memcpy(pack->script.data(),b.data(),b.size());
    std::fprintf(stderr,"ixdc2 monster loaded donor=e202a textures=%zu keys=%zu script_bytes=%zu scale=%.9f\n",pack->textures.size(),pack->keys.size(),b.size(),Scale);
}
int KeyNo(const char *name){if(!pack||!name)return -1;for(size_t i=0;i<pack->keys.size();++i)if(pack->keys[i].name==name)return int(i);return -1;}
float Dist(){float p[4];C().GetPosition(p);float d=0;for(int i=0;i<3;++i)d+=(CharaMain.pos[i]-p[i])*(CharaMain.pos[i]-p[i]);return std::sqrt(d)/Scale;}
void Vec(RS_STACKDATA *v,const float *p){for(int i=0;i<3;++i)Put(v+i,p[i]/Scale);}
void Damage(RS_STACKDATA *v,int n){
    int key=KeyNo(v[2].s),index=I(v[5]);if(key<0||index<0)return;
    std::string frame;
    for(auto &cmd:pack->collisions){auto &a=cmd[1];if(a[0]==3&&size_t(1+2*index)<a.size())frame=a[1+2*index].get<std::string>();}
    if(frame.empty())return;
    auto *joint=C().frame->SearchFrame(frame.data());if(!joint)return;
    auto &e=state.owner->effect2[state.slot];int slot=-1;
    for(int i=0;i<16;++i)if(!e.active[i]){slot=i;break;}if(slot<0)return;
    auto &k=pack->keys[key];e.active[slot]=1;e.frame[slot]=joint;e.radius[slot]=2*F(v[1])*Scale;
    e.motion_start[slot]=k.start+(k.end-k.start)*F(v[3]);e.motion_end[slot]=k.start+(k.end-k.start)*F(v[4]);
    // DC2 monster.cpp:2830 overwrites the primitive's damage with the row attack.
    e.damage[slot]=13;e.kind[slot]=std::strcmp(v[0].s,"\x8e\xe3\x8d\x55\x8c\x82")==0?2:3;e.flags[slot]=0;e.angle[slot]=0;
    std::fprintf(stderr,"ixdc2 dmg donor=e202a slot=%d index=%d native=113 motion=%d window=%.3f..%.3f radius=%.3f raw=%d segment=%d\n",state.slot,index,key,e.motion_start[slot],e.motion_end[slot],e.radius[slot],e.damage[slot],n==7);
}
int Dispatch(int id,RS_STACKDATA *v,int n){
    if(!state.owner||state.slot<0)return 0;
    if(!S().logged[id]){S().logged[id]=true;std::fprintf(stderr,"ixdc2 native donor=e202a slot=%d id=%d argc=%d\n",S().slot,id,n);}
    float p[4]{},r[4]{};
    switch(id){
    case 35: if(n!=2)return 0;Put(v+1,int(float(I(v[0]))*float(rand())/2147483648.0f));break;
    case 40: Put(v,S().slot);break;
    case 42: Put(v,24+S().slot);break;
    case 43: Put(v,-1);break; // DC1 Toan has no monster transformation.
    case 53: if(n!=2)return 0;Put(v+1,I(v[0])==51?S().bit51:0);break;
    case 54: if(n!=2)return 0;if(I(v[0])==51)S().bit51=I(v[1]);break;
    case 55: Put(v,0);break; // native Toan melee attack type; event 2510 is logged only.
    case 58: if(n!=2)return 0;Put(v+1,int((M().stop_timer>0?8:0)&I(v[0])));break;
    case 75: if(n!=3)return 0;C().GetPosition(p);Vec(v,p);break;
    case 77: if(n!=3)return 0;C().GetRotation(r);for(int i=0;i<3;++i)Put(v+i,r[i]);break;
    case 78: if(n!=3)return 0;C().SetRotation(F(v[0]),F(v[1]),F(v[2]));S().rot_speed=0;break;
    case 79: if(n!=2)return 0;S().next_yaw=F(v[0]);S().rot_speed=F(v[1]);break;
    case 80: if(n!=5)return 0;for(int i=0;i<3;++i)S().destination[i]=F(v[i])*Scale;S().destination[3]=1;S().move_speed=F(v[3])*Scale;S().arrival=F(v[4])*Scale;break;
    case 81: if(n!=1)return 0;C().GetPosition(p);Put(v,int(DistVector(p,S().destination)<S().arrival));break;
    case 82: S().move_speed=0;M().movement_speed=0;break;
    case 83: if(n<3||n>4)return 0;Vec(v,CharaMain.pos);if(n==4)Put(v+3,Dist());break;
    case 84: Put(v,Dist());break;
    case 85: C().GetPosition(p);Put(v,std::atan2(CharaMain.pos[0]-p[0],CharaMain.pos[2]-p[2]));break;
    case 113: if(n<6||n>7)return 0;Damage(v,n);break;
    case 129: if(n!=2)return 0;if(I(v[1]))S().mask|=I(v[0]);else S().mask&=~I(v[0]);break;
    case 130: S().status=I(v[0]);break;
    case 131: M().invincible_timer=int(std::ceil(I(v[0])/Rate()));break;
    case 134: Put(v,S().gekirin);break; // initialized from the donor row; rage reactions remain host-owned in R1.
    case 135: Put(v,0);break; // sole donor's target priority.
    case 138: Put(v,0);break; // DC1 has no DC2 piyori-mark state.
    case 150: if(n!=3)return 0;S().blow_speed=F(v[0])*Scale*M().knockback[2];S().blow_decel=F(v[1])*Scale;S().blow_ticks=I(v[2]);break;
    case 151: S().dead=true;S().move_speed=0;M().movement_speed=0;M().lockon_enabled=0;std::fprintf(stderr,"ixdc2 dead-start donor=e202a slot=%d hp=%d reward_owner=DC1\n",S().slot,M().hp);break;
    case 152: if(S().dead&&M().hp==0&&!S().paid){M().state=-1;S().paid=true;}break; // CheckDmg already decremented alive_count and AddKills.
    case 160: Put(v,13);break;
    case 171: if(n!=2)return 0;Put(v+1,0);break;
    case 180: C().motion_flags|=4;break;
    case 182:{int key=KeyNo(v[0].s);if(key<0)return 0;int flags=n>=3?I(v[2]):0;float step=n>=2&&F(v[1])>0?F(v[1]):pack->keys[key].step;
        if(key==4)S().attack_started=true; // authored spin-entry key, for completion evidence
        if(C().motion_no!=key||(flags&4))std::fprintf(stderr,"ixdc2 motion frame=%lld donor=e202a slot=%d key=%d flags=%d step=%.4f\n",static_cast<long long>(GameFrameCount()),S().slot,key,flags,step);
        // DC1 reserves numeric keys 1/2 for player locomotion phase carry. Donor keys use names.
        if(C().motion_no!=key)flags|=4;
        S().motion_step=step;
        C().SetMotion(key,flags);C().SetMotionSpeed(step*1.2f*Rate());M().requested_motion=key;M().requested_motion_flags=flags;M().requested_motion_speed=step*1.2f*Rate();break;}
    case 183:{int key=n==2?KeyNo(v[1].s):C().motion_no;if(key<0)return 0;
        // DC2 character.cpp:740: frame <= end && frame + 2*(1.2*step) >= end.
        // Its margin is in donor key units and must not shrink with host rate.
        float frame=C().motion_type.state.time,end=float(C().motion_type.motion_info[key].end);
        Put(v,int(frame<=end && frame+2.4f*S().motion_step>=end));break;}
    case 184:case 185:{
        if(n<1||n>2||(n==2&&(v[1].type!=RS_STR||!v[1].s)))return 0;
        // DC2's optional name selects a CActionChara attachment, not a motion key.
        // Vanguard has only its root character. Unknown attachments return typed zero.
        bool root=n==1||std::strcmp(v[1].s,"e202a")==0;
        int key=C().motion_no,status=0;float ratio=0;
        if(root&&pack&&key>=0&&size_t(key)<pack->keys.size()){
            auto &k=pack->keys[key];float frame=C().motion_type.state.time;
            ratio=k.end!=k.start?(frame-k.start)/float(k.end-k.start):0;
            // DC2 NormalDrive: END when frame + 1.2*step > end, including HOLD.
            // DC1 holds just below end; compare in donor units, independent of host Hz.
            if(C().motion_flags&4)status=1;
            else if(frame+1.2f*S().motion_step>k.end)status=4;
            else if(C().motion_type.state.blending)status=3;
            else status=frame<=k.start?1:2;
        }
        if(id==184)Put(v,ratio);else Put(v,status);
        break;}
    case 47:case 116:case 117:case 137:case 191:case 194:case 200:case 202:
        std::fprintf(stderr,"ixdc2 Unimplemented optional-effect donor=e202a slot=%d id=%d argc=%d\n",S().slot,id,n);return 0;
    default:std::fprintf(stderr,"ixdc2 Unimplemented donor=e202a slot=%d id=%d argc=%d\n",S().slot,id,n);return 0;
    }
    return 1;
}
template<size_t Id>int NativeCall(RS_STACKDATA *v,int n){return Dispatch(int(Id),v,n);}
template<size_t... Id>auto MakeTable(std::index_sequence<Id...>){return std::array<Native,sizeof...(Id)>{&NativeCall<Id>...};}
auto table=MakeTable(std::make_index_sequence<256>{});
}
Native *Table(){return table.data();}
void TestLoadPack(bool fresh){if(fresh)pack.reset();Load();}
bool IsDonor(CMonstorUnit *u,int slot){return S().owner==u&&S().slot==slot&&u->interpreter[slot].prog==S().script;}
int Program(int dc1){switch(dc1){case 1:return 100;case 50:case 100:return 200;case 110:return 500;case 120:return 1000;default:return dc1;}}
void BindTest(CMonstorUnit *u,int slot){
    state={};S().owner=u;S().slot=slot;S().script=u->interpreter[slot].prog;u->interpreter[slot].ext_func(Table(),256);
    if(S().script){
        auto *base=reinterpret_cast<char*>(S().script);
        auto *rows=reinterpret_cast<RS_PROGDATA*>(base+S().script->prog);
        for(int i=0;i<S().script->prog_num;++i)if(rows[i].no==200){std::memcpy(&S().main_addr,base+rows[i].func,sizeof(int));break;}
    }
}
void FixtureMotion(const char *name,const char *joint,int start,int end,float step){pack=std::make_unique<Pack>();pack->keys.push_back({name,start,end,step});pack->collisions=nlohmann::json::array({nlohmann::json::array({"OBJECT_NAME2",nlohmann::json::array({3,joint,2.0})})});}
void PlayerHit(int owner,int damage,int before,int after){if(S().owner&&owner==S().slot*5+200)std::fprintf(stderr,"ixdc2 player-hit frame=%lld donor=e202a slot=%d owner=%d damage=%d hp_before=%d hp_after=%d\n",static_cast<long long>(GameFrameCount()),S().slot,owner,damage,before,after);}
void Reset(CMonstorUnit *u){if(S().owner==u){state={};consumed=false;}}
void Install(CMonstorUnit *u,int slot){
    const char *selected=std::getenv("DC_IXION_DC2_MONSTER");
    if(!selected||std::strcmp(selected,"e202a")||consumed||selectMapNo!=0||UserStatus->cur_floor!=0)return;
    float spawn[4];u->chara[slot][0].GetPosition(spawn);
    if(DistVector(spawn,CharaMain.pos)>140)return; // one normal floor encounter in the player's active neighborhood
    try{Load();}catch(const std::exception &e){std::fprintf(stderr,"ixdc2 monster refused: %s\n",e.what());pack.reset();consumed=true;return;}
    consumed=true;float pos[4];u->chara[slot][0].GetPosition(pos);
    u->chara[slot][0]=pack->character;auto &c=u->chara[slot][0];c.motion[0]=&c.motion_type;c.SetPosition(pos);
    auto &m=u->monster[slot];m.attachment_count=0;m.hp=m.max_hp=200;m.defense=5;m.hardness=1;m.money=14;m.money_chance=100;m.exp=16;
    m.body_radius=m.collision_radius=15*Scale;m.lockon_enabled=1;m.clip_distance=600;m.revealed=-1;m.rare_item=-1;m.shot_effect=m.shot_effect2=-1;
    m.knockback[0]=0;m.knockback[2]=1;
    std::memset(&u->sound[slot],0,sizeof(u->sound[slot]));for(int &id:u->sound[slot].id)id=-1;u->sound[slot].sequence_id=-1;
    std::memset(&u->guard[slot],0,sizeof(u->guard[slot]));
    std::memset(&u->effect[slot],0,sizeof(u->effect[slot]));std::memset(&u->effect2[slot],0,sizeof(u->effect2[slot]));std::memset(&u->effect3[slot],0,sizeof(u->effect3[slot]));
    auto &vm=u->interpreter[slot];vm.load(reinterpret_cast<RS_PROG_HEADER*>(pack->script.data()),nullptr,128,nullptr,512);BindTest(u,slot);
    for(auto &cmd:pack->collisions){auto &a=cmd[1];int group=a[0];
        for(size_t at=1;at+1<a.size();at+=2){std::string name=a[at];auto *f=c.frame->SearchFrame(name.data());if(!f)continue;
            float radius=a[at+1].get<float>()*2*Scale;size_t i=(at-1)/2;
            if(group==2&&i<16){auto &e=u->effect[slot];e.timer[i]=1;e.frame[i]=f;e.radius[i]=radius;for(int &v:e.parameter[i])v=100;for(int &v:e.body_parameter[i])v=100;}
            if(group==0){m.lockon_frame=f;m.lockon_scale_x=m.lockon_scale_y=1;}
            if(group==4&&i<12){auto &e=u->effect3[slot];e.timer[i]=1;e.frame[i]=f;e.radius[i]=radius;e.count=int(i+1);}
        }
    }
    std::fprintf(stderr,"ixdc2 spawned donor=e202a slot=%d life=200 exp=16 money=14 position=(%.3f,%.3f,%.3f)\n",slot,pos[0],pos[1],pos[2]);
    vm.run(100);u->script_state[slot]=0;
}
bool Tick(CMonstorUnit *u,int slot){
    if(!IsDonor(u,slot))return true;
    Textures();S().cadence+=25.0/HostHz();bool tick=S().cadence>=1-1e-9;if(tick)S().cadence=std::max(0.0,S().cadence-1);
    if(tick&&S().rot_speed!=0){float r[4];C().GetRotation(r);float d=std::remainder(S().next_yaw-r[1],6.283185307f);r[1]+=std::clamp(d,-S().rot_speed,S().rot_speed);C().SetRotation(r);}
    float p[4];C().GetPosition(p);float d=0;for(int i=0;i<3;++i)d+=(S().destination[i]-p[i])*(S().destination[i]-p[i]);d=std::sqrt(d);
    if(d<S().arrival)S().move_speed=0;
    M().movement_speed=S().move_speed*Rate();for(int i=0;i<3;++i)M().movement[i]=d>1e-5f?(S().destination[i]-p[i])/d:0;
    M().knockback[0]=0;
    if(S().blow_ticks>0){for(int i=0;i<3;++i)M().movement[i]=M().knockback_direction[i];M().movement_speed=S().blow_speed*Rate();if(tick){--S().blow_ticks;S().blow_speed=std::max(0.f,S().blow_speed-S().blow_decel);}}
    return tick;
}
void BeforeDamage(CMonstorUnit *u,int slot){if(IsDonor(u,slot)){S().hp_before=M().hp;}}
void AfterDamage(CMonstorUnit *u,int slot,int result){if(IsDonor(u,slot)&&result){S().attack_started=false;std::fprintf(stderr,"ixdc2 host-hit donor=e202a slot=%d hp_before=%.0f hp_after=%d result=%d attacker=%d dc1_kill=%d\n",slot,S().hp_before,M().hp,result,M().last_attacker,result==2);}}
void AfterStep(CMonstorUnit *u,int slot){
    if(!IsDonor(u,slot))return;float p[4];C().GetPosition(p);int hp=UserStatus->hp[int(UserStatus->cur_chara)];
    auto &vm=u->interpreter[slot];
    if(S().attack_started&&vm.func&&vm.func->addr==S().main_addr&&vm.call_sp==vm.call){
        std::fprintf(stderr,"ixdc2 attack-cycle-complete frame=%lld donor=e202a slot=%d returned_to=200 hp=%d player_hit_interrupted=0\n",static_cast<long long>(GameFrameCount()),slot,M().hp);
        S().attack_started=false;
    }
    if(hp!=S().player_before)std::fprintf(stderr,"ixdc2 observed-player-hp frame=%lld donor=e202a slot=%d before=%d after=%d\n",static_cast<long long>(GameFrameCount()),slot,S().player_before,hp);
    S().player_before=hp;
    if(GameFrameCount()%60==0)std::fprintf(stderr,"ixdc2 state frame=%lld donor=e202a slot=%d program_end=%d key=%d time=%.3f hp=%d toan_hp=%d pos=(%.3f,%.3f,%.3f) dist=%.3f\n",static_cast<long long>(GameFrameCount()),slot,u->interpreter[slot].IsEnd(),C().motion_no,C().motion_type.state.time,M().hp,hp,p[0],p[1],p[2],Dist());
}
int WeaponXp(){auto *s=static_cast<CDngStatusData*>(UserStatus);int c=UserStatus->cur_chara;return s->chara_weapons[c][int(s->equipped_weapon_slot[c])].experience;}
void Reward(CMonstorUnit *u,int slot,int before){if(IsDonor(u,slot))std::fprintf(stderr,"ixdc2 DC1-reward donor=e202a slot=%d last_attacker=%d exp=%d weapon_xp_before=%d weapon_xp_after=%d money_base=%d drop_item=%d\n",slot,M().last_attacker,M().exp,before,WeaponXp(),M().money,M().drop_item);}
}
