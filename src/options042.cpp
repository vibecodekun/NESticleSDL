// Windows NESticle 0.42's options: the command line and NEStcl95.ini share one
// syntax (0040ab50). "-name parameters" sets an option, "@file" parses a file
// of options, and a bare argument means "-run <argument>" (00408540). System
// options run before the display exists; commands (load/run/restore/message/
// hidegui) run once the GUI is up (00408420). NEStcl95.ini is rewritten on exit
// (00408920) and ends with "@NEStcl95.cmd", so that file is read on start.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "types.h"
#include "dd.h"
#include "config.h"
#include "input.h"
#include "message.h"
#include "n6502.h"
#include "nes.h"
#include "nesvideo.h"
#include "gui.h"
#include "persistence.h"
#include "patch.h"
#include "runtime.h"
#include "timing.h"
#include "options042.h"

extern int SOUNDRATE;
extern int joybuttonmap[2][4];
void disablegui();
void m_showmessages();
void m_loadstate();
int sound_block_size=256;
std::string palette_file="NESticle.pal";

namespace {
struct option_value {std::string name,parm;};
std::vector<option_value> options;

// 0.42's input type numbers, used by -setinput, -setkey and -remapbut.
const char *const type_names[]={"NONE","KEY1","KEY2","JOY1","JOY2","GRAVIS"};
const int type_ids[]={ID_NONE,ID_KEY1,ID_KEY2,ID_JOY1,ID_JOY2,ID_GRAVIS};
int type_index(const char *name) { // 004136e0: unknown names are NONE
 for (int i=0;i<6;++i) if (!strcasecmp(name,type_names[i])) return i;
 return 0;
}
const char *type_name(int id) {
 for (int i=0;i<6;++i) if (type_ids[i]==id) return type_names[i];
 return type_names[0];
}
int clamp(int value,int low,int high) {return value<low ? low : value>high ? high : value;}
bool set_directory(const char *p,std::string &dir) { // 0041cfa0
 // _fullpath: an empty path is the current directory.
 std::error_code ec;auto full=*p ? std::filesystem::absolute(p,ec) : std::filesystem::current_path(ec);
 if (ec) {msg.error("Unable to find path: %s",p);return false;}
 dir=full.string();return true;
}

using handler=int(*)(const char*);
struct definition {const char *name;handler func;bool flag;const char *syntax;const char *desc;};

int opt_res(const char *p) {int w,h;if (std::sscanf(p,"%d %d",&w,&h)<2) return 0;SCREENX=w;SCREENY=h;return 1;}
int opt_waitvsync(const char*) {wait_vsync=1;return 1;}
// 0.42 returns sscanf's result, so an empty parameter (EOF) is not an error.
int opt_palfile(const char *p) {char name[128];int n=std::sscanf(p,"%127s",name);if (n>0) palette_file=name;return n!=0;}
int opt_sndrate(const char *p) {int v;if (std::sscanf(p,"%d",&v)<1) return 0;if (v>=8000 && v<=44100) SOUNDRATE=v;return 1;}
int opt_sndblksize(const char *p) {int v;if (std::sscanf(p,"%d",&v)<1) return 0;sound_block_size=clamp(v,16,4096);return 1;}
int opt_sndconstant(const char*) {return 1;}
int opt_trapbadops(const char*) {ncpu.trapbadops=1;return 1;}
int opt_setinput(const char *p) {
 int num;char type[32]="";if (std::sscanf(p,"%d %31s",&num,type)<1) return 0;
 if (--num<0 || num>3) {msg.error("Input num out of range");return 1;}
 cfg->pinput[num]=type_ids[type_index(type)];return 1;
}
int opt_remapbut(const char *p) {
 char type[32];if (std::sscanf(p,"%31s",type)<1) return 0;
 // The port keeps one SDL button map per joystick port; GRAVIS uses port 1's.
 int *map=joybuttonmap[type_ids[type_index(type)]==ID_JOY2 ? 1 : 0];
 std::sscanf(p,"%*s %d %d %d %d",&map[0],&map[1],&map[2],&map[3]);
 return 1;
}
int opt_setkey(const char *p) {
 char type[32];if (std::sscanf(p,"%31s",type)<1) return 0;
 int index=type_index(type);
 if (index!=1 && index!=2) {msg.error("Invalid input type %s.",type);return 1;}
 int v[12];
 int n=std::sscanf(p,"%*s %d %d %d %d %d %d %d %d %d %d %d %d",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9],&v[10],&v[11]);
 auto *bytes=reinterpret_cast<char*>(&cfg->ids.km[index-1]);
 for (int i=0;i<n && i<12;++i) bytes[i]=char(v[i]);
 return 1;
}
int opt_setjoythresh(const char *p) {
 int num;if (std::sscanf(p,"%d",&num)<1) return 0;
 if (--num<0 || num>1) return 0;
 auto &t=cfg->ids.aj[num];int skip;std::sscanf(p,"%d %d %d %d %d",&skip,&t.l,&t.r,&t.u,&t.d);return 1;
}
int opt_vfps(const char *p) {int v;if (std::sscanf(p,"%d",&v)<1) return 0;virtual_rate=clamp(v,10,1024);return 1;}
int opt_hblank(const char *p) {int v;if (std::sscanf(p,"%d",&v)<1) return 0;HBLANKCYCLES=clamp(v,10,256);return 1;}
int opt_vblank(const char *p) {int v;if (std::sscanf(p,"%d",&v)<1) return 0;VBLANKLINES=clamp(v,1,256);return 1;}
int opt_framelines(const char *p) {int v;if (std::sscanf(p,"%d",&v)<1) return 0;VFRAMELINES=clamp(v,1,256);return 1;}
int opt_netname(const char *p) {char name[16];int n=std::sscanf(p,"%15s",name);if (n>0) std::snprintf(cfg->localname,sizeof(cfg->localname),"%s",name);return n!=0;}
int opt_romdir(const char *p) {return set_directory(p,rom_directory);}
int opt_savedir(const char *p) {return set_directory(p,save_directory);}
int opt_pcxdir(const char *p) {return set_directory(p,snapshot_directory);}
int opt_logdir(const char *p) {return set_directory(p,log_directory);}
int opt_patchdir(const char *p) {return set_directory(p,patch_directory);}
int opt_nomsgpopup(const char*) {message_popup=0;return 1;}
int opt_nostatepopup(const char*) {state_popup=0;return 1;}
int opt_statebackup(const char*) {state_backup=1;return 1;}
int opt_nothrottle(const char*) {speed_throttling=0;return 1;}

