#include "runtime.h"
#include <fstream>
#include <iomanip>
#include <algorithm>
#include "types.h"
#include "nes.h"
#include "nesvideo.h"
#include "nessound.h"
#include "persistence.h"
#include "patch.h"
#include "n6502.h"
#include "dd.h"
#include "timing.h"
#include "input.h"

int virtual_rate=60,scroll_fix1=0,scroll_fix2=0,state_backup=0,state_popup=1,message_popup=1;
int speed_throttling=1,wait_vsync=0,tile_caching=1,show_cpu_use=0;
double cpu_use_percent=0;
std::string rom_directory=".",log_directory=".",snapshot_directory=".";

void load_runtime_settings() {
 std::ifstream f("nesticle-sdl.ini");std::string key;
 while (f>>key) {
  if (key=="savedir") f>>std::quoted(save_directory);
  else if (key=="patchdir") f>>std::quoted(patch_directory);
  else if (key=="romdir") f>>std::quoted(rom_directory);
  else if (key=="logdir") f>>std::quoted(log_directory);
  else if (key=="pcxdir") f>>std::quoted(snapshot_directory);
  else if (key=="joybuttons1" || key=="joybuttons2") {
   auto *map=joybuttonmap[key=="joybuttons2"];
   for (int i=0,value;i<4 && f>>value;++i) if ((value>=0 && value<32) || value==255) map[i]=value;
  }
  else {
   int value;if (!(f>>value)) break;
   if (key=="statebackup") state_backup=value!=0;
   else if (key=="statepopup") state_popup=value!=0;
   else if (key=="messagepopup") message_popup=value!=0;
   else if (key=="scrollfix1") scroll_fix1=value!=0;
   else if (key=="scrollfix2") scroll_fix2=value!=0;
   else if (key=="throttle") speed_throttling=value!=0;
   else if (key=="vsync") wait_vsync=value!=0;
   else if (key=="pcm") pcm_enabled=value!=0;
   else if (key=="channels") sound_channels=value&15;
   else if (key=="tilecache") tile_caching=value!=0;
   else if (key=="cpuuse") show_cpu_use=value!=0;
   else if (key=="rate" && value>=1 && value<=1000) virtual_rate=value;
   else if (key=="cycles" && value>=1 && value<=10000) HBLANKCYCLES=value;
   else if (key=="blanklines" && value>=8 && value<=1000) VBLANKLINES=value;
   else if (key=="framelines" && value>=1 && value<=1000) VFRAMELINES=value;
  }
 }
}
void save_runtime_settings() {
 std::ofstream f("nesticle-sdl.ini");
 f<<"savedir "<<std::quoted(save_directory)<<"\npatchdir "<<std::quoted(patch_directory)
  <<"\nromdir "<<std::quoted(rom_directory)<<"\nlogdir "<<std::quoted(log_directory)
  <<"\npcxdir "<<std::quoted(snapshot_directory)<<"\nstatebackup "<<state_backup
  <<"\nstatepopup "<<state_popup<<"\nmessagepopup "<<message_popup
  <<"\nscrollfix1 "<<scroll_fix1<<"\nscrollfix2 "<<scroll_fix2
  <<"\nthrottle "<<speed_throttling<<"\nvsync "<<wait_vsync<<"\npcm "<<int(pcm_enabled)
  <<"\nchannels "<<sound_channels<<"\ntilecache "<<tile_caching<<"\ncpuuse "<<show_cpu_use<<"\nrate "<<virtual_rate
  <<"\ncycles "<<HBLANKCYCLES<<"\nblanklines "<<VBLANKLINES<<"\nframelines "<<VFRAMELINES<<'\n';
 for (int i=0;i<2;++i) {f<<"joybuttons"<<i+1;for (int button:joybuttonmap[i]) f<<' '<<button;f<<'\n';}
}
void m_loadstate() {if (load_state_042(state_slot_path_042())) {state_slot_used_042(false);clearprimary();}}
void m_savestate() {if (save_state_042(state_slot_path_042())) state_slot_used_042(true);}
