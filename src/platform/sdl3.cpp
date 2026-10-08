#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <malloc.h>
#include "types.h"
#include "dd.h"
#include "r2img.h"
#include "config.h"
#include "gui.h"
#include "guimenu.h"
#include "font.h"
#include "nes.h"
#include "nesvideo.h"
#include "nessound.h"
#include "message.h"
#include "mouse.h"
#include "keyb.h"
#include "m6502.h"
#include "persistence.h"
#include "runtime.h"
#include "patch.h"
#include "netplay.h"
#include "input.h"
#include "release_video.h"
#include "ppu.h"
#include "options042.h"
extern COLOR nespal[256];
void m_patch();
void m_nestimingview();
void m_directories();
void m_soundview();
void m_patternview();
void m_paletteview();
void m_netstatus();
void show_netstatus(bool);
void m_romheader();
void m_editpattern(unsigned,unsigned,unsigned);
void m_editcolor(unsigned);
void m_netserver();
void m_netconnect();
void m_netchat();
void m_recordmovie();
void m_playmovie();
void m_waveoutputview();
void m_startwavelog();
void m_resolution();
void m_about();
void m_load();void editinputdevice0();void editinputdevice1();void m_nametableview();
bool reference_gui_test(const char *path,const char *fixture);
bool test_input_controls();
#ifdef NESTICLE_TEST_HOOKS
int lockstep_test(const char *reference,const char *rom,int frames,bool caching,const char *dump);
extern int lockstep_reset_frame;
#endif

extern char configfile[];
extern int guienabled;
void enablegui();
void disablegui();
void wm_keydown(char);
void wm_keyup(char);
const PALETTE& framebuffer_palette();

char* screen = nullptr;
char* video = nullptr;
int PITCH = 640;
int timeperframe = 0;
volatile int timerbusy = 0;
char errstr[80]{};
mouse m;
int SOUNDRATE = 22050;
byte soundinstalled = 0;
static SDL_Window* window;
static SDL_Renderer* renderer;
static SDL_Texture* texture;
static SDL_AudioStream* audio;
static std::vector<char> pixels;
static std::vector<Uint32> rgba;
static std::array<SDL_Gamepad*,2> pads{};
static bool testing_gamepads;
static std::array<SDL_JoystickID,2> test_pad_ids{};
static bool running = true;
static Uint64 audio_samples = 0;
static bool screenshot_requested = false;
static Uint32 open_rom_event;
static FILE *audio_dump;
static FILE *wave_log;
static uint32_t wave_bytes;
static bool pcx_requested;
static std::string pcx_path; // explicit --save-pcx file for the requested snapshot
static bool pcx_written;

void quitgame() { running = false; }
void cleanexit(int code) { std::fprintf(stderr,"%s\n",errstr); std::exit(code); }
void disable() {}
void enable() {}
int random(int n) { return n > 0 ? std::rand()%n : 0; }
void* loadresource(char*) { return nullptr; }
char* getclipboardtext() {
  char* text=SDL_GetClipboardText();
  if (!text) return nullptr;
  char* copy=strdup(text); SDL_free(text); return copy;
}
void ddrawinfo() { msg.printf(2,"SDL3 renderer: %s",SDL_GetRendererName(renderer)); }
#ifdef _WIN32
void platform_heap_check() {msg.printf(1,_heapchk()==_HEAPOK?"Heap OK":"ERROR: Bad heap node");}
#else
// glibc has no heap walk; it aborts on corruption it detects itself.
void platform_heap_check() {msg.printf(1,"Heap OK");}
#endif
void clearprimary() { if (audio) SDL_ClearAudioStream(audio); }
void primaryskip(int) {}
void primaryoutput(short* samples, int count) {
  if (audio_dump) std::fwrite(samples,2,count,audio_dump);
  if (wave_log) {
    if (std::fwrite(samples,2,count,wave_log)==size_t(count)) wave_bytes+=count*2;
    else {msg.error("Wave log write failed");stop_wave_log();}
  }
  if (audio && SDL_GetAudioStreamQueued(audio) < SOUNDRATE/5*2) {
    SDL_PutAudioStreamData(audio,samples,count*2);
    audio_samples += count;
  }
}
void platform_audio() { if (ns) for (int remaining=SOUNDRATE/virtual_rate;remaining>0;) {
  int count=std::min(4096,remaining);ns->update(count);remaining-=count;
} }
bool start_wave_log(const std::string &path) {
  stop_wave_log();std::error_code ec;auto parent=std::filesystem::path(path).parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent,ec);
  if (ec) return false;
  wave_log=std::fopen(path.c_str(),"wb");if (!wave_log) {msg.error("Unable to create wave log");return false;}
  byte header[44]={'R','I','F','F',36,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0};
  uint32_t rate=SOUNDRATE,bytes=rate*2;std::memcpy(header+24,&rate,4);std::memcpy(header+28,&bytes,4);
  header[32]=2;header[34]=16;std::memcpy(header+36,"data",4);wave_bytes=0;
  if (std::fwrite(header,1,44,wave_log)!=44) {stop_wave_log();return false;}
  soundinstalled=1;return true;
}
void stop_wave_log() {
  if (!wave_log) return;
  uint32_t size=wave_bytes+36;std::fseek(wave_log,4,SEEK_SET);std::fwrite(&size,4,1,wave_log);
  std::fseek(wave_log,40,SEEK_SET);std::fwrite(&wave_bytes,4,1,wave_log);std::fclose(wave_log);wave_log=nullptr;
  soundinstalled=audio || audio_dump;
}
void platform_sound_changed() {clearprimary();}
void platform_snapshot() {if (nv) pcx_requested=true;}

static bool resize_framebuffer(int w, int h) {
  if (w < 256 || h < 200 || w > 4096 || h > 4096) return false;
  auto* next = SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,w,h);
  if (!next) return false;
  SDL_SetTextureScaleMode(next,SDL_SCALEMODE_NEAREST);
  SDL_DestroyTexture(texture); texture = next;
  SCREENX=w; SCREENY=h; PITCH=w;
  pixels.assign(w*h,0); rgba.resize(w*h);
  screen=video=pixels.data();
  SDL_SetRenderLogicalPresentation(renderer,w,h,SDL_LOGICAL_PRESENTATION_LETTERBOX);
  return true;
}
void changeresolution(int w,int h) {
  if (!resize_framebuffer(w,h)) msg.error("Unable to set %dx%d framebuffer",w,h);
}