definition system_options[]={ // 00439f58
 {"res",opt_res,false,"<X-res> <Y-res>","Change video resolution [256,224]"},
 {"nosound",nullptr,false,nullptr,"Disable sound"},
 {"waitvsync",opt_waitvsync,false,nullptr,"Wait till vertical sync to draw frames"},
 {"palfile",opt_palfile,false,"<name>","Reads palette from <name> [NESticle.pal]"},
 {"sndrate",opt_sndrate,false,"<samples/sec>","Change sound rate [22010]"},
 {"sndblksize",opt_sndblksize,false,"<samples>","Change sound block size [256]"},
 {"sndconstant",opt_sndconstant,false,"","Not used"},
 {"trapbadops",opt_trapbadops,false,nullptr,"Trap bad opcodes"},
 {"setinput",opt_setinput,false,"<num> <type>","Set input device <num> to <type>"},
 {"remapbut",opt_remapbut,false,"<type> <...>","Remaps buttons of device <type>"},
 {"setkey",opt_setkey,false,"<type> <...>","Redefine keys for keyboard <type> "},
 {"setjoythresh",opt_setjoythresh,false,"<num> <...>","Set threshholds for joy <type> "},
 {"vfps",opt_vfps,false,"<x>","Sets Virtual FPS to <x>"},
 {"hblank",opt_hblank,false,"<x>","Sets hblankcycles to <x>"},
 {"vblank",opt_vblank,false,"<x>","Sets vblanklines to <x>"},
 {"framelines",opt_framelines,false,"<x>","Sets framelines to <x>"},
 {"netname",opt_netname,false,"<name>","Sets default name for netplay"},
 {"force8bit",nullptr,false,nullptr,"Forces 8-bit sound output"},
 {"romdir",opt_romdir,false,"<dir>","Sets dir for *.NES"},
 {"savedir",opt_savedir,false,"<dir>","Sets dir for *.SAV/*.STA"},
 {"pcxdir",opt_pcxdir,false,"<dir>","Sets dir for *.PCX"},
 {"logdir",opt_logdir,false,"<dir>","Sets dir for logs"},
 {"patchdir",opt_patchdir,false,"<dir>","Sets dir for patches"},
 {"nomsgpopup",opt_nomsgpopup,false,nullptr,"Disables message popup"},
 {"nostatepopup",opt_nostatepopup,false,nullptr,"Disables state popup"},
 {"statebackup",opt_statebackup,false,nullptr,"Enables state backup"},
 {"nothrottle",opt_nothrottle,false,nullptr,"Disables speed throttle"},
 {nullptr,nullptr,false,nullptr,nullptr}
};

