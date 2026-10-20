#include "ixion_api.h"

#include <SDL3/SDL.h>
#include <libpad.h>

#include <cstdio>

#include "gameloop.hpp"
#include "gamepad.hpp"
#include "platform/audio.hpp"
#include "platform/clock.hpp"
#include "platform/embed.hpp"
#include "platform/input.hpp"
#include "platform/mouse.hpp"
#include "platform/paths.hpp"
#include "platform/settings.hpp"
#include "platform/window.hpp"

// Link the real plugin and game/runtime objects, substituting only its expensive run entry.
int RunDarkCloud(int, const char **) { return 0; }

namespace {
unsigned failures = 0;

void Check(bool ok, const char *what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int Poll(void *, uint32_t, void *) { return 0; }

int Checkpoint(void *, uint32_t, uint32_t) { return 1; }

void Render(void *, float *out, int frames) { SDL_memset(out, 0, frames * 2 * sizeof(float)); }

bool SDLCALL Sensors(void *, bool) { return true; }

void Press(SDL_Scancode code) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.down = true;
    e.key.scancode = code;
    InputHandleEvent(e);
}
} // namespace

int main(int argc, char **argv) {
    PathsSetSaveRoot(PathsExecutable().parent_path() / "smoke-saves-c1");
    const auto *api = ixion_get_api(IXION_ABI_VERSION);
    Check(api && api->size == sizeof(IxionApi), "ABI size/version");
    Check(ixion_get_api(1) == nullptr, "old ABI rejected");
    uint32_t shortHost = 4;
    Check(api->bind(reinterpret_cast<const IxionHost *>(&shortHost)) != 0, "short host prefix rejected");
    IxionHost host{sizeof(IxionHost), IXION_ABI_VERSION, nullptr, Poll, Checkpoint, nullptr, nullptr};
    Check(api->bind(&host) == 0, "valid host bind");
    Check(api->run(1, argv) == 0, "plugin installs embedding");
    if (argc > 1) {
        char  data[] = "--data";
        char *args[] = {argv[0], data, argv[1], nullptr};
        Check(api->preflight(3, args) == 0, "own data recognizer accepts known extraction");
        char  missing[] = "--data";
        char  absent[] = "Z:/ixion-hop-r3-nonexistent-data";
        char *bad[] = {argv[0], missing, absent, nullptr};
        Check(api->preflight(3, bad) != 0, "own recognizer rejects missing data");
    }
    std::puts("plugin ABI/bind/data preflight: PASS");

    InputResetBindings();
    InputPadState neutral;
    neutral.connected = true;
    InputSetOverride(1, &neutral);
    unsigned char dma[2][1024]{};
    scePadInit(0);
    scePadPortOpen(0, 0, dma[0]);
    scePadPortOpen(1, 0, dma[1]);
    for (int i = 0; i < 10; ++i) {
        InputPoll();
        GamePad.UpDate();
    }
    Press(SDL_SCANCODE_W);
    Press(SDL_SCANCODE_SPACE);
    InputPoll();
    GamePad.UpDate();
    GamePad.UpDate();
    Check(GamePad.On(PAD_CROSS) && GamePad.GetLY() != 0, "cached held key/button established");
    api->activate(0);
    api->activate(1);
    const auto ticks = ClockTickCount();
    Check(EmbedCheckpoint(kEmbedFrameEnd), "resume callback");
    Check(!GamePad.On(PAD_CROSS) && GamePad.GetLY() == 0, "first resumed sample drops stale hold");
    Check(!GamePad.Down(PAD_CROSS) && !(GamePad.GetPadUp() & PAD_CROSS), "no artificial input edge");
    Check(ClockTickCount() == ticks, "resume refresh advances no clock ticks");
    std::puts("held-input resume and cached retail samples: PASS");

    Check(SDL_InitSubSystem(SDL_INIT_GAMEPAD), "virtual gamepad subsystem");
    SDL_VirtualJoystickSensorDesc sensor{SDL_SENSOR_GYRO, 60.0f};
    SDL_VirtualJoystickDesc       desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.name = "Ixion regression gyro";
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.nsensors = 1;
    desc.sensors = &sensor;
    desc.SetSensorsEnabled = Sensors;
    const auto device = SDL_AttachVirtualJoystick(&desc);
    Check(device != 0, "virtual gyro controller");
    InputInit();
    InputPoll();
    auto *external = SDL_OpenGamepad(device);
    auto *second = SDL_OpenGamepad(device);
    Check(external && external == second, "SDL shares controller handle between clients");
    HostSettings config;
    config.gyro = ConfigGyro::Always;
    InputApplyConfig(config);
    Check(SDL_SetGamepadSensorEnabled(external, SDL_SENSOR_GYRO, false), "other client disables gyro");
    api->activate(1);
    Check(SDL_GamepadSensorEnabled(external, SDL_SENSOR_GYRO), "activation restores gyro on");
    config.gyro = ConfigGyro::Off;
    InputApplyConfig(config);
    Check(SDL_SetGamepadSensorEnabled(external, SDL_SENSOR_GYRO, true), "other client enables gyro");
    api->activate(1);
    Check(!SDL_GamepadSensorEnabled(external, SDL_SENSOR_GYRO), "activation restores gyro off");
    SDL_CloseGamepad(second);
    SDL_CloseGamepad(external);
    InputShutdown();
    SDL_DetachVirtualJoystick(device);
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    std::puts("shared virtual-controller gyro policy on/off: PASS");

    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    Check(AudioOutputStart(48000, Render, nullptr), "dummy audio stream starts");
    SDL_Delay(40);
    Check(AudioOutputRenderedFrames() > 0, "mixer work counted");
    Check(AudioOutputPause(true), "pause reports success");
    const auto frames = AudioOutputRenderedFrames();
    SDL_Delay(60);
    Check(AudioOutputRenderedFrames() == frames && AudioOutputPaused(), "paused mixer work is frozen");
    Check(AudioOutputPause(false), "resume reports success");
    SDL_Delay(40);
    Check(AudioOutputRenderedFrames() > frames, "mixer resumes");
    AudioOutputStop();
    Check(AudioOutputPause(true), "pause before a stream exists");
    Check(AudioOutputStart(48000, Render, nullptr), "stream starts while parked");
    const auto restartFrames = AudioOutputRenderedFrames();
    SDL_Delay(40);
    Check(AudioOutputRenderedFrames() == restartFrames && AudioOutputPaused(), "late stream preserves pause");
    AudioOutputStop();
    AudioOutputPause(false);
    std::puts("audio pause result and mixer counters: PASS");

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    for (int i = 0; i < 2; ++i) {
        Check(SDL_InitSubSystem(SDL_INIT_VIDEO), "host video reference");
        WindowConfig config;
        config.width = 320;
        config.height = 240;
        config.vulkan = false;
        config.headless = true;
        WindowInit(config);
        MouseConfigure(false, {});
        api->activate(0);
        Check(SDL_CursorVisible(), "deactivation releases cursor even with capture off");
        WindowShutdown();
        Check((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0, "module leaves host video reference");
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        Check((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0, "module video reference balanced");
    }
    std::puts("cursor release and repeated embedded video lifecycle: PASS");
    std::printf("module regressions: %s (%u failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