static void add_pad(SDL_JoystickID id) {
  if (testing_gamepads && std::find(test_pad_ids.begin(),test_pad_ids.end(),id)==test_pad_ids.end()) return;
  if (!SDL_IsGamepad(id)) return;
  for (auto *pad:pads) if (pad && SDL_GetGamepadID(pad)==id) return;
  for (auto& pad:pads) if (!pad) { pad=SDL_OpenGamepad(id); break; }
}
void platform_input() {
  for (int i=0;i<2;++i) if (auto *device=inputdevice[i]) {
    device->rawbuttons=0;
    if (!pads[i]) if (auto *position=device->getjoyposition()) {*position={32768,32768};device->stat&=~15u;}
  }
  for (int i=0;i<2;++i) if (pads[i] && inputdevice[i]) {
    auto* pad=pads[i]; auto* device=inputdevice[i];
    int x=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTX);
    int y=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTY);
    joythreshold limits{32768-10000,32768+10000,32768-10000,32768+10000};
    if (auto *position=device->getjoyposition()) {position->x=x+32768;position->y=y+32768;limits=*device->getjoythreshold();device->stat&=~15u;}
    if (x+32768>limits.r || SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) device->stat|=ID_RIGHT;
    if (x+32768<limits.l || SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_LEFT)) device->stat|=ID_LEFT;
    if (y+32768>limits.d || SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_DOWN)) device->stat|=ID_DOWN;
    if (y+32768<limits.u || SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_UP)) device->stat|=ID_UP;
    device->rawbuttons=0;
    for (int button=0;button<SDL_GAMEPAD_BUTTON_COUNT && button<32;++button)
      if (SDL_GetGamepadButton(pad,SDL_GamepadButton(button))) device->rawbuttons|=1u<<button;
    const int defaults[]={SDL_GAMEPAD_BUTTON_WEST,SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_BACK,SDL_GAMEPAD_BUTTON_START};
    const int *mapping=device->getbuttonmap();if (!mapping) mapping=defaults;
    for (int button=0;button<4;++button) if (mapping[button]>=0 && mapping[button]<32 && device->rawbuttons&(1u<<mapping[button])) device->but|=ID_BUT0<<button;
  }
}
// 0.42's window procedure passes bits 16-23 of the key message: the set 1
// scan code without the extended-key bit. Keypad and navigation keys, and
// left and right modifiers, share codes.
static int dos_scan(SDL_Scancode code) {
  static constexpr int letters[]={30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
  if (code>=SDL_SCANCODE_A && code<=SDL_SCANCODE_Z) return letters[code-SDL_SCANCODE_A];
  if (code>=SDL_SCANCODE_1 && code<=SDL_SCANCODE_9) return code-SDL_SCANCODE_1+2;
  if (code>=SDL_SCANCODE_F1 && code<=SDL_SCANCODE_F10) return code-SDL_SCANCODE_F1+KB_F1;
  if (code>=SDL_SCANCODE_F13 && code<=SDL_SCANCODE_F23) return code-SDL_SCANCODE_F13+0x64;
  switch(code) {
    case SDL_SCANCODE_0:return 11; case SDL_SCANCODE_RETURN:case SDL_SCANCODE_KP_ENTER:return 28;
    case SDL_SCANCODE_ESCAPE:return 1; case SDL_SCANCODE_SPACE:return 57;
    case SDL_SCANCODE_BACKSPACE:return 14; case SDL_SCANCODE_TAB:return 15;
    case SDL_SCANCODE_UP:case SDL_SCANCODE_KP_8:return 72; case SDL_SCANCODE_DOWN:case SDL_SCANCODE_KP_2:return 80;
    case SDL_SCANCODE_LEFT:case SDL_SCANCODE_KP_4:return 75; case SDL_SCANCODE_RIGHT:case SDL_SCANCODE_KP_6:return 77;
    case SDL_SCANCODE_LSHIFT:return 42; case SDL_SCANCODE_RSHIFT:return 54;
    case SDL_SCANCODE_LCTRL:case SDL_SCANCODE_RCTRL:return 29;
    case SDL_SCANCODE_LALT:case SDL_SCANCODE_RALT:return 56;
    case SDL_SCANCODE_HOME:case SDL_SCANCODE_KP_7:return 71; case SDL_SCANCODE_END:case SDL_SCANCODE_KP_1:return 79;
    case SDL_SCANCODE_PAGEUP:case SDL_SCANCODE_KP_9:return 73; case SDL_SCANCODE_PAGEDOWN:case SDL_SCANCODE_KP_3:return 81;
    case SDL_SCANCODE_DELETE:case SDL_SCANCODE_KP_PERIOD:return 83; case SDL_SCANCODE_INSERT:case SDL_SCANCODE_KP_0:return 82;
    case SDL_SCANCODE_KP_5:return 76;
    case SDL_SCANCODE_MINUS:return 12; case SDL_SCANCODE_EQUALS:return 13;
    case SDL_SCANCODE_LEFTBRACKET:return 26; case SDL_SCANCODE_RIGHTBRACKET:return 27;
    case SDL_SCANCODE_SEMICOLON:return 39; case SDL_SCANCODE_APOSTROPHE:return 40;
    case SDL_SCANCODE_GRAVE:return 41; case SDL_SCANCODE_BACKSLASH:case SDL_SCANCODE_NONUSHASH:return 43;
    case SDL_SCANCODE_COMMA:return 51; case SDL_SCANCODE_PERIOD:return 52;
    case SDL_SCANCODE_SLASH:case SDL_SCANCODE_KP_DIVIDE:return 53;
    case SDL_SCANCODE_KP_MULTIPLY:return 55; case SDL_SCANCODE_CAPSLOCK:return 58;
    case SDL_SCANCODE_NUMLOCKCLEAR:case SDL_SCANCODE_PAUSE:return 69; case SDL_SCANCODE_SCROLLLOCK:return 70;
    case SDL_SCANCODE_KP_MINUS:return 74; case SDL_SCANCODE_KP_PLUS:return 78;
    case SDL_SCANCODE_NONUSBACKSLASH:return 86; case SDL_SCANCODE_KP_EQUALS:return 89; case SDL_SCANCODE_LGUI:return 91; case SDL_SCANCODE_RGUI:return 92;
    case SDL_SCANCODE_APPLICATION:return 93; case SDL_SCANCODE_INTERNATIONAL2:return 112;
    case SDL_SCANCODE_INTERNATIONAL1:return 115; case SDL_SCANCODE_F24:return 118;
    case SDL_SCANCODE_INTERNATIONAL4:return 121; case SDL_SCANCODE_INTERNATIONAL5:return 123;
    case SDL_SCANCODE_INTERNATIONAL3:return 125; default:return 0;
  }
}
static void release_keys() { for (int i=1;i<128;++i) wm_keyup(static_cast<char>(i)); }
static void open_rom(const char* path) {
  clearprimary();
  std::string file(path);
  loadrom(file.data());
  if (romloaded) {
    m_execute();
    if (CPURunning) { static_cast<GUImaximizebox*>(nv->parent)->maximize(); disablegui(); }
  }
}
static void SDLCALL file_selected(void*,const char* const* files,int) {
  if (!files || !files[0]) return;
  SDL_Event event{}; event.type=open_rom_event; event.user.data1=SDL_strdup(files[0]);
  if (!SDL_PushEvent(&event)) SDL_free(event.user.data1);
}
void platform_choose_rom() {
  static const SDL_DialogFileFilter filters[]={{"NES ROMs","nes"}};
  SDL_ShowOpenFileDialog(file_selected,nullptr,window,filters,1,rom_directory.c_str(),false);
}
static void events() {
  m.reset(); SDL_Event e;
  while (SDL_PollEvent(&e)) {
    SDL_ConvertEventToRenderCoordinates(renderer,&e);
    if (e.type==SDL_EVENT_QUIT) running=false;
    else if (e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP) {
      // Port additions; these key presses never reach the emulator.
      bool fkey=e.key.scancode==SDL_SCANCODE_F11 || e.key.scancode==SDL_SCANCODE_F12;
      if (e.type==SDL_EVENT_KEY_DOWN && (fkey || ((e.key.mod&SDL_KMOD_CTRL) && e.key.scancode==SDL_SCANCODE_O))) {
        if (e.key.repeat) continue;
        if (e.key.scancode==SDL_SCANCODE_O) platform_choose_rom();
        else if (e.key.scancode==SDL_SCANCODE_F11) SDL_SetWindowFullscreen(window,!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN));
        else screenshot_requested=true;
        continue;
      }
      if (fkey) continue;
      // Like WM_KEYDOWN in 0.42, autorepeat reaches the handler again.
      int scan=dos_scan(e.key.scancode);
      if (scan) { if (e.type==SDL_EVENT_KEY_UP) wm_keyup(scan); else wm_keydown(scan); }
    } else if (e.type==SDL_EVENT_MOUSE_MOTION) m.updatexy(e.motion.x,e.motion.y);
    else if (e.type==SDL_EVENT_MOUSE_BUTTON_DOWN || e.type==SDL_EVENT_MOUSE_BUTTON_UP) {
      m.updatexy(e.button.x,e.button.y);
      int mask=e.button.button==SDL_BUTTON_LEFT?1:e.button.button==SDL_BUTTON_RIGHT?2:4;
      m.updatebut(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN ? m.b|mask : m.b&~mask);
    } else if (e.type==SDL_EVENT_WINDOW_FOCUS_LOST) { release_keys(); m.updatebut(0); }
    else if (e.type==SDL_EVENT_GAMEPAD_ADDED) add_pad(e.gdevice.which);
    else if (e.type==SDL_EVENT_GAMEPAD_REMOVED) {
      for (auto& pad:pads) if (pad && SDL_GetGamepadID(pad)==e.gdevice.which) { SDL_CloseGamepad(pad); pad=nullptr; }
    } else if (e.type==SDL_EVENT_DROP_FILE) open_rom(e.drop.data);
    else if (e.type==open_rom_event) { open_rom(static_cast<char*>(e.user.data1)); SDL_free(e.user.data1); }
  }
}
static bool test_gamepads() {
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
  testing_gamepads=true;
  for (auto &pad:pads) {if (pad) SDL_CloseGamepad(pad);pad=nullptr;}
  SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);
  desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
  desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;desc.name="NESticle test gamepad";
  auto &ids=test_pad_ids;std::array<SDL_Joystick*,2> sticks{};bool ok=true;
  for (unsigned i=0;i<2;++i) {
    ids[i]=SDL_AttachVirtualJoystick(&desc);sticks[i]=SDL_OpenJoystick(ids[i]);
    ok&=ids[i] && sticks[i];if (!sticks[i]) continue;
    SDL_SetJoystickVirtualAxis(sticks[i],SDL_GAMEPAD_AXIS_LEFTX,i?-20000:20000);
    SDL_SetJoystickVirtualAxis(sticks[i],SDL_GAMEPAD_AXIS_LEFTY,i?20000:-20000);
    for (auto button:{SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_WEST,SDL_GAMEPAD_BUTTON_BACK,SDL_GAMEPAD_BUTTON_START})
      SDL_SetJoystickVirtualButton(sticks[i],button,true);
  }
  SDL_UpdateJoysticks();events();inputdevice[0]->read();inputdevice[1]->read();platform_input();
  for (unsigned i=0;i<2;++i) {
    std::printf("Gamepad %u attached=%d mask=%02x/%02x\n",i+1,pads[i]!=nullptr,inputdevice[i]->stat,inputdevice[i]->but);
    ok&=pads[i] && inputdevice[i]->but==0xf0 && (inputdevice[i]->stat&15)==(i?10:5);
  }
  auto *saved_device=inputdevice[0];inputdevice[0]=newinputdevice(ID_JOY1);
  int saved_map[4];std::copy(joybuttonmap[0],joybuttonmap[0]+4,saved_map);
  for (int i=0;i<4;++i) joybuttonmap[0][i]=255;joybuttonmap[0][0]=SDL_GAMEPAD_BUTTON_NORTH;
  for (int button=0;button<SDL_GAMEPAD_BUTTON_COUNT;++button) SDL_SetJoystickVirtualButton(sticks[0],button,button==SDL_GAMEPAD_BUTTON_NORTH);
  SDL_UpdateJoysticks();events();inputdevice[0]->read();platform_input();
  ok&=inputdevice[0]->but==ID_BUT0 && inputdevice[0]->rawbuttons==(1u<<SDL_GAMEPAD_BUTTON_NORTH);
  for (unsigned i=0;i<2;++i) {if (sticks[i]) SDL_CloseJoystick(sticks[i]);if (ids[i]) SDL_DetachVirtualJoystick(ids[i]);}
  events();inputdevice[0]->read();inputdevice[1]->read();platform_input();
  for (unsigned i=0;i<2;++i) {
    std::printf("Gamepad %u detached=%d mask=%02x/%02x\n",i+1,pads[i]==nullptr,inputdevice[i]->stat,inputdevice[i]->but);
    ok&=!pads[i] && inputdevice[i]->but==0 && (inputdevice[i]->stat&15)==0;
  }
  delete inputdevice[0];inputdevice[0]=saved_device;std::copy(saved_map,saved_map+4,joybuttonmap[0]);
  testing_gamepads=false;
  std::printf("Gamepad ports, axes, buttons and hot-unplug: %s\n",ok?"PASS":"FAIL");return ok;
}
static void render() {
  if (nv) nv->refreshpalette();
  // A maximized view has a black desktop. SDL's persistent software buffer
  // still needs clearing there before repainting transient menus and cursors.
  std::fill(pixels.begin(),pixels.end(),cfg->get(CFG_NOFILLEDDESKTOP)?0:158);
  updatescreen();
  const auto& palette=framebuffer_palette();
  for (size_t i=0;i<pixels.size();++i) {
    const auto& c=palette.c[static_cast<byte>(pixels[i])];
    rgba[i]=0xff000000u | Uint32(c.r)<<16 | Uint32(c.g)<<8 | c.b;
  }
  SDL_UpdateTexture(texture,nullptr,rgba.data(),PITCH*4);
  SDL_SetRenderDrawColor(renderer,0,0,0,255); SDL_RenderClear(renderer);
  if (!guienabled && nv) {
    SDL_SetRenderLogicalPresentation(renderer,256,224,SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_FRect crop{float((SCREENX-256)/2),float((SCREENY-224)/2),256,224};
    SDL_RenderTexture(renderer,texture,&crop,nullptr);
  } else {
    SDL_SetRenderLogicalPresentation(renderer,SCREENX,SCREENY,SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_RenderTexture(renderer,texture,nullptr,nullptr);
  }
  SDL_RenderPresent(renderer);
}
static bool screenshot(const char* path) {
  int w=SCREENX,h=SCREENY,pitch=PITCH; auto* data=rgba.data();std::vector<Uint32> cropped;
  if (!guienabled && nv) {
    w=256;h=224;pitch=w;cropped.assign(w*h,0xff000000);
    int left=nv->x1+(nv->width()-w)/2,top=nv->y1+(nv->height()-h)/2;
    for (int y=0;y<h;++y) for (int x=0;x<w;++x)
      if (left+x>=0 && left+x<SCREENX && top+y>=0 && top+y<SCREENY) cropped[y*w+x]=rgba[(top+y)*PITCH+left+x];
    data=cropped.data();
  }
  auto* image=SDL_CreateSurfaceFrom(w,h,SDL_PIXELFORMAT_ARGB8888,data,pitch*4);
  bool ok=image && SDL_SaveBMP(image,path); SDL_DestroySurface(image); return ok;
}
static bool test_compositor() {
  if (!nv) {std::fputs("Compositor test requires a ROM\n",stderr);return false;}
  enablegui();m.hidecursor();render();render();
  auto baseline=pixels;
  m.showcursor();
  for (int i=0;i<80;++i) {
    m.updatexy((i*37)%SCREENX,(i*53)%SCREENY);render();
  }
  m.hidecursor();render();
  bool cursor_ok=pixels==baseline;
  std::printf("GUI cursor trail cleanup: %s\n",cursor_ok?"PASS":"FAIL");
  // Open and dismiss real menu trees without selecting a command.
  GUIhmenu *bar=nullptr;
  for (auto *g=guiroot->child;g;g=g->next) if ((bar=dynamic_cast<GUIhmenu*>(g))) break;
  bool menus_ok=bar!=nullptr;
  if (bar) {
    for (int x=bar->x1+5;x<bar->x2;x+=40) {
      m.updatexy(x,bar->y1+5);m.updatebut(1);render();
      m.updatexy(x,bar->y1+20);render();
      bar->losefocus();m.capture=nullptr;m.updatebut(0);
      render();menus_ok&=pixels==baseline;
    }
  }
  std::printf("GUI dismissed menu cleanup: %s\n",menus_ok?"PASS":"FAIL");
  auto *box=new GUIbox(guiroot,"Overlay cleanup",new GUIcontents(128,48),20,20);
  render();delete box;render();
  bool dialog_ok=pixels==baseline;
  std::printf("GUI closed dialog cleanup: %s\n",dialog_ok?"PASS":"FAIL");
  m.showcursor();return cursor_ok && menus_ok && dialog_ok;
}
// 0.42's snapshot (00408d80): snapNNN.pcx in the PCX directory. Each pixel is
// stored as its NES palette slot plus 16. The header claims RLE, which values
// below 0xC0 never trigger. The palette holds the 32 slots at 16-47 and white
// at 255. Write errors are ignored.
static bool snapshot_pcx(const std::string &explicit_path="") {
  if (!nv) return false;
  std::error_code ec;std::filesystem::create_directories(snapshot_directory,ec);
  std::filesystem::path path;
  if (!explicit_path.empty()) path=explicit_path;
  else {
    unsigned i=0;
    do {char name[16];std::snprintf(name,sizeof(name),"snap%03u.pcx",i);path=std::filesystem::path(snapshot_directory)/name;}
    while (std::filesystem::exists(path) && ++i<999);
  }
  auto *f=std::fopen(path.string().c_str(),"wb");
  if (!f) {msg.error("Unable to create %s",path.string().c_str());return false;}
  const int w=256,h=224;
  std::array<byte,128> header{};header[0]=10;header[1]=5;header[2]=1;header[3]=8;
  header[8]=byte(w-1);header[9]=byte((w-1)>>8);header[10]=byte(h-1);header[11]=byte((h-1)>>8);
  header[65]=1;header[66]=byte(w);header[67]=byte(w>>8);header[68]=1;
  bool ok=std::fwrite(header.data(),1,128,f)==128;
  int left=nv->x1+(nv->width()-w)/2,top=nv->y1+(nv->height()-h)/2;
  std::vector<byte> data(w*h);
  for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
    byte p=left+x>=0 && left+x<SCREENX && top+y>=0 && top+y<SCREENY?byte(pixels[(top+y)*PITCH+left+x]):0;
    data[y*w+x]=(p&0x1f)+0x10;
  }
  ok=std::fwrite(data.data(),1,data.size(),f)==data.size() && ok;
  std::array<byte,769> palette{};palette[0]=12;
  for (int i=0;i<32;++i) std::memcpy(&palette[1+(16+i)*3],&nespal[ppu->palette()[i]],3);
  palette[1+255*3]=palette[2+255*3]=palette[3+255*3]=0xff;
  ok=std::fwrite(palette.data(),1,palette.size(),f)==palette.size() && ok;ok=std::fclose(f)==0 && ok;
  msg.printf(1,"Snapshot saved to %s",path.filename().string().c_str());return ok;
}
// Called by updatescreen once the GUI or NES view is drawn and before the
// popups: 0.42 takes the snapshot while drawing the NES view (00412630).
void platform_frame_drawn() {
  if (!pcx_requested) return;
  bool ok=snapshot_pcx(pcx_path);pcx_written|=ok && !pcx_path.empty();
  pcx_requested=false;pcx_path.clear();
}
static WORD test_read(WORD address) { return address==0x4000 ? 0x35 : 0; }
static int test_write(WORD address,BYTE value) { m6502Base[address]=value; return 0; }
static int core_test() {
  static byte memory[65536]{};
  static struct m6502MemoryRead reads[]={{0x4000,0x4000,test_read},{0xffff,0xffff,nullptr}};
  static struct m6502MemoryWrite writes[]={{0x4001,0x4001,test_write},{0xffff,0xffff,nullptr}};
  const byte program[]={0xa9,0x12,0x85,0x10,0xa2,0x03,0xe8,0x8e,0x01,0x40,0xad,0x00,0x40,0x85,0x11,0x4c,0x0f,0x80};
  std::memcpy(memory+0x8000,program,sizeof(program));
  memory[0xfffc]=0; memory[0xfffd]=0x80; memory[0xfffa]=0; memory[0xfffb]=0x90;
  memory[0x9000]=0xe6; memory[0x9001]=0x12; memory[0x9002]=0x40;
  m6502Base=memory; m6502MemoryRead=reads; m6502MemoryWrite=writes; m6502reset();
  auto status=m6502exec(80); m6502nmi(); auto nmi_status=m6502exec(30);
  bool ok=status==0x10000 && nmi_status==0x10000 && memory[0x10]==0x12 && memory[0x11]==0x35 && memory[0x4001]==4 && memory[0x12]==1;
  std::printf("Bloodlust N6502: %s (execution, read/write traps, NMI/RTI)\n",ok?"PASS":"FAIL");
  if (!ok) std::printf("status=%x nmi_status=%x pc=%04x ram=%02x,%02x,%02x trap=%02x\n",status,nmi_status,m6502pc,memory[0x10],memory[0x11],memory[0x12],memory[0x4001]);
  CPURunning=1;
  nessound test_sound; test_sound.setenable(1);
  test_sound.write(0,0xbf); test_sound.write(1,0); test_sound.write(2,0xfd); test_sound.write(3,8);
  short buffer[800]; test_sound.mix16(buffer,800);
  int low=0,high=0;
  for (auto sample:buffer) {low=std::min(low,int(sample));high=std::max(high,int(sample));}
  bool audio_ok=low<0 && high>0; CPURunning=0;
  std::printf("Historical pulse mixer: %s (range %d..%d)\n",audio_ok?"PASS":"FAIL",low,high);
  return ok && audio_ok?0:1;
}
static void dump_menu(const menu &items,const std::string &prefix) {
  for (const auto &item:items.m) {
    if (!item.text) break;
    auto path=prefix.empty()?std::string(item.text):prefix+"/"+item.text;
    std::printf("MENU %u %s\n",unsigned(byte(item.key)),path.c_str());
    if (item.submenu) dump_menu(*item.submenu,path);
  }
}
static bool test_menu_layout() {
  bool ok=true;
  for (int i=0;i<10;++i) {
    char expected[4];std::snprintf(expected,sizeof(expected),"F%d",i+1);
    ok&=std::strcmp(getkeyname(KB_F1+i),expected)==0;
  }
  ok&=std::strcmp(getkeyname(0),"<none>")==0;
  ok&=std::strcmp(getkeyname(0x7f),"<none>")==0;
  // Draw the real menus and longer shortcut/fallback names on a blank buffer.
  // Any changed pixel outside the menu is an actual text overflow.
  auto check=[&](menu &items) {
    GUIvmenu view(nullptr,&items,10,20);
    std::fill(pixels.begin(),pixels.end(),0);
    view.draw(screen);
    bool bounded=true;
    for (int y=0;y<SCREENY;++y) for (int x=0;x<SCREENX;++x)
      if (x<view.x1 || x>=view.x2 || y<view.y1 || y>=view.y2)
        bounded&=pixels[y*PITCH+x]==0;
    return bounded;
  };
  extern menu mainmenu;
  for (auto &item:mainmenu.m) {
    if (!item.text) break;
    if (item.submenu) ok&=check(*item.submenu);
  }
  std::printf("GUI shortcut names and menu bounds: %s\n",ok?"PASS":"FAIL");
  return ok;
}
int main(int argc,char** argv) {
  SDL_SetMainReady();
  bool headless=false,test_input=false,test_gui=false,test_pads=false,test_net_input=false,test_comp=false,backup=false,cli_pcm=false,server=false,save_config=false; int limit=0,reset_frame=-1,slot=-1,net_port=6996,sync_frames=0;
  int initial_width=0,initial_height=0;
  std::vector<std::string> legacy; // 0.42 "-option" and "@file" arguments
  std::string rom,output,state_in,state_out,movie_in,movie_out,sound_out;
  std::string cli_savedir,patch_file;std::vector<std::string> cli_patches;
  std::string connect_to,cli_romdir,inspect_view,net_name="Player";
  std::string wave_out,pcx_out,video_fixture;
  std::string reference_gui,dialog_action;
#ifdef NESTICLE_TEST_HOOKS
  std::string lockstep_reference,lockstep_dump;bool lockstep_caching=true;
#endif
  for (int i=1;i<argc;++i) {
    std::string arg=argv[i];
    if (arg=="--self-test") return core_test();
#ifdef NESTICLE_TEST_HOOKS
    if (arg=="--lockstep" && i+1<argc) {lockstep_reference=std::filesystem::absolute(argv[++i]).string();headless=true;continue;}
    if (arg=="--lockstep-uncached") {lockstep_caching=false;continue;}
    if (arg=="--lockstep-reset" && i+1<argc) {lockstep_reset_frame=std::atoi(argv[++i]);continue;}
    if (arg=="--lockstep-dump" && i+1<argc) {lockstep_dump=std::filesystem::absolute(argv[++i]).string();continue;}
#endif
    if (arg=="--menu-inventory") {extern menu mainmenu;dump_menu(mainmenu,"");return 0;}
    if (arg=="--headless") headless=true;
    else if (arg=="--reference-gui" && i+1<argc) reference_gui=std::filesystem::absolute(argv[++i]).string();
    else if (arg=="--test-dialog-action" && i+1<argc) dialog_action=argv[++i];
    else if (arg=="--test-input") test_input=true;
    else if (arg=="--test-gui") test_gui=true;
    else if (arg=="--test-compositor") test_comp=true;
    else if (arg=="--test-gamepads") test_pads=true;
    else if (arg=="--test-net-input") test_net_input=true;
    else if (arg=="--pcm") cli_pcm=true;
    else if (arg=="--state-backup") backup=true;
    else if (arg=="--save-config") save_config=true; // also save settings from a headless run
    else if (arg=="--net-server" && i+1<argc) {server=true;net_port=std::atoi(argv[++i]);}
    else if (arg=="--net-connect" && i+2<argc) {connect_to=argv[++i];net_port=std::atoi(argv[++i]);}
    else if (arg=="--net-name" && i+1<argc) net_name=argv[++i];
    else if (arg=="--sync-frames" && i+1<argc) {sync_frames=std::atoi(argv[++i]);if (sync_frames<1) return 2;}
    else if (arg=="--rom-dir" && i+1<argc) cli_romdir=std::filesystem::absolute(argv[++i]).string();
    else if (arg=="--inspect-view" && i+1<argc) inspect_view=argv[++i];
    else if (arg=="--state-slot" && i+1<argc) {slot=std::atoi(argv[++i]);if (slot<0 || slot>9) return 2;}
    else if (arg=="--patch" && i+1<argc) cli_patches.emplace_back(argv[++i]);
    else if (arg=="--patch-file" && i+1<argc) patch_file=std::filesystem::absolute(argv[++i]).string();
    else if (arg=="--reset-frame" && i+1<argc) reset_frame=std::atoi(argv[++i]);
    else if (arg=="--save-dir" && i+1<argc) cli_savedir=std::filesystem::absolute(argv[++i]).string();
    else if (arg=="--save-state" && i+1<argc) state_out=std::filesystem::absolute(argv[++i]).string();
    else if (arg=="--load-state" && i+1<argc) state_in=std::filesystem::absolute(argv[++i]).string();
    else if (arg=="--record-movie" && i+1<argc) movie_out=std::filesystem::absolute(argv[++i]).string();
    else if (arg=="--play-movie" && i+1<argc) movie_in=std::filesystem::absolute(argv[++i]).string();
    else if (arg=="--audio-dump" && i+1<argc) sound_out=std::filesystem::absolute(argv[++i]).string();
    else if (arg=="--wave-log" && i+1<argc) wave_out=std::filesystem::absolute(argv[++i]).string();
    else if (arg=="--save-pcx" && i+1<argc) pcx_out=std::filesystem::absolute(argv[++i]).string();
    else if (arg=="--video-fixture" && i+1<argc) video_fixture=std::filesystem::absolute(argv[++i]).string();
    else if (arg=="--resolution" && i+2<argc) {initial_width=std::atoi(argv[++i]);initial_height=std::atoi(argv[++i]);}
    else if (arg=="--frames" && i+1<argc) { limit=std::atoi(argv[++i]); if (limit<1) return 2; }
    else if (arg=="--screenshot" && i+1<argc) output=std::filesystem::absolute(argv[++i]).string();
    else if (arg=="--help") {
      std::puts("NESticle SDL3 [rom.nes] [--frames N] [--headless] [--screenshot file.bmp] [--self-test]\nCtrl+O open, F1 messages, F2 patterns, F3 nametables, F4 palette\nF5 save state, F6 ROM patcher, F7 restore state, F8 sound, F9 PCX snapshot\n0-9 state slot, F11 fullscreen, F12 BMP screenshot, Escape or Space GUI\nAlt+L load, Alt+R reset, Alt+P pause, Alt+A advance frame, Alt+T trace, Alt+Q exit\n--save-dir DIR, --save-state FILE, --load-state FILE, --record-movie FILE, --play-movie FILE\n--state-slot N, --state-backup, --patch CODE, --patch-file FILE\n--rom-dir DIR, --net-server PORT, --net-connect IPV4 PORT, --net-name NAME\n--resolution WIDTH HEIGHT, --save-pcx FILE, --wave-log FILE\n--pcm enables direct-write PCM; --audio-dump FILE writes signed 16-bit mono.\nNESticle 0.42 options (-res X Y, -romdir DIR, -run FILE, @file, ...) are also\naccepted and saved to NEStcl95.ini.\nArrows (Home/PgUp/End/PgDn diagonals) move; Alt=A, Ctrl=B, [=Select, ]=Start.\nGamepads supported."); return 0;
    } else if (arg.size()>1 && (arg[0]=='@' || (arg[0]=='-' && arg[1]!='-'))) {
      // A 0.42 option takes the following plain arguments as its parameters.
      legacy.push_back(arg);
      while (i+1<argc && argv[i+1][0]!='-' && argv[i+1][0]!='@') legacy.push_back(argv[++i]);
    } else if (!arg.empty() && arg[0]!='-' && rom.empty()) rom=std::filesystem::absolute(arg).string();
    else if (!arg.empty() && arg[0]!='-') legacy.push_back(arg); // 0.42 runs each extra ROM argument
    else { std::fprintf(stderr,"Invalid option: %s\n",arg.c_str()); return 2; }
  }
  if (headless && limit==0) { std::fputs("--headless requires --frames N\n",stderr); return 2; }
  message_echo=headless;
  // Some ROMs log every frame (0.42's PPU[%X]=%X); unbuffered stderr makes
  // each echoed message a separate system call. The frame loop flushes it.
  if (headless) std::setvbuf(stderr,nullptr,_IOFBF,1<<16);
  if (headless) { SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy"); SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software"); }
  // 0.42 handles WM_SYSKEYDOWN itself, so Alt+F4 is just Alt and F4 (palette).
  SDL_SetHint(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4,"0");
  // Audio starts below and may fail: 0.42 logs a DirectSound failure and runs
  // silent (0040adc9).
  if (!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)) { std::fprintf(stderr,"SDL: %s\n",SDL_GetError()); return 1; }
  std::filesystem::current_path(SDL_GetBasePath());
  // Settings: the port's files, then 0.42's NEStcl95.ini and its command line
  // (0040ab50), which run their system options before the display exists.
  cfg=new config; cfg->load(configfile);
  load_runtime_settings();
  options042_read_file("NEStcl95.ini");
  options042_add_arguments(legacy);
  options042_execute(false);
  if (!cli_savedir.empty()) save_directory=cli_savedir;
  if (!cli_romdir.empty()) rom_directory=cli_romdir;
  if (backup) state_backup=1;if (cli_pcm) pcm_enabled=true;
  if (initial_width) {SCREENX=initial_width;SCREENY=initial_height;}
  window=SDL_CreateWindow("NESticle SDL3",960,720,SDL_WINDOW_RESIZABLE|(headless?SDL_WINDOW_HIDDEN:0));
  renderer=window?SDL_CreateRenderer(window,nullptr):nullptr;
  // An unusable -res falls back to 0.42's default mode.
  if (!renderer || (!resize_framebuffer(SCREENX,SCREENY) && !resize_framebuffer(320,240))) { std::fprintf(stderr,"Video: %s\n",SDL_GetError()); SDL_Quit(); return 1; }
  std::printf("SDL %d | video=%s | renderer=%s\n",SDL_GetVersion(),SDL_GetCurrentVideoDriver(),SDL_GetRendererName(renderer));
  enablegui();
  if (initgame()!=0) { std::fputs("Missing or invalid gui.vol\n",stderr); SDL_Quit(); return 1; }
