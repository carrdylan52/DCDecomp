#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <vector>
#include "../ixion_dc2_monster.hpp"
#include "character.hpp"
#include "frame.hpp"
#include "gameutil.hpp"
#include "gameloop.hpp"
#include "dun/gameloop.hpp"
#include "mathutil.hpp"
#include "monstorunit.hpp"
#include "platform/clock.hpp"
#include "texture.hpp"
#include "gfx/gfx.hpp"

namespace {
RS_STACKDATA Int(int n){RS_STACKDATA v{};v.type=RS_INT;v.i=n;return v;}
RS_STACKDATA Float(float n){RS_STACKDATA v{};v.type=RS_FLOAT;v.f=n;return v;}
RS_STACKDATA String(const char *s){RS_STACKDATA v{};v.type=RS_STR;v.s=const_cast<char*>(s);return v;}
RS_STACKDATA Ptr(RS_STACKDATA &v){RS_STACKDATA p{};p.type=RS_PTR;p.p=&v;return p;}
class Dc2Monster : public testing::Test {
protected:
    std::unique_ptr<CMonstorUnit> unit;
    CFrame root;
    MOTION_INFO info{10,30,.5f,0};
    void SetUp()override{
        unit=std::make_unique<CMonstorUnit>();
        std::memset(unit->monster,0,sizeof(unit->monster));
        std::memset(unit->effect2,0,sizeof(unit->effect2));
        std::strcpy(root.name,"fixture_joint");
        auto &c=unit->chara[0][0];c.frame=&root;c.SetPosition(0,0,0);c.SetRotation(0,0,0);c.motion_no=0;
        c.motion_type.motion_info=&info;c.motion_type.state.time=10;c.motion_speed=.25f;
        unit->monster[0].knockback[2]=1;
        ixdc2::dc1mon::FixtureMotion("fixture_motion","fixture_joint",10,30,.5f);
        ixdc2::dc1mon::BindTest(unit.get(),0);
        ClockSetTickRate(60);
        CharaMain.pos[0]=3*ixdc2::dc1mon::Scale;CharaMain.pos[1]=0;CharaMain.pos[2]=4*ixdc2::dc1mon::Scale;
    }
    void TearDown()override{ixdc2::dc1mon::Reset(unit.get());}
    int Call(int id,std::initializer_list<RS_STACKDATA> v){std::vector<RS_STACKDATA>a(v);return ixdc2::dc1mon::Table()[id](a.data(),int(a.size()));}
};
TEST_F(Dc2Monster,EveryNativeIsPrivateAnd113RegistersDc2Damage){
    auto *table=ixdc2::dc1mon::Table();for(int i=0;i<256;++i)ASSERT_NE(table[i],nullptr);
    EXPECT_NE(table[113],table[131]);EXPECT_EQ(Call(253,{}),0);
    EXPECT_EQ(Call(113,{String("fixture_hit"),Float(3),String("fixture_motion"),Float(.25f),Float(.75f),Int(0)}),1);
    auto &e=unit->effect2[0];EXPECT_EQ(e.active[0],1);EXPECT_EQ(e.frame[0],&root);
    EXPECT_FLOAT_EQ(e.radius[0],6*ixdc2::dc1mon::Scale);EXPECT_FLOAT_EQ(e.motion_start[0],15);EXPECT_FLOAT_EQ(e.motion_end[0],25);
    EXPECT_EQ(e.kind[0],3);EXPECT_EQ(e.damage[0],13);
}
TEST_F(Dc2Monster,QueriesUseDonorUnitsAndTypedOutputPointers){
    RS_STACKDATA a{},b{},c{};EXPECT_EQ(Call(83,{Ptr(a),Ptr(b),Ptr(c)}),1);
    EXPECT_EQ(a.type,RS_FLOAT);EXPECT_FLOAT_EQ(a.f,3);EXPECT_FLOAT_EQ(c.f,4);
    Call(84,{Ptr(a)});EXPECT_FLOAT_EQ(a.f,5);
    Call(85,{Ptr(a)});EXPECT_NEAR(a.f,std::atan2(3.f,4.f),1e-6f);
    unit->chara[0][0].SetPosition(10*ixdc2::dc1mon::Scale,2*ixdc2::dc1mon::Scale,0);
    Call(75,{Ptr(a),Ptr(b),Ptr(c)});EXPECT_FLOAT_EQ(a.f,10);EXPECT_FLOAT_EQ(b.f,2);
    Call(40,{Ptr(a)});EXPECT_EQ(a.type,RS_INT);EXPECT_EQ(a.i,0);
    Call(42,{Ptr(a)});EXPECT_EQ(a.i,24);Call(43,{Ptr(a)});EXPECT_EQ(a.i,-1);
    Call(171,{Int(-1),Ptr(a)});EXPECT_EQ(a.i,0);
    Call(160,{Ptr(a)});EXPECT_EQ(a.i,13);Call(134,{Ptr(a)});EXPECT_EQ(a.type,RS_FLOAT);EXPECT_FLOAT_EQ(a.f,10);Call(135,{Ptr(a)});EXPECT_EQ(a.i,0);
    Call(55,{Ptr(a)});EXPECT_EQ(a.i,0);Call(138,{Ptr(a)});EXPECT_EQ(a.i,0);
    unit->monster[0].stop_timer=1;Call(58,{Int(8|32),Ptr(a)});EXPECT_EQ(a.i,8);
    Call(54,{Int(51),Int(1)});Call(53,{Int(51),Ptr(a)});EXPECT_EQ(a.i,1);
    for(int i=0;i<50;++i){Call(35,{Int(7),Ptr(a)});EXPECT_GE(a.i,0);EXPECT_LT(a.i,7);}
}
TEST_F(Dc2Monster,MovementAndTurnAdvanceAt25HzAndResetStopsBoth){
    Call(80,{Float(0),Float(0),Float(100),Float(4),Float(1)});
    Call(79,{Float(1),Float(.1f)});
    int ticks=0;for(int i=0;i<60;++i)ticks+=ixdc2::dc1mon::Tick(unit.get(),0);
    EXPECT_EQ(ticks,25);EXPECT_FLOAT_EQ(unit->monster[0].movement_speed,4*ixdc2::dc1mon::Scale*25/60);
    EXPECT_NEAR(unit->chara[0][0].GetRotation()->y,1,1e-5f);
    Call(82,{});ixdc2::dc1mon::Tick(unit.get(),0);EXPECT_FLOAT_EQ(unit->monster[0].movement_speed,0);
    Call(131,{Int(30)});EXPECT_EQ(unit->monster[0].invincible_timer,72);
}
TEST_F(Dc2Monster,MotionWindowCompletionAndDeathCannotPayOrDecrementTwice){
    EXPECT_EQ(Call(182,{String("fixture_motion"),Float(.8f),Int(2)}),1);
    EXPECT_FLOAT_EQ(unit->chara[0][0].motion_speed,.8f*1.2f*25/60);
    RS_STACKDATA done{};unit->chara[0][0].motion_type.state.time=28;
    Call(183,{Ptr(done)});EXPECT_EQ(done.i,0);
    // The donor margin is 2*(1.2*.8)=1.92, independent of host cadence.
    unit->chara[0][0].motion_type.state.time=28.2f;Call(183,{Ptr(done)});EXPECT_EQ(done.i,1);
    unit->chara[0][0].motion_type.state.time=31;Call(183,{Ptr(done)});EXPECT_EQ(done.i,0);
    unit->monster[0].hp=0;unit->monster[0].state=2;unit->alive_count=7;
    Call(151,{});EXPECT_EQ(unit->monster[0].state,2); // collapse still renders
    Call(152,{});Call(152,{});EXPECT_EQ(unit->monster[0].state,-1);EXPECT_EQ(unit->alive_count,7);
    EXPECT_EQ(ixdc2::dc1mon::Program(1),100);EXPECT_EQ(ixdc2::dc1mon::Program(100),200);
    EXPECT_EQ(ixdc2::dc1mon::Program(110),500);EXPECT_EQ(ixdc2::dc1mon::Program(120),1000);
}
TEST_F(Dc2Monster,RealVmWaitResumesLocalsAndDoesNotRestartMain){
    std::array<unsigned char,512> bytes{};
    auto *h=reinterpret_cast<RS_PROG_HEADER*>(bytes.data());h->code=64;h->prog=32;h->prog_num=1;
    int row[]={200,40};std::memcpy(bytes.data()+32,row,sizeof(row));int fd[]={0,0,1,0};std::memcpy(bytes.data()+40,fd,sizeof(fd));
    // local0=7; WAIT; local0+=1; WAIT. All synthetic, no retail bytes.
    const int ins[][3]={{2,0,1},{3,1,7},{5,0,0},{4,0,0},{23,0,0},{2,0,1},{1,0,1},{3,1,1},{6,0,0},{5,0,0},{4,0,0},{23,0,0},{3,1,0},{15,0,0}};
    std::memcpy(bytes.data()+64,ins,sizeof(ins));auto &vm=unit->interpreter[0];vm.load(h,nullptr,128,nullptr,512);
    ixdc2::dc1mon::BindTest(unit.get(),0);EXPECT_EQ(vm.run(200),1);EXPECT_EQ(vm.frame[0].i,7);
    vm.resume();EXPECT_EQ(vm.frame[0].i,8);EXPECT_EQ(vm.IsEnd(),0);
    vm.resume();EXPECT_EQ(vm.IsEnd(),1);
    ixdc2::dc1mon::Reset(unit.get());EXPECT_FALSE(ixdc2::dc1mon::IsDonor(unit.get(),0));
}
TEST_F(Dc2Monster,ControlNativesWriteTypedResultsAndReleaseMotionWait){
    RS_STACKDATA value=Float(-99);
    Call(79,{Float(2),Float(.5f)});
    EXPECT_EQ(Call(78,{Float(.1f),Float(.2f),Float(.3f)}),1);
    for(int i=0;i<12;++i)ixdc2::dc1mon::Tick(unit.get(),0);
    EXPECT_FLOAT_EQ(unit->chara[0][0].GetRotation()->y,.2f);
    Call(80,{Float(0),Float(0),Float(10),Float(4),Float(2)});
    EXPECT_EQ(Call(81,{Ptr(value)}),1);EXPECT_EQ(value.type,RS_INT);EXPECT_EQ(value.i,0);
    // Strict arrival comparison, in reciprocal host units.
    Call(80,{Float(0),Float(0),Float(2),Float(4),Float(2)});
    unit->chara[0][0].SetPosition(0,0,0);
    Call(81,{Ptr(value)});EXPECT_EQ(value.i,0);
    unit->chara[0][0].SetPosition(0,0,ixdc2::dc1mon::Scale);
    Call(81,{Ptr(value)});EXPECT_EQ(value.i,1);
    Call(182,{String("fixture_motion"),Float(.5f),Int(2)});
    auto &c=unit->chara[0][0];c.motion_flags&=~4;
    c.motion_type.state.time=20;
    EXPECT_EQ(Call(184,{Ptr(value)}),1);EXPECT_EQ(value.type,RS_FLOAT);EXPECT_FLOAT_EQ(value.f,.5f);
    EXPECT_EQ(Call(185,{Ptr(value)}),1);EXPECT_EQ(value.type,RS_INT);EXPECT_EQ(value.i,2);
    c.motion_type.state.time=29.5f;
    Call(185,{Ptr(value)});EXPECT_EQ(value.i,4);
    Call(184,{Ptr(value)});EXPECT_FLOAT_EQ(value.f,.975f);
    // The optional name selects an attached character, not a motion key.
    Call(185,{Ptr(value),String("missing_attachment")});EXPECT_EQ(value.i,0);
    Call(184,{Ptr(value),String("missing_attachment")});EXPECT_FLOAT_EQ(value.f,0);
    EXPECT_EQ(Call(78,{Float(0)}),0);EXPECT_EQ(Call(81,{}),0);
    EXPECT_EQ(Call(184,{}),0);EXPECT_EQ(Call(185,{}),0);
}
TEST(Dc2MonsterLoader,RealPackPreservesFallbackNeighborsAndReentersAfterTextureReset){
    if(!std::getenv("DC_IXION_DC2_MONSTER_PACK"))GTEST_SKIP()<<"private converted pack required";
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
    auto *window=SDL_CreateWindow("DC2 monster loader regression",64,64,SDL_WINDOW_HIDDEN);
    ASSERT_NE(window,nullptr);
    gfx::RendererConfig config;config.offscreen=true;config.pipeline_cache="dc2mon-test-pipeline.bin";
    gfx::RendererInit(window,config);
    std::array<u_long128,4096> staging{};
    for(int pass=0;pass<2;++pass){
        TexManager.Initialize(16352);
        TexManager.SetBuffer(staging.data(),int(staging.size()));
        TexManager.textures[0].width=123;TexManager.textures[0].tex1=0x12345678;
        std::strcpy(TexManager.textures[1].name,"retail_neighbor");TexManager.textures[1].block=70;
        std::strcpy(TexManager.blocks[71].name,"ridepod_neighbor");
        const auto fallback=TexManager.textures[0],neighbor=TexManager.textures[1];
        const auto before70=TexManager.blocks[70],before71=TexManager.blocks[71];
        ASSERT_NO_THROW(ixdc2::dc1mon::TestLoadPack(pass==0));
        EXPECT_EQ(std::memcmp(&fallback,&TexManager.textures[0],sizeof(fallback)),0);
        EXPECT_EQ(std::memcmp(&neighbor,&TexManager.textures[1],sizeof(neighbor)),0);
        EXPECT_EQ(std::memcmp(&before70,&TexManager.blocks[70],sizeof(before70)),0);
        EXPECT_EQ(std::memcmp(&before71,&TexManager.blocks[71],sizeof(before71)),0);
        int count=0;for(auto &t:TexManager.textures)if(std::strncmp(t.name,"ixvg_",5)==0){++count;EXPECT_EQ(t.block,69);}
        EXPECT_EQ(count,2);
    }
    TexManager.Initialize(16352);
    std::strcpy(TexManager.blocks[69].name,"foreign_owner");
    const auto foreign=TexManager.blocks[69];
    EXPECT_THROW(ixdc2::dc1mon::TestLoadPack(false),std::runtime_error);
    EXPECT_EQ(std::memcmp(&foreign,&TexManager.blocks[69],sizeof(foreign)),0);
    TexManager.Initialize(16352);
    gfx::RendererShutdown();SDL_DestroyWindow(window);SDL_Quit();
}
}
