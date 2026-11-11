#pragma once
#include <cstdlib>
#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/achievements.h"

namespace input {
struct Pad;
struct WheelFeel;
}

// the host side: window, settings
namespace app {

enum class DisplayMode : uint32_t { Fullscreen, Borderless, Windowed };

struct Settings {
    DisplayMode display_mode = DisplayMode::Borderless;
    uint32_t width = 0, height = 0;  // 0 is the desktop mode
    uint32_t render_percent = 100;   // of the output height
    uint32_t fps_cap = 60;           // 0: only the title's present interval
    bool vsync = true;
    // HDR output (the Vulkan renderer, on a screen in Windows' HDR mode): the picture at Windows' SDR white, the 3D
    // scene's highlights raised toward the peak; nits, 0 the screen's own (Windows' "SDR content brightness", DXGI's peak)
    bool hdr = false;
    uint32_t hdr_peak = 0, hdr_paper = 0;
    uint32_t aniso = 0;              // 0 as the game asks
    std::string join_ip;                   // co-op: a host past the LAN the system link search also asks
    // 0 from the output's shape, 1 4:3, 2 16:9 (the console's TV setting, at boot), 3 widescreen filling the screen's
    // shape (ultrawide: the title's 3D views widened by its GameInfo code, the frame shown at the screen's shape)
    uint32_t aspect = 0;
    // controls, by input::Control: the physical pad input that drives each console input, and up to two keys (Windows
    // virtual key codes, 0 none) that press it on controller 1
    std::vector<uint8_t> pad_map;    // empty: each input drives itself
    std::vector<std::array<uint16_t, 2>> keys;  // empty: the default keys
    bool keyboard = true;
    // the keyboard and mouse are controller 1 alone, the controllers 2 to 4 (split screen with one controller and the
    // keyboard); else the first controller is controller 1 with them
    bool keyboard_alone = false;
    // the mouse moves controller 1's right stick, for a title that offers it (GameInfo::mouse_look): the cursor is
    // held in the window while the game has it
    bool mouse_look = true;
    // the on-screen touch controls on a touch screen (Windows; Android always has them): a finger on the screen plays
    // controller 1, see app/touch.cpp
    bool touch_controls = false;
    uint32_t mouse_sensitivity = 100;  // percent
    bool mouse_invert_y = false;
    uint32_t monitor = 0;            // 0 the primary display, n the nth display Windows lists
    bool pause_unfocused = false;    // the game stands still, silent, while its window is not in front
    bool show_fps = false;           // frames a second in a corner
    bool damage_numbers = false;     // the damage a hit did, a number floating up from the mark
    uint32_t hit_feedback = 2;       // a player's shot hitting (HitMarker): 0 nothing, 1 a mark, 2 a mark and a tick
    bool sharp_2d = true;            // GameInfo::sharpen_2d: the HUD's, menus' and text's pictures enlarged sharp
    uint32_t menu_colors = 0;        // the title's menus recoloured (its own texture_bound): 0 as drawn, 1 blue and silver
    uint32_t msaa = 0;               // 0 none, 1 2 3 MSAA 2x 4x 8x on the scene
    bool fxaa = false;               // FXAA on the finished frame
    bool smaa = false;               // SMAA on the 3D scene before the HUD (the Vulkan renderer)
    uint32_t sharpen = 0;            // percent, contrast adaptive sharpening of the finished frame
    uint32_t renderer = 0;           // 0 Direct3D 12, 1 Direct3D 11, 2 Vulkan, at the next start
    uint32_t language = 1;           // the console's language (XC_LANGUAGE, GameInfo::languages), at the next start
    // a steering wheel and its kit (core/wheel.cpp), playing as controller 1: each control on any DirectInput device,
    // named by the device (and its instance, telling two of the same apart), then an axis (0-7: X Y Z Rx Ry Rz,
    // slider 1, slider 2) or a button (0-127). No device: the wheel found by itself (the first driving device; other
    // devices are bound by name). A pedal reads 0 at rest and 1 at full, from those raw values (-32768 to 32767).
    struct WheelBind {
        std::string device, guid;
        int32_t axis = -1, button = -1;
        int32_t rest = 32767, full = -32768;
        bool invert = false;  // steering: turning right reads negative
        bool operator==(const WheelBind&) const = default;
    };
    // the defaults, a Logitech G920/G923/G29: steering X, throttle Y, brake Rz, clutch slider 1, pedals released at the top
    WheelBind wheel_steer{.axis = 0}, wheel_throttle{.axis = 1}, wheel_brake{.axis = 5}, wheel_clutch{.axis = 6};
    uint32_t wheel_range = 900;      // the wheel's rotation lock to lock, as its driver sets it (Logitech G HUB: 900)
    uint32_t wheel_lock = 360;       // degrees of wheel turn for the game's full steering
    uint32_t wheel_deadzone = 0;     // percent of the steering around the centre that reads as straight
    std::vector<WheelBind> wheel_buttons;  // per input::Control: the button that presses it
    std::vector<WheelBind> wheel_gears;    // an H-pattern shifter's positions: R, 1 to 7 (kGears)
    uint32_t ffb_strength = 70;      // force feedback overall, percent, 0 off
    uint32_t ffb_road = 50;          // the game's rumble (kerbs, impacts) felt in the wheel, percent
    // Discord Rich Presence (discord.cpp): on, and the application's id from Discord's developer portal (empty: off)
    bool discord = true;
    std::string discord_app_id;
    // Online list: a host publishes its game to the master server (its address, map and players, every 30 s) when on
    bool publish_server = false;
    bool hurt_effects = true;  // the red pulse at low health (app/blood.cpp SetHealth)
    // the crosshair (app/crosshair.cpp): 0 the game's own, 1 dot, 2 cross, 3 ring, 4 cross and dot; colour as RGBA
    // (IM_COL32 layout, red in the low byte), size and gap in pixels at 1080 lines, thickness too
    uint32_t crosshair_style = 0;
    uint32_t crosshair_color = 0xFFFFFFFFu;
    uint32_t crosshair_size = 10, crosshair_gap = 4;
    uint32_t crosshair_thickness = 2;
    std::string master_url = "https://masterserver.dankelsoft.dk/api.php";
};
// the wheel buttons of a Logitech G920/G923 (Xbox) for each input::Control, -1 none
int8_t DefaultWheelButton(int c);
constexpr int kGears = 8;  // an H-pattern shifter's positions: R, 1 to 7
const char* GearName(int g);  // "R", "1" ... "7"
// a wheel control bound by moving or pressing it on any device (core/wheel.cpp; the settings window's Wheel tab and a
// title's own menus): every device's state is kept as it begins, then the first axis moved far enough (the steering a
// little, a pedal most of its travel) or button newly pressed becomes the binding, saved at once (Save, then
// input::WheelSetup). One at a time, any thread. While it waits the rig reaches no controller, nor until the button
// taken is let go (it would press the menu's own buttons)
enum class WheelDetect { None, Steer, Pedal, Button };
void WheelDetectStart(WheelDetect kind, Settings::WheelBind& bind);
void WheelDetectCancel();
WheelDetect WheelDetecting(const Settings::WheelBind* bind = nullptr);  // what waits (for that binding), None nothing
bool WheelDetectPoll();  // each frame while one waits: true when the binding was taken
const char* WheelAxisName(int axis);                       // "X", "Y", "Z", "Rx", "Ry", "Rz", "Slider 1", "Slider 2"
std::string WheelBindText(const Settings::WheelBind& b);  // "the wheel: X axis", "G27 Pedals: button 3", "-" unbound
// the default keys for input::Control c
std::array<uint16_t, 2> DefaultKeys(int c);
std::string KeyToName(uint16_t vk);  // settings file names of Windows virtual keys
uint16_t KeyFromName(const std::string& name);
// the in-game settings window (F1): open, and drawn each frame inside the ImGui frame (overlay.cpp)
bool OverlayOpen();
void ToggleOverlay();
void DrawOverlay();
// a key pressed while the overlay waits for one to bind (window thread); true takes it
bool OverlayKey(uint16_t vk);
// a title's own key binding (its menus, any thread): the next key or mouse button pressed is taken for it, and the
// keyboard and mouse reach neither the controllers nor the menu pointer while it waits, nor until that key is let go
void BeginKeyCapture();
void EndKeyCapture();
bool TakeCapturedKey(uint16_t& vk);  // the key taken (VK_ESCAPE: the player cleared it), once
bool KeyCaptureBusy();
bool CaptureKey(uint16_t vk);  // window thread, a new press (not a repeat): true when the capture takes it

Settings& Get();
void Load();
void Save();
void LogSettings();  // every setting, as the settings file has them
void LogSystem();    // the machine and the settings, at a run's start
void ApplyDisplay();  // any thread
struct Mode {
    uint32_t width, height;
};
Mode Desktop();
void* Monitor();                  // HMONITOR of the monitor setting (the primary display when it is gone)
std::vector<std::string> Monitors();  // the displays' names, in the monitor setting's order from 1
bool Paused();                    // pause_unfocused and the window is not in front (Windows)
void DrawHud(double fps);         // the frame counter, inside the ImGui frame (overlay.cpp)
// the runtime's own drawing over the frame (the frame counter, hit markers, blood, unlock pop-ups) left out while
// hidden: a title's photo mode (any thread)
void HideOverlays(bool hide);
bool OverlaysHidden();
// Discord Rich Presence (discord.cpp): what the player is doing, on their Discord profile while Discord runs on this
// PC (its local IPC, a background thread; nothing without Discord, Settings::discord off or no discord_app_id).
// details the first line ("Playing The Exchange"), state the second (may be empty), start the Unix time the elapsed
// time counts from (0 none). Any thread; sent when it changes
void SetPresence(const std::string& details, const std::string& state, int64_t start);
// hit feedback (hit_marker.cpp): a game's hook when a local player's shot hits a character, at that player's
// crosshair (x, y the shown frame's fractions); kind 0 a hit, 1 the head, 2 a kill
void HitMarker(float x, float y, int kind, float damage = 0);  // damage > 0: a number shows (damage_numbers)
void DrawHitMarkers();                          // inside the ImGui frame, from DrawHud
void MixHitSound(float* out, uint32_t frames);  // the ticks into the mixer's stereo output
// blood (gpu::GraphicsStyle::blood, blood.cpp): a game's hooks, a spray where a player's shot hit a character (x, y
// the shown frame's fractions, strength 0 to 1), and a player's view (its corners, the same) hit, by how much (0 to 1)
void BloodSpray(float x, float y, float strength);
void BloodHurt(float x0, float y0, float x1, float y1, float amount);
// Low health (Settings::hurt_effects): the player's health as a fraction (1 full, 0 none), set each frame by the game;
// under a third the view's edges go red and pulse, faster the lower it is (drawn over the picture, every renderer)
void SetHealth(float fraction);
void DrawCrosshair();  // the custom crosshair, if one is set (app/crosshair.cpp)
void DrawBlood();                               // inside the ImGui frame, from DrawHud
void FeedPad();                   // controllers to the settings window's navigation, before each ImGui frame
Mode Output();
float RenderScale(uint32_t guest_height);
// the output is 3:2 or wider: the console reports a widescreen TV and the frame is shown 16:9, else 4:3 as on a 4:3 TV
bool Widescreen();
// the shape the frame is shown at: 4:3, 16:9, or the screen's when the aspect setting fills it (never under 16:9)
float FrameAspect();
// the cursor in the title's 2D layout (0 to 1 across its width and height as GameInfo::hud_aspect lays it out, the
// shown frame's letterbox and ultrawide squeeze taken out); false when it is outside the window
bool FramePoint(float& u, float& v);
void SetFramePointOverride(float u, float v);  // XBE_PAD_CMD's PTR: a pointer of the test's in its place

std::filesystem::path ExeDir();
// the game in assets (game_files.cpp): its folder and this executable's xbe, from the extracted files or a disc
// image (.iso), which it mounts as D:; false after telling the player what is missing
struct GameFiles {
    std::filesystem::path dir;
    std::vector<uint8_t> xbe;
};
bool FindGame(GameFiles& game);
int Instance();  // --instance N on the command line, 0 without: a second copy on this PC for system link tests
void* Window();  // HWND (off Windows the SDL_Window*)
// on-screen touch controls (app/touch.cpp): Android's, Windows' with the touch_controls setting, else XBE_TOUCH=1
bool TouchEvent(const void* sdl_event);         // window thread, each event first; true: taken (fingers, Android's back)
// a Windows touch point (WM_POINTER*): kind 0 down, 1 moved, 2 up, 3 cancelled; x, y in client pixels
void TouchPoint(uint32_t id, int kind, float x, float y);
bool TouchRead(float* out);  // input::kControls entries: controller 1's inputs from the screen, 0 to 1 each; false while hidden
bool TouchControlsShown();
void DrawTouch();                               // inside the ImGui frame, UiLock held
#ifndef _WIN32
// off Windows (app/main_sdl.cpp, SDL3): the Vulkan renderer's way into the window, and the displays
std::vector<const char*> VulkanInstanceExtensions();  // the instance extensions the window's surface needs
bool CreateVulkanSurface(void* instance, void* surface);  // a VkInstance, a VkSurfaceKHR* to fill; false logged
void* VulkanGetInstanceProcAddr();  // the loader SDL opened for the window: a PFN_vkGetInstanceProcAddr
void WindowPixels(uint32_t& width, uint32_t& height);  // the window's drawable size in pixels (GetClientRect's)
// Android: the app is in the background, its window's surface gone (nothing is presented), and a count of the
// surfaces the window has had (a new one after each return: the renderer makes its Vulkan surface again); elsewhere
// false and 0
bool Backgrounded();
uint32_t SurfaceGeneration();
std::vector<Mode> DisplayModes();  // the monitor setting's display's fullscreen sizes, widest first, each once
std::string Utf8(const wchar_t* w);  // a wide string (kGame's, UTF-32 here) as UTF-8
// a message box over the game window (SDL's); yes_no: Yes and No buttons, true for Yes, else OK and true
bool MessageBox(const wchar_t* text, bool error, bool yes_no = false);
#ifdef __ANDROID__
// Android: the other image's library started in a new process of the app (kernel::Relaunch's exec), this one ends
[[noreturn]] void AndroidRelaunch(const char* exe);
#endif
#endif
// leave the game as closing its window does (the settings window's Quit, a title's own quit item); from any thread
void Quit();
// boot splash: GameInfo::splash .bmp or .png beside the exe, up before the game window
bool OpenSplash();        // window thread, at launch; false without one, and the game window opens at once
void SplashFirstFrame();  // the game's first Swap: waits out the splash's minimum, then opens the game window
void CloseSplash();       // window thread
void OpenGameWindow(bool apply = true);  // any thread but the window thread's; returns once the window is up
// relaunched: the previous instance keeps its window up until this one has shown its first frames (handover.cpp)
bool StartHandover();   // window thread, at launch; true: the game window starts hidden
bool HandingOver();
void FinishHandover();  // any thread but the window thread's: opens the game window, lets the old instance go

// held around ImGui: the window thread's messages and the renderer's frame
std::recursive_mutex& UiLock();

// an on-screen touch button (app/touch.cpp): the console control it holds, its label (another in the title's menus,
// GameInfo::menu_up), its centre in screen heights from the right edge (from the left edge when negative) and from the
// bottom, and its radius
struct TouchButton {
    int control;  // input::Control
    const char* label;
    const char* menu_label;
    float x, y, r;
};

// what the game repo defines once
struct Option {  // a setting in the settings file, flag or value
    const char* key;
    bool* flag;
    uint32_t* value;
    const char* label = nullptr;  // the settings window's name for it, else the key
    const char* help = nullptr;   // a line under it in the settings window
};
struct GameInfo {
    const wchar_t* title;        // window and splash
    const wchar_t* short_title;  // message boxes
    const wchar_t* name;         // window classes, crash dumps, handover event
    const char* log;             // log prefix
    const char* settings;        // settings file beside the exe
    const wchar_t* splash;       // .bmp or .png beside the exe
    const wchar_t* missing;      // message when neither the game files nor a disc image are found
    std::vector<std::pair<const char*, const char*>> images;  // xbe file name, executable name
    std::vector<Option> options;
    // the settings file's toml::table, for what options cannot hold
    void (*load)(const void* table);
    void (*save)(void* table);
    void (*init)();                // after the settings load, before the window and boot
    void (*loaded)();              // image in memory, before its entry runs
    void (*window)(void* hwnd);    // the game window is up, before boot
    // window thread, each message after ImGui's; true returns result instead of the host's handling
    bool (*message)(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);
    void (*ui)(double fps);        // each frame inside the ImGui frame, UiLock held
    void (*pad)(int port, input::Pad& pad);  // a polled pad before the game gets it
    void (*rumble)(int port, uint16_t& left, uint16_t& right);
    std::vector<achievements::Def> achievements = {};  // core/achievements.h
    // other releases' xbe SHA-1s whose code and data are the recompiled one's, differing only in the signature and
    // certificate (region, version, keys): started without the "different release" question
    std::vector<const char*> same_code = {};
    // a steering wheel's force feedback: what the player's car is doing, each poll of controller 1 (input.h)
    void (*wheel)(input::WheelFeel& feel) = nullptr;
    // the level (0 to 1) of voices heard only through the effect sends, which the mixer plays into the speakers in
    // place of the DSP image's effects: a title whose image applies a volume there (PGR2's radio, at the music
    // volume) reports it here, read on the audio thread at each mix; none is full level
    float (*send_level)() = nullptr;
    // movies step one frame per game frame and the host holds a frame BinkWait says is early (xdk/bink.cpp); off for a
    // title whose movie loop waits on BinkWait itself
    bool bink_frame_hold = true;
    // the shape the title lays out its 2D for (HUD, menus): shown wider, that 2D is squeezed into it (gpu::HudSqueeze)
    float hud_aspect = 16.0f / 9.0f;
    // that 2D also where drawn by a vertex program that only moves its position input to the screen (gpu::
    // ScreenProgram: EoN's HUD and menus, through D3D's pass-through program form), or that multiplies it by a 2D
    // orthographic matrix (gpu::MatrixProgram, OrthoConstants: EoN's driving HUD)
    bool squeeze_screen_programs = false;
    // ultrawide: a textured bar the frame's full height ending on its left or right edge (NightFire's sniper scope: a
    // black 32x32 texture from x 0 to 81 and 559 to 640 beside the sight) keeps that edge, as an untextured one always
    // does (gpu::DrawSpan), so the bars reach the screen's sides; off, they are squeezed with the rest of the 2D
    bool edge_bars = false;
    // the textures 2D draws use (pretransformed or as above; swizzled, up to 512 texels) are enlarged two or four times
    // with sharp edges (gpu::Sharpen2D, a background thread) and drawn so: text and HUD pictures stay crisp at high
    // resolutions (Settings::sharp_2d turns it off)
    bool sharpen_2d = false;
    // the title's hooks call HitMarker (a shooter's): the settings window offers Settings::hit_feedback
    bool hit_feedback = false;
    // the languages a title has on its disc, as the console's setting numbers them (1 English, 2 Japanese, 3 German,
    // 4 French, 5 Spanish, 6 Italian, 7 Korean, 8 Chinese, 9 Portuguese): the settings window offers them; none, English
    std::vector<uint32_t> languages;
    // a PAL release (the certificate's game regions without North America) runs on a PAL console with PAL 60 Hz on:
    // a title naming its files by the video standard finds the disc's (EoN's movies are _pal and _palw only)
    bool pal_console = false;
    // disc reads paced as a drive of this many MB a second reads (the console's about 6), 0 at once; XBE_DISC_RATE
    // overrides (EoN's driving streams stall for good on reads that complete at once)
    float disc_rate = 0;
    // a file the title asks to open (its Xbox path, as named) is reported missing when this says so: a title that
    // passes over a missing movie skips it (EoN's logos, by a setting)
    bool (*hide_file)(const std::string& xbox_path) = nullptr;
    // the mouse can play controller 1's right stick (Settings::mouse_look): a shooter's camera and aim
    bool mouse_look = false;
    // the title's code takes the mouse's movement itself (input::TakeMouse) rather than as the right stick
    bool mouse_direct = false;
    // the keys each input::Control starts with (two Windows virtual keys, 0 none), else the SDK's (DefaultKeys)
    std::array<uint16_t, 2> (*default_keys)(int c) = nullptr;
    // a texture the title binds (SetTexture, its guest D3DBaseTexture) with its stage's palette (D3DPalette, 0 none), on
    // the title's thread before the draws that use it, and again when the stage's palette is set (SetPalette): the
    // title may change its texels in guest memory (EoN's button pictures), true when it did, and may give the
    // renderers a larger picture of its own for it (gpu::SetReplacement)
    bool (*texture_bound)(uint32_t header, uint32_t palette) = nullptr;
    // the aspect setting's "from the screen's shape" follows an ultrawide screen instead of stopping at 16:9
    bool auto_fills_screen = false;
    // a title's music decoded to PCM streams against its other sounds, read on the audio thread at each mix (effects the
    // DSP image would have balanced drown it out): the level of PCM streams and of every other voice; none is full level
    float (*pcm_stream_level)() = nullptr;
    float (*other_voice_level)() = nullptr;
    // a menu of the title's is up: the mouse is a pointer there (the cursor shown and free, not mouse look)
    bool (*menu_up)() = nullptr;
    // the pointer's picture in the menus: size x size premultiplied B8G8R8A8 pixels for a cursor that many pixels
    // high, and its hot spot; none, the system's arrow
    void (*menu_cursor)(int size, std::vector<uint32_t>& bgra, int& hot_x, int& hot_y) = nullptr;
    // Vulkan always (Settings::renderer held at 2, the settings window's Renderer choice not shown): a title whose
    // look is Vulkan's. Direct3D 12 still starts in its place on a PC where Vulkan cannot
    bool vulkan_only = false;
    // the settings window (F1); a title whose settings are all in its own menus and its launcher turns it off
    bool settings_window = true;
    // smooth turning (GraphicsStyle::smooth_turn): how far the view will turn by the mouse the title has not read yet,
    // radians (yaw right, pitch up), and the tangent of half its vertical field of view; false when it does not
    // (menus, cutscenes, a controller)
    bool (*view_turn)(float& yaw, float& pitch, float& tan_half_fov) = nullptr;
    // the on-screen touch buttons (Android); none, the console's face buttons, triggers, Start and Back
    std::vector<TouchButton> touch_buttons = {};
};
extern const GameInfo kGame;

// sleeps out the rest of the frame: the title's present interval (vertical blanks a frame at 60 Hz, 0 unpaced)
// within the fps cap
void Pace(uint32_t vblanks = 1);
// called by Pace after each present: the renderer's in-between frame (smooth turning), drawn half way to the next
void SetMidFrame(void (*fn)());
// frames a second for a title asking for vblanks within cap (0 unlimited); 0 unpaced
inline uint32_t PaceRate(uint32_t cap, uint32_t vblanks) {
    // NF_UNCAPPED=1 (diagnostic): no pacing, so the frame's real work time shows (the 10 s average, from Pace)
    static const bool uncapped = [] { const char* e = std::getenv("NF_UNCAPPED"); return e && *e == '1'; }();
    if (uncapped) return 0;
    if (!vblanks) return cap;
    uint32_t fps = 60 / vblanks;
    return cap && cap < fps ? cap : fps;
}

}  // namespace app
