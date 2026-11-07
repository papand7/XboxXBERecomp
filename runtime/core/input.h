#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace input {

// the console controller's report, analog buttons are 0-255
struct Pad {
    bool connected = false;
    uint16_t buttons = 0;  // dpad up/down/left/right, start, back, left thumb, right thumb
    uint8_t analog[8] = {};  // A B X Y black white left trigger right trigger
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
    bool wheel = false;  // a steering wheel steers it: the port reports itself a wheel (XInputGetCapabilities)
};

// every input of the console controller, each 0 to 1: buttons, triggers, and each stick direction on its own
enum Control : int {
    kUp, kDown, kLeft, kRight, kStart, kBack, kLThumb, kRThumb,  // the digital buttons, as Pad::buttons bits
    kA, kB, kX, kY, kBlack, kWhite, kLT, kRT,                    // the analog buttons, as Pad::analog
    kLSLeft, kLSRight, kLSUp, kLSDown, kRSLeft, kRSRight, kRSUp, kRSDown,
    kControls
};
const char* ControlName(int c);  // settings file name, "dpad_up" ...
const char* ControlLabel(int c); // shown to the player, "D-pad up" ...
int ControlByName(const char* name);  // -1 when unknown

void Init();  // window thread, before boot
#ifndef _WIN32
// the window thread's SDL events (an SDL_Event*), each one: true when it was a controller plugged in or out, taken here
bool HandleSdlEvent(const void* event);
// the virtual key of an SDL scancode, as Windows' WM_KEYDOWN has it (either shift VK_SHIFT ...); 0 for none
int VkOfScancode(int scancode);
#endif
// the controller on that port, then GameInfo::pad
Pad Poll(int port);
// the keyboard and mouse by Windows virtual key code (core/vk_codes.h; the settings and key bindings store them), any
// thread: held now wherever the focus is, mouse buttons VK_LBUTTON .. VK_XBUTTON2 the physical ones (Windows:
// GetAsyncKeyState; elsewhere SDL's keyboard and mouse state, the codes mapped to scancodes)
bool KeyDown(int vk);
// the game window has the keyboard (Windows: it is the foreground window)
bool GameFocused();
// held while the game window has the keyboard
inline bool KeyHeld(int vk) { return GameFocused() && KeyDown(vk); }
// raw mouse movement, from the window thread (Windows WM_INPUT, SDL's relative mouse motion): read into controller 1's
// right stick (Settings::mouse_look) or taken by the title (TakeMouse)
void MouseMoved(int dx, int dy);
// the mouse plays the right stick now: mouse look on, the game window in front, the settings window shut
bool MouseLooking();
// the mouse a pointer in a title's menu (GameInfo::menu_up), the window in front: its buttons and wheel are read
bool MousePointing();
// for a title taking the mouse itself (GameInfo::mouse_direct): the movement since the last call as a rate, in full
// stick deflections (2400 counts a second at 100% sensitivity is 1), up positive, the settings' sensitivity and invert
// applied; false and nothing taken while the mouse is not looking
bool TakeMouse(float& x, float& y);
// the counts moved since the title last took them (times the sensitivity, y up), left for it: false when the mouse
// does not look (GameInfo::view_turn's smooth turning between the title's frames)
bool PeekMouse(float& x, float& y);
// what controller 1 was last played with: true for the keyboard and mouse (a bound key or mouse button pressed, the
// mouse moved while it looks; the keyboard alone when no controller is plugged in), false for a controller or wheel
// (a button pressed, a stick pushed half way); a title's button pictures may follow it
bool UsingKeyboard();
// the keys controller 1's control c is bound to: the settings', else the defaults (0 none; mouse buttons VK_LBUTTON ..)
std::array<uint16_t, 2> ControlKeys(int c);
// the mouse for a title's own menus, while it looks (MouseLooking): the wheel's notches since the last call (up
// positive), and the buttons held (1 left, 2 right, 4 middle)
int TakeWheel();
uint32_t MouseButtons();
void MouseWheel(int delta);  // window thread, WHEEL_DELTA (120) a notch (an SDL wheel step is one notch)
void Rumble(int port, uint16_t left, uint16_t right);
// the physical controls held on any controller, a bit per Control, from the last polls (the settings window's
// navigation)
uint32_t HeldControls();

// a steering wheel and the rest of a rig through DirectInput (core/wheel.cpp): every game controller DirectInput lists
// but Xbox pads (SDL's), each control bound to one of them (app::Settings::WheelBind), merged into controller 1
constexpr int kGearUnknown = -100;
// what the game tells the force feedback and the shifter, each poll of controller 1 (GameInfo::wheel)
struct WheelFeel {
    bool racing = false;     // the player drives: the wheel's forces follow the car, else a light centring
    float speed = 0;         // m/s
    int gear = kGearUnknown;  // the player's gear: -1 reverse, 0 neutral, 1 first ...; kGearUnknown when not known
    int shift_up = -1, shift_down = -1;  // the console controls (Control) that change gear, for an H-pattern shifter
};
struct WheelDevice {  // one device, as the settings window shows it
    std::string name, guid;  // DirectInput's product name, the instance GUID as text
    bool ffb = false;        // it offers force feedback
    bool feedback = false;   // the forces go to it (the steering's device)
    int axes_count = 0, buttons_count = 0, povs_count = 0;
    uint8_t axis_mask = 0;   // the axes it has, a bit each in the order below (a pedal box may have Z, Rz and a slider)
    int32_t axes[8] = {};    // raw, -32768 to 32767: X Y Z Rx Ry Rz slider 1, slider 2
    std::vector<int> pressed;  // buttons held
    int pov = -1;            // its first hat in hundredths of a degree, -1 centred
};
struct WheelInfo {
    std::vector<WheelDevice> devices;
    bool active = false;     // some binding reads a device: the rig plays as controller 1
    std::string effects;     // the force feedback effects made
    float steer = 0, throttle = 0, brake = 0, clutch = 0;  // as the game gets them: steer -1 to 1, pedals 0 to 1
    int gear_selected = kGearUnknown, gear_game = kGearUnknown;  // the shifter's position and the car's gear
};
void WheelInit(void* hwnd);  // with the game window (force feedback needs it): starts the wheel thread
void WheelSetup();           // the bindings again from app::Get(), after the settings window changed them
// the rig's console inputs, 0 to 1 each in Control order; false when no binding reads a device. steers: the steering
// is bound to a device that is plugged in
bool WheelRead(float* v, bool* steers = nullptr);
void WheelFeedback(const WheelFeel& feel, uint16_t rumble_left, uint16_t rumble_right);
WheelInfo Wheel();           // the settings window's view
void WheelTestForce(float level);  // -1 left to 1 right, for half a second

}  // namespace input