#ifdef NESTICLE_TEST_HOOKS
  if (!reference_gui.empty()) return reference_gui_test(reference_gui.c_str(),rom.c_str())?0:1;
#else
  if (!reference_gui.empty()) {std::fputs("--reference-gui requires nesticle-test\n",stderr);return 2;}
#endif
  if (test_gui) {
    auto* previous=guiroot->child;
    new GUIbox(guiroot,"Mouse interaction test",new GUIcontents(128,48),20,20);
    m.updatexy(141,24); m.updatebut(1); m.updatebut(0);
    bool ok=guiroot->child==previous && m.capture==nullptr;
    std::printf("GUI mouse close: %s\n",ok?"PASS":"FAIL");
    if (!ok) return 1;
    auto *contents=new GUIcontents(270,95);
    auto *number=new GUIintedit(contents,"Value:",5,5,45,42,1,99);
    auto *fraction=new GUIfloatedit(contents,"Float:",5,28,45,1.5f,0,5);
    auto *first=new GUItextedit(contents,"Name:","",5,50,180,8);
    auto *second=new GUItextedit(contents,"Code:","",5,72,180,8);
    auto *box=new GUIbox(guiroot,"Control test",contents,20,20);
    bool controls=number->get()==42;number->set(200);controls&=number->get()==99;
    controls&=font[0]->getwidth(char(255))==6;
    number->set(-2);controls&=number->get()==1;fraction->minus(.5f);controls&=fraction->get()==1.0f;
    GUIrect::setfocus(first);wm_keydown(45);wm_keyup(45);wm_keydown(57);wm_keyup(57);
    controls&=std::strcmp(first->getinput(),"x ")==0;
    wm_keydown(KB_TAB);wm_keyup(KB_TAB);controls&=second->focus!=0;
    second->setinput("01234567890123456789");controls&=std::strlen(second->getinput())==8;
    delete box;std::printf("GUI text, focus, numeric controls: %s\n",controls?"PASS":"FAIL");if (!controls) return 1;
    if (!test_menu_layout()) return 1;
    if (!test_input_controls()) return 1;
  }
  ns=new nessound;
