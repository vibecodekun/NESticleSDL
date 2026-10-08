#pragma once
void platform_heap_check();
#include <string>
extern int virtual_rate,scroll_fix1,scroll_fix2,state_backup,state_popup,message_popup;
extern int speed_throttling,wait_vsync,tile_caching,show_cpu_use;
extern double cpu_use_percent;
extern std::string rom_directory,log_directory,snapshot_directory;
void load_runtime_settings();
void save_runtime_settings();
void m_reset();
void m_togglepause();
void m_pause();
void m_resume();
void clearprimary();
void m_advanceframe();
void m_step();
bool emulation_due();   // running, or paused with a queued frame or trace
void emulation_ticked();
// Advance frame with Ctrl: that frame's PPU/IRQ event messages (0043aae0).
extern unsigned char ppu_event_log;
int ppu_log_line();
int ppu_log_cycle();
void m_vint();
void m_dumpregs();
void m_loadstate();
void m_savestate();
void platform_choose_rom();
void platform_snapshot();
void platform_frame_drawn();
void platform_sound_changed();
bool start_wave_log(const std::string &path);
void stop_wave_log();
