#pragma once
#include <string>
#include "types.h"
constexpr unsigned STATE042_SIZE=0x59d0;
extern std::string save_directory,rom_basename;
extern unsigned state_slot;
void select_state_slot_042(unsigned slot);
std::string state_slot_path_042();
void refresh_state_slots_042();          // after a ROM loads
void state_slot_used_042(bool saved);    // flash SAVE or LOAD on the popup
void draw_state_popup_042(char *dest);
void persistence_rom(const std::string &path);
std::string persistence_path(const char *extension);
void save_battery_042();
void load_battery_042();
bool save_state_042(const std::string &path);
bool load_state_042(const std::string &path);
bool record_movie_042(const std::string &path);
bool play_movie_042(const std::string &path);
void stop_movie_042();
bool movie_active_042();
void movie_frame_042();