// Commands (0043a138): -load only loads; -run also hides the GUI, maximizes the
// NES view and resets; -restore restores the current state slot instead,
// resetting only if that leaves the CPU stopped. They pass the whole parameter
// string to the loader, so paths may contain spaces.
void show_game() {disablegui();static_cast<GUImaximizebox*>(nv->parent)->maximize();}
int cmd_message(const char*) {m_showmessages();return 1;}
int cmd_hidegui(const char*) {disablegui();return 1;}
int cmd_load(const char *p) {
 char file[128];if (std::sscanf(p,"%127s",file)<1) return 0;
 std::string path(p);loadrom(path.data());return 1;
}
int cmd_run(const char *p) {
 if (!cmd_load(p)) return 0;
 if (nv) {show_game();m_reset();}
 return 1;
}
int cmd_restore(const char *p) {
 if (!cmd_load(p)) return 0;
 if (nv) {show_game();m_loadstate();if (!CPURunning) m_reset();}
 return 1;
}
definition commands[]={
 {"message",cmd_message,false,nullptr,"Display message box"},
 {"hidegui",cmd_hidegui,false,nullptr,"Hide GUI on startup"},
 {"load",cmd_load,false,"<filename>","Load ROM <filename>"},
 {"run",cmd_run,false,"<filename>","Load & run ROM <filename>"},
 {"restore",cmd_restore,false,"<filename>","Load/run/restore ROM <filename>"},
 {nullptr,nullptr,false,nullptr,nullptr}
};

definition *find(definition *table,const std::string &name) {
 for (auto *d=table;d->name;++d) if (!strcasecmp(d->name,name.c_str())) return d;
 return nullptr;
}

// 00408540: build options from tokens.
void add_tokens(const std::vector<std::string> &tokens) {
 for (size_t i=0;i<tokens.size();) {
  const std::string &token=tokens[i];
  if (token[0]=='-') {
   option_value option{token.substr(1),""};
   for (++i;i<tokens.size() && tokens[i][0]!='-' && tokens[i][0]!='@';++i) {
    if (!option.parm.empty()) option.parm+=' ';
    option.parm+=tokens[i];
   }
   options.push_back(option);
  } else if (token[0]=='@') {
   std::string file=token.substr(1);++i;
   if (auto *f=std::fopen(file.c_str(),"rt")) {
    msg.printf(1,"Parsing file: %s",file.c_str());
    char line[256];while (std::fgets(line,sizeof(line),f)) options042_parse_line(line);
    std::fclose(f);
   }
  } else options.push_back({"run",tokens[i++]});
 }
}
}

// 004087c0: whitespace-separated tokens. A ';' starting a token directly
// after the previous token's single delimiter ends the line.
void options042_parse_line(const char *line) {
 std::vector<std::string> tokens;const char *p=line;
 while (*p) {
  if (*p==';') break;
  while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
  if (!*p) break;
  const char *start=p;
  while (*p && !std::isspace(static_cast<unsigned char>(*p))) ++p;
  tokens.emplace_back(start,p);
  if (!*p) break;
  ++p;
 }
 add_tokens(tokens);
}
void options042_add_arguments(const std::vector<std::string> &tokens) {add_tokens(tokens);}
void options042_read_file(const char *path) {
 if (auto *f=std::fopen(path,"rt")) {
  char line[256];while (std::fgets(line,sizeof(line),f)) options042_parse_line(line);
  std::fclose(f);
 }
}
// 00408420: type 0 runs system options and reports unknown names; type 1 runs
// commands. A handler returning 0 reports a syntax error.
void options042_execute(bool run_commands) {
 for (auto &option:options) {
  auto *d=find(run_commands ? commands : system_options,option.name);
  if (!d) {
   if (!run_commands && !find(commands,option.name)) msg.error("unrecognized arg: %s",option.name.c_str());
   continue;
  }
  d->flag=true;
  if (d->func && !d->func(option.parm.c_str())) msg.error("syntax error: %s %s",d->name,d->syntax ? d->syntax : "(null)");
 }
}
bool options042_flag(const char *name) {auto *d=find(system_options,name);return d && d->flag;}

