// Recovered from Windows 0.42's 004142a0 parser and ROM-cache patch classes.
#include "patch.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "types.h"
#include "nes.h"
#include "persistence.h"
#include "netplay.h"

static std::vector<rom_patch> patches;
static std::array<char,0x8000> unpatched;
std::string patch_directory = "saves";

std::string rom_patch::code() const {
 char text[20];
 if (compare < 0) std::snprintf(text,sizeof(text),"%04X:%02X",address,value);
 else std::snprintf(text,sizeof(text),"%04X?%02X:%02X",address,compare,value);
 return text;
}
bool decode_patch_042(const std::string &input,rom_patch &patch) {
 auto start=input.find_first_not_of(" \t\r\n");
 if (start==std::string::npos) return false;
 std::string code=input.substr(start);
 std::transform(code.begin(),code.end(),code.begin(),[](unsigned char c){return std::toupper(c);});
 unsigned address=0,value=0,compare=0;
 rom_patch decoded;
 if (std::sscanf(code.c_str(),"%x?%x:%x",&address,&compare,&value)==3) decoded.compare=compare&255;
 else if (std::sscanf(code.c_str(),"%x:%x",&address,&value)!=2) {
  if (code.size()!=6 && code.size()!=8) return false;
  std::array<unsigned,8> n{};
  for (unsigned i=0;i<code.size();++i) {
   auto index=std::string("APZLGITYEOXUKSVN").find(code[i]);
   if (index==std::string::npos) return false;
   n[i]=index;
  }
  address=(n[3]&7)<<12 | (n[5]&7 | n[4]&8)<<8 |
          (n[2]&7 | n[1]&8)<<4 | (n[4]&7 | n[3]&8);
  value=(n[1]&7 | n[0]&8)<<4 | (n[0]&7) | (n[code.size()==6?5:7]&8);
  if (code.size()==8) decoded.compare=(n[7]&7 | n[6]&8)<<4 | (n[6]&7 | n[5]&8);
 }
 decoded.address=address&0x7fff;decoded.value=value;
 patch=decoded;return true;
}
const std::vector<rom_patch> &patches_042() {return patches;}
std::string patch_path_042() {return (std::filesystem::path(patch_directory)/(rom_basename+".pat")).string();}
static void activate(unsigned index) {
 auto &p=patches[index];
 for (auto &other:patches) if (other.address==p.address &&
     (p.compare<0 || other.compare<0 || p.compare==other.compare)) other.enabled=false;
 p.enabled=true;
}
bool add_patch_042(const std::string &code,const std::string &name,bool enabled) {
 if (net_active() || movie_active_042()) return false;
 rom_patch p;if (!decode_patch_042(code,p)) return false;
 p.name=(name.empty()?code:name).substr(0,63);
 for (char &c:p.name) if (c=='\r' || c=='\n') c=' ';
 patches.push_back(p);if (enabled) activate(patches.size()-1);
 apply_patches_042();return true;
}
void toggle_patch_042(unsigned i) {
 if (net_active() || movie_active_042()) return;
 if (i>=patches.size()) return;
 if (patches[i].enabled) patches[i].enabled=false;else activate(i);
 apply_patches_042();
}
void remove_patch_042(unsigned i) {
 if (net_active() || movie_active_042()) return;
 if (i<patches.size()) patches.erase(patches.begin()+i);
 apply_patches_042();
}
void clear_patches_042() {patches.clear();}
void disable_patches_042() {for (auto &p:patches) p.enabled=false;apply_patches_042();}
bool load_patches_042(const std::string &path) {
 if (net_active() || movie_active_042()) return false;
 std::ifstream f(path);if (!f) return false;
 patches.clear();std::string line;
 while (std::getline(f,line)) {
  auto start=line.find_first_not_of(" \t\r\n");
  if (start==std::string::npos || line[start]==';') continue;
  bool enabled=line[start]=='+';if (enabled) ++start;
  std::istringstream stream(line.substr(start));std::string code,name;
  if (!(stream>>code)) continue;
  std::getline(stream>>std::ws,name);
  if (!name.empty() && name.back()=='\r') name.pop_back();
  add_patch_042(code,name,enabled);
 }
 return true;
}
bool save_patches_042(const std::string &path) {
 std::error_code ec;auto parent=std::filesystem::path(path).parent_path();
 if (!parent.empty()) std::filesystem::create_directories(parent,ec);
 if (ec) return false;
 std::ofstream f(path);if (!f) return false;
 for (const auto &p:patches) f<<(p.enabled?'+':' ')<<p.code()<<"  "<<p.name<<'\n';
 f.close();return !f.fail();
}
void apply_patches_042() {
 if (!ram) return;
 std::copy(unpatched.begin(),unpatched.end(),ram+0x8000);
 if (net_active()) return;
 for (const auto &p:patches) if (p.enabled) {
  unsigned a=p.address+0x8000;
  if (p.compare<0 || byte(ram[a])==p.compare) ram[a]=p.value;
 }
}
void map_prg_042(unsigned address,const void *source,size_t size) {
 if (address<0x8000 || address>0x10000 || size>0x10000-address) return;
 std::memcpy(unpatched.data()+address-0x8000,source,size);
 apply_patches_042();
}