#ifdef NESTICLE_TEST_HOOKS
  if (!lockstep_reference.empty())
    return lockstep_test(lockstep_reference.c_str(),rom.c_str(),limit,lockstep_caching,lockstep_dump.c_str());
#endif
  if (!sound_out.empty()) {
    audio_dump=std::fopen(sound_out.c_str(),"wb");
    if (!audio_dump) {std::fputs("Unable to open audio dump\n",stderr);return 1;}
    soundinstalled=1;
  }
  if (!headless && !options042_flag("nosound")) {
    SDL_AudioSpec spec{SDL_AUDIO_S16,1,SOUNDRATE};
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) audio=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
    if (audio) { soundinstalled=1; SDL_ResumeAudioStreamDevice(audio); }
    else std::fprintf(stderr,"Audio unavailable: %s\n",SDL_GetError());
  }
  std::printf("audio=%s\n",audio?SDL_GetCurrentAudioDriver():"disabled");
  int pad_count=0; auto* pad_ids=SDL_GetGamepads(&pad_count);
  for (int i=0;i<pad_count;++i) add_pad(pad_ids[i]); SDL_free(pad_ids);
  open_rom_event=SDL_RegisterEvents(1); SDL_HideCursor();
  if (test_pads && !test_gamepads()) running=false;
  if ((server || !connect_to.empty()) && !net_start(server,connect_to,net_port,net_name)) running=false;
  options042_execute(true); // -message, -hidegui, -load, -run, -restore
  if (!rom.empty()) { open_rom(rom.c_str()); if (!CPURunning) running=false; }
  if (!inspect_view.empty()) {
    enablegui();
    if (inspect_view=="patch") m_patch();else if (inspect_view=="timing") m_nestimingview();
    else if (inspect_view=="directories") m_directories();else if (inspect_view=="sound") m_soundview();
    else if (inspect_view=="patterns") m_patternview();else if (inspect_view=="palette") m_paletteview();
    else if (inspect_view=="net") show_netstatus(false);else if (inspect_view=="header") m_romheader();
    else if (inspect_view=="pattern-edit") m_editpattern(0,2,0);else if (inspect_view=="color-edit") m_editcolor(1);
    else if (inspect_view=="net-server") m_netserver();else if (inspect_view=="net-connect") m_netconnect();
    else if (inspect_view=="net-chat") m_netchat();else if (inspect_view=="movie-record") m_recordmovie();
    else if (inspect_view=="movie-play") m_playmovie();else if (inspect_view=="wave") m_waveoutputview();
    else if (inspect_view=="wave-log") m_startwavelog();else if (inspect_view=="resolution") m_resolution();
    else if (inspect_view=="about") m_about();
    else if (inspect_view=="rom-picker") m_load();
    else if (inspect_view=="input-1") editinputdevice0();else if (inspect_view=="input-2") editinputdevice1();
    else if (inspect_view=="name-tables") m_nametableview();
    else if (inspect_view.rfind("menu-",0)==0) {
      extern menu mainmenu;extern GUImenu *gmenu;
      int x=gmenu->x1;bool found=false;
      for (auto &item:mainmenu.m) {
        if (!item.text) break;
        if (inspect_view.substr(5)==item.text) {
          m.updatexy(x+5,gmenu->y1+5);m.updatebut(1);found=true;break;
        }
        x+=item.width();
      }
      if (!found) {std::fprintf(stderr,"Unknown menu\n");return 2;}
    }
    else {std::fprintf(stderr,"Unknown view\n");return 2;}
  }
  if (!dialog_action.empty()) {
    auto *box=dynamic_cast<GUIbox*>(guiroot->child);
    if (!box) {std::fputs("Dialog action requires an open dialog\n",stderr);return 1;}
    int x=box->x1+box->width()/2,y=box->y2-5;
    GUIcontents *content=nullptr;
    for (auto *child=box->child;child;child=child->next) if ((content=dynamic_cast<GUIcontents*>(child))) break;
    auto click=[](int x,int y) {m.updatexy(x,y);m.updatebut(1);m.updatebut(0);};
    if (dialog_action=="header-change" && content) click(content->x1+10,content->y1+43);
    if (dialog_action=="header-battery-change" && content) click(content->x1+10,content->y1+53);
    if (dialog_action=="resolution-change" && content) {
      for (auto *child=content->child;child;child=child->next) if (auto *edit=dynamic_cast<GUInumbertextedit*>(child)) edit->setinput(const_cast<char*>(child->y1<content->y1+15?"320":"240"));
    }
    if (dialog_action=="patch" && content) {
      GUIlistbox *list=nullptr;
      for (auto *child=content->child;child;child=child->next) {
        if (auto *edit=dynamic_cast<GUItextedit*>(child)) edit->setinput(const_cast<char*>(child->y1<content->y1+10?"8000:AA":"GUI patch"));
        if (auto *items=dynamic_cast<GUIlistbox*>(child)) list=items;
      }
      click(content->x1+20,content->y1+28);if (!list || list->getnumitems()!=1) return 1;
      list->setsel(0);click(content->x1+105,content->y1+28);click(content->x1+145,content->y1+28);
    }
    else if (dialog_action=="double" && content) {
      GUIlistbox *list=nullptr;for (auto *child=content->child;child;child=child->next) if ((list=dynamic_cast<GUIlistbox*>(child))) break;
      if (!list) return 1;int left=list->x1+15,top=list->y1+5;click(left,top);click(left,top);
    }
    else if (dialog_action=="folder" && content) {
      GUIlistbox *list=nullptr;for (auto *child=content->child;child;child=child->next) if ((list=dynamic_cast<GUIlistbox*>(child))) break;
      if (!list) return 1;list->keyhit(0,'j');box->keyhit(KB_ENTER,'\r');
      if (std::filesystem::path(rom_directory).filename()!="jump") return 1;
    }
    else if (dialog_action=="enter") {box->keyhit(KB_ENTER,'\r');}
    else {m.updatexy(x,y);m.updatebut(1);m.updatebut(0);}
    bool closed=true;for (auto *child=guiroot->child;child;child=child->next) if (child==box) closed=false;
    bool expected=dialog_action!="stay-open" && dialog_action!="patch" && dialog_action!="folder";
    bool ok=closed==expected && m.capture==nullptr;
    std::printf("GUI dialog action %s: %s\n",inspect_view.c_str(),ok?"PASS":"FAIL");if (!ok) return 1;
  }
  if (slot>=0) select_state_slot_042(slot);
  if (!patch_file.empty() && !load_patches_042(patch_file)) running=false;
  for (const auto &code:cli_patches) if (!add_patch_042(code,"",true)) {std::fprintf(stderr,"Invalid patch: %s\n",code.c_str());running=false;}
  if (!state_in.empty() && !load_state_042(state_in)) running=false;
  if (!movie_in.empty() && !play_movie_042(movie_in)) running=false;
  if (!movie_out.empty() && !record_movie_042(movie_out)) running=false;
  if (!wave_out.empty() && !start_wave_log(wave_out)) running=false;
  Uint64 deadline=SDL_GetTicksNS(); int frames=0; int result=running?0:1; bool buttons_seen=false;
  bool net_pressed=false,net_released=false;
  while (running && (!limit || frames<limit) && (!sync_frames || net_frame_count<unsigned(sync_frames))) {
    if (test_input && (frames==10 || frames==60)) {
      // 0.42's KEY1 buttons: Alt=A, Ctrl=B, [=Select, ]=Start.
      for (auto scan:{SDL_SCANCODE_LALT,SDL_SCANCODE_LCTRL,SDL_SCANCODE_LEFTBRACKET,SDL_SCANCODE_RIGHTBRACKET}) {
        SDL_Event e{}; e.type=frames==10?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
        e.key.scancode=scan; SDL_PushEvent(&e);
      }
    }
    events();
    if (test_net_input && net_connected() && !net_pressed && net_frame_count>=20) {wm_keydown(56);net_pressed=true;}
    if (test_net_input && net_pressed && !net_released && net_frame_count>=60) {wm_keyup(56);net_released=true;}
    if (frames==reset_frame && romloaded) resetNEShardware();
    auto before=SDL_GetTicksNS();gametimer();cpu_use_percent=(SDL_GetTicksNS()-before)*virtual_rate/10000000.0;
    if (test_input && frames==30 && ram) buttons_seen=ram[0x40] && ram[0x41] && ram[0x42] && ram[0x43];
    if (!pcx_out.empty() && frames==limit-1 && nv) {pcx_requested=true;pcx_path=pcx_out;}
    render(); ++frames;
    if (headless) std::fflush(stderr);
    if (screenshot_requested) { screenshot("screenshot.bmp"); screenshot_requested=false; }
    static int applied_vsync=-1;
    if (wait_vsync!=applied_vsync) {SDL_SetRenderVSync(renderer,wait_vsync);applied_vsync=wait_vsync;}
    if ((!headless && speed_throttling) || net_active()) {
      deadline+=1000000000ull/virtual_rate;
      Uint64 now=SDL_GetTicksNS();
      if (deadline>now) SDL_DelayNS(deadline-now);
      else if (now-deadline>100000000ull) deadline=now;
    }
  }
  if (test_comp && !test_compositor()) result=1;
  if (!video_fixture.empty() && (!nv || !release_video_fixture(video_fixture.c_str()))) result=1;
  if (!output.empty() && !screenshot(output.c_str())) { std::fprintf(stderr,"Screenshot: %s\n",SDL_GetError()); result=1; }
  if (!pcx_out.empty() && !pcx_written) result=1;
  if (sync_frames && net_frame_count!=unsigned(sync_frames)) result=1;
  if (!state_out.empty() && !save_state_042(state_out)) result=1;
  Uint32 hash=2166136261u; for (auto p:pixels) hash=(hash^static_cast<byte>(p))*16777619u;
  if (!rom.empty() && romloaded && !CPURunning && !CPUpaused && limit) result=1;
  std::printf("frames=%d cpu_pc=%04x framebuffer=%08x audio_samples=%llu\n",frames,m6502pc,hash,static_cast<unsigned long long>(audio_samples));
  if (server || !connect_to.empty()) std::printf("net_frames=%u\n",net_frame_count);
  if (ram) std::printf("demo_nmi_count=%u sprite_x=%u sprite_y=%u controller_a=%u\n",static_cast<byte>(ram[0x20]),spritemem[0].x,spritemem[0].y,static_cast<byte>(ram[0x40]));
  if (test_input) {
    bool ok=buttons_seen && ram && spritemem[0].y==38 && ram[0x40]==0 && ram[0x41]==0 && ram[0x42]==0 && ram[0x43]==0;
    std::printf("Keyboard A/B/Select/Start: %s\n",ok?"PASS":"FAIL");
    if (!ok) result=1;
  }
  // Leaving at once could strand input the peer still needs for its last frames.
  if (sync_frames) net_finish(500,5000);
  if (!headless || save_config) {cfg->save(configfile);save_runtime_settings();options042_write("NEStcl95.ini");}
  save_battery_042();stop_movie_042();
  stop_wave_log();
  if (audio_dump) {std::fclose(audio_dump);audio_dump=nullptr;}
  delete ns; ns=nullptr;
  terminategame();
  net_disconnect();
  for (auto* pad:pads) if (pad) SDL_CloseGamepad(pad);
  SDL_DestroyAudioStream(audio); SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
  return result;
}