// 00408920
void options042_write(const char *path) {
 auto *f=std::fopen(path,"wt");
 if (!f) {msg.error("Unable to create %s",path);return;}
 std::fprintf(f,";----------------------------------------------\n");
 std::fprintf(f,"; Automatically generated NESticle config file \n");
 std::fprintf(f,";----------------------------------------------\n\n");
 std::fprintf(f,"; video settings\n");
 std::fprintf(f,"-res %d %d\n",SCREENX,SCREENY);
 std::fprintf(f,"-palfile %s\n",palette_file.c_str());
 if (wait_vsync) std::fprintf(f,"-waitvsync\n");
 std::fprintf(f,"\n");
 if (!message_popup) std::fprintf(f,"-nomsgpopup\n");
 if (!state_popup) std::fprintf(f,"-nostatepopup\n");
 if (state_backup) std::fprintf(f,"-statebackup\n");
 if (!speed_throttling) std::fprintf(f,"-nothrottle\n");
 std::fprintf(f,"\n");
 std::fprintf(f,"; sound settings\n");
 std::fprintf(f,"-sndrate    %d\n",SOUNDRATE);
 std::fprintf(f,"-sndblksize %d\n",sound_block_size);
 std::fprintf(f,"\n");
 std::fprintf(f,"; input device types\n");
 for (int i=0;i<4;++i) std::fprintf(f,"-setinput %d %s\n",i+1,type_name(cfg->pinput[i]));
 std::fprintf(f,"\n");
 std::fprintf(f,"; keyboard scan codes\n");
 for (int k=0;k<2;++k) {
  auto *b=reinterpret_cast<const unsigned char*>(&cfg->ids.km[k]);
  std::fprintf(f,"-setkey KEY%d %d %d %d %d %d %d %d %d %d %d %d %d\n",k+1,b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],b[10],b[11]);
 }
 std::fprintf(f,"\n");
 std::fprintf(f,"; joystick settings\n");
 for (int j=0;j<2;++j) {auto &t=cfg->ids.aj[j];std::fprintf(f,"-setjoythresh %d   %d %d %d %d\n",j+1,t.l,t.r,t.u,t.d);}
 // JOY1, JOY2 and GRAVIS (0043a1a0); GRAVIS shares port 1's map here.
 const struct {const char *name;int port;} remaps[]={{"JOY1",0},{"JOY2",1},{"GRAVIS",0}};
 for (auto &r:remaps) {
  auto *map=joybuttonmap[r.port];
  std::fprintf(f,"-remapbut %6s %d %d %d %d\n",r.name,map[0],map[1],map[2],map[3]);
 }
 std::fprintf(f,"\n");
 std::fprintf(f,"; directories\n");
 std::fprintf(f,"-pcxdir   %s\n",snapshot_directory.c_str());
 std::fprintf(f,"-savedir  %s\n",save_directory.c_str());
 std::fprintf(f,"-logdir   %s\n",log_directory.c_str());
 std::fprintf(f,"-patchdir %s\n",patch_directory.c_str());
 std::fprintf(f,"-romdir   %s\n",rom_directory.c_str());
 std::fprintf(f,"\n");
 std::fprintf(f,"; Netplay\n");
 if (cfg->localname[0]) std::fprintf(f,"-netname %s\n",cfg->localname);
 std::fprintf(f,"\n");
 std::fprintf(f,"; NES emulation\n");
 std::fprintf(f,"-vfps   %d\n",virtual_rate);
 std::fprintf(f,"-hblank %d\n",HBLANKCYCLES);
 std::fprintf(f,"-framelines %d\n",VFRAMELINES);
 std::fprintf(f,"-vblank %d\n",VBLANKLINES);
 if (ncpu.trapbadops) std::fprintf(f,"-trapbadops\n");
 std::fprintf(f,"\n");
 std::fprintf(f,"\n");
 std::fprintf(f,"; insert command line file...\n");
 std::fprintf(f,"@NEStcl95.cmd\n");
 std::fclose(f);
}
