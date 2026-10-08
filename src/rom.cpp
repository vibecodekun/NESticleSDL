//routines for handling roms

#include <stdio.h>
#include <stdlib.h>
#include <filesystem>
#include <cstring>
#include <limits>
#include <string.h>
#include <stdlib.h>
// #include <process.h>

#include "types.h"

#include "message.h"
#include "file.h"

#include "keyb.h"

#define FREE(x) if (x) {free(x); x=0;}
#define DELETE(x) if (x) {delete x; x=0;}

#include "nes.h"
#include "nesvideo.h"
#include "persistence.h"
#include "patch.h"
#include "netplay.h"
#include "release_ui_data.h"

//rom stuff
NESROMHEADER romhdr;
NESROMHEADER rom_file_header;
static_assert(sizeof(NESROMHEADER)==16,"ROM header must remain 16 bytes");
char *trainer;
int numrom,numvrom;
ROMPAGE *ROM;
VROMPAGE *VROM;

//
char *ram; //CPU address space
NES_ppumemory *ppu; //PPU address space
NES_sprite *spritemem; //Sprite memory

int NESinitialized=0;

int romloaded=0;
std::string rom_filename;
void freerom();

//-----------------------------------------------------
// NES rom loader

// NES 2.0 compatibility is a loader adaptation, not a new mapper implementation.
// Decode sizes before narrowing them to the bank counts used by the 0.42 core.
static std::uint64_t rom_size(unsigned low,unsigned high,unsigned unit)
{
 if (high!=15) return std::uint64_t(low | (high<<8))*unit;
 auto base=std::uint64_t(1)<<(low>>2);
 unsigned multiplier=(low&3)*2+1;
 if (base>std::numeric_limits<std::uint64_t>::max()/multiplier)
  return std::numeric_limits<std::uint64_t>::max();
 return base*multiplier;
}
bool NESROMHEADER::is_nes2() const {return (static_cast<unsigned char>(reserved[0])&12)==8;}
// 0.42 reads only byte 6's high nibble; NES 2.0 headers carry the full number.
unsigned NESROMHEADER::mapper_id() const {
 if (!is_nes2()) return banktype;
 return banktype | (static_cast<unsigned char>(reserved[0])&0xf0) | (static_cast<unsigned char>(reserved[1])&15)<<8;
}
unsigned NESROMHEADER::submapper_id() const {return is_nes2() ? static_cast<unsigned char>(reserved[1])>>4 : 0;}
std::uint64_t NESROMHEADER::prg_size() const {
 return rom_size(static_cast<unsigned char>(num16k),is_nes2() ? static_cast<unsigned char>(reserved[2])&15 : 0,0x4000);
}
std::uint64_t NESROMHEADER::chr_size() const {
 return rom_size(static_cast<unsigned char>(num8k),is_nes2() ? static_cast<unsigned char>(reserved[2])>>4 : 0,0x2000);
}
static unsigned ram_size(unsigned shift) {return shift ? 64u<<shift : 0;}
static bool translate_nes2(NESROMHEADER &header)
{
 const auto prg=header.prg_size(),chr=header.chr_size();
 if (!prg || prg%0x4000 || chr%0x2000) {
  msg.error("NES 2.0 ROM sizes cannot be represented as iNES 16K PRG / 8K CHR banks");return false;
 }
 if (prg/0x4000>255 || chr/0x2000>255) {
  msg.error("NES 2.0 ROM exceeds the iNES limit of 255 PRG / CHR banks");return false;
 }
 if (header.reserved[0]&3) {
  msg.error("NES 2.0 console type %u is not supported (requires NES/Famicom)",header.reserved[0]&3);return false;
 }
 unsigned mapper=header.mapper_id(),submapper=header.submapper_id();
 // These discrete-board variants only specify bus conflicts. Match the legacy
 // mapper behavior, as an iNES conversion would, and report the approximation.
 bool discrete=(mapper==2 || mapper==3 || mapper==7) && (submapper==1 || submapper==2);
 if (submapper && !discrete) {
  msg.error("NES 2.0 mapper %u submapper %u cannot use the Windows 0.42 mapper",mapper,submapper);return false;
 }
 unsigned prgram=ram_size(header.reserved[3]&15),prgnvram=ram_size(static_cast<unsigned char>(header.reserved[3])>>4);
 unsigned chrram=ram_size(header.reserved[4]&15),chrnvram=ram_size(static_cast<unsigned char>(header.reserved[4])>>4);
 if ((prgram && prgnvram) || prgram>8192 || prgnvram>8192) {
  msg.error("NES 2.0 PRG RAM layout is not supported (requires at most one 8K RAM bank)");return false;
 }
 if (chrnvram || (chr && chrram) || chrram>(mapper==6 ? 32768u : 8192u)) {
  msg.error("NES 2.0 CHR RAM layout is not supported by Windows 0.42");return false;
 }
 if (!chr && !chrram) {
  msg.error("NES 2.0 header specifies neither CHR ROM nor CHR RAM");return false;
 }
 if (header.battery && !prgnvram) {
  msg.error("NES 2.0 non-volatile storage is not supported (requires PRG NVRAM)");return false;
 }
 if (header.reserved[7]&3) {
  msg.error("NES 2.0 miscellaneous ROM data is not supported by Windows 0.42");return false;
 }
 if (discrete) msg.printf(1,"NES 2.0 submapper %u: using legacy mapper %u bus-conflict behavior",submapper,mapper);
 unsigned timing=header.reserved[5]&3,device=header.reserved[8]&63;
 if (timing==1 || timing==3) msg.printf(1,"NES 2.0 %s timing hint: using current Timing settings",timing==1 ? "PAL" : "Dendy");
 if (device>1) msg.printf(1,"NES 2.0 expansion device %u: using configured controllers",device);
 if ((prgram && prgram<8192) || (prgnvram && prgnvram<8192) || (chrram && chrram<8192))
  msg.printf(1,"NES 2.0 small RAM: using legacy 8K RAM mapping");
 if (prgnvram && !header.battery) msg.printf(1,"NES 2.0 PRG NVRAM: enabling battery saves");
 header.battery=prgnvram!=0;
 header.num16k=prg/0x4000;header.num8k=chr/0x2000;
 std::memset(header.reserved,0,sizeof(header.reserved));
 msg.printf(1,"NES 2.0 header translated to iNES in memory (ROM file unchanged)");
 return true;
}

// Follows 0.42's loader (00422950). A plain iNES header is read as 0.42 reads
// it: only "NES" is checked and the mapper is byte 6's high nibble, so byte 7
// (including "DiskDude!" text) is ignored. NES 2.0 translation is the port's
// one intentional loader extension.
void loadrom(char *romfile)
{
 if (!romfile) return;
 std::string filename(romfile);
 if (filename.find_last_of('.') == std::string::npos) filename += ".nes";
 char* s=filename.data();

 //free old rom
 freerom();

 msg.printf(2,"Loading rom %s...",s);
 FILEIO f;
 if (f.open(s)) {msg.error("Unable to open file"); return;}

  //read header
 if (f.read(&romhdr,sizeof(romhdr))) {msg.error("Unable to read header"); return;}
 auto alternate=std::filesystem::path(filename);alternate.replace_extension(".hdr");
 if (auto *header=std::fopen(alternate.string().c_str(),"rb")) {
  // 0.42 reads over the header without checking the result.
  NESROMHEADER h=romhdr;std::fread(&h,1,16,header);std::fclose(header);
  romhdr=h;msg.printf(1,"Alternate header read");
 }

 if (!romhdr.validate()) {msg.error("Bad ROM header"); return;}
 rom_file_header=romhdr;
 if (romhdr.is_nes2()) {
  // The NES 2.0 translation only targets mappers 0.42 implements.
  unsigned mapper=romhdr.mapper_id();
  if (mapper>15 || mapper==10 || mapper==12 || mapper==13 || mapper==14) {msg.error("Mapper %u is not supported by Windows 0.42",mapper); return;}
  if (!translate_nes2(romhdr)) return;
 }
 //load trainer; 0.42 continues without it when the read fails
 if (romhdr.trainer) trainer=(char *)f.readalloc(512);

 //read ROM; 0.42 skips the read for a zero bank count
 numrom=romhdr.num16k;
 if (numrom) {
  ROM=(ROMPAGE *)f.readalloc(numrom*sizeof(ROMPAGE));
  if (!ROM) {msg.error("error reading rom"); FREE(trainer); return;}
 }

 //read VROM
 numvrom=romhdr.num8k;
 VROM=(VROMPAGE *)f.readalloc(numvrom*sizeof(VROMPAGE)); //read vrom
 if (numvrom && !VROM) {msg.error("error reading vrom"); FREE(trainer); FREE(ROM); return;}
 msg.printf(1,"%dK ROM - %dK VROM",numrom*16,numvrom*8);
 long left=f.size()-f.getpos();
 if (left) msg.printf(1,"%d bytes left at end of .NES file",int(left));

 f.close();
 // Host bound: 0.42 goes on to run a zero-bank ROM from a null pointer.
 if (!numrom) {msg.error("Invalid ROM bank count"); FREE(trainer); FREE(VROM); return;}
 romloaded=1;
 persistence_rom(filename);
 rom_filename=filename;
 load_patches_042(patch_path_042());

 //initialize nes hardware....
 initNEShardware();
 unsigned mapper=romhdr.banktype;
 if (mapper==10 || mapper==13 || mapper==14) {
  // 0.42 reports these and runs them with its base mapper.
  msg.error("#%d: %s Mapper not supported",mapper,mapper_names042[mapper]);
 }

 //create video space
 std::string title=std::filesystem::path(filename).filename().string();
 new nesvideo(title.data());
 nes_load_initialize();
 load_battery_042();
 refresh_state_slots_042();
 net_rom_loaded();
}

void freerom()
{
 net_rom_freed();
 save_battery_042();stop_movie_042();
 clear_patches_042();
 terminateNEShardware();
 FREE(trainer);
 FREE(ROM);
 FREE(VROM);

 if (romloaded) msg.printf(2,"ROM freed");
 romloaded=0;

 //delete video space
 if (nv && nv->parent)  delete nv->parent;
}

void m_showmessages();
void m_getrominfo()
{
 if (!romloaded) msg.error("ROM not loaded");
  else  romhdr.printinfo(); //print info about rom
 m_showmessages();
}

void m_free()
{
 if (!romloaded) msg.error("ROM not loaded");
  else freerom();
}


//create data structures for nes hardware
void initNEShardware()
{
 if (NESinitialized) return;
 ppu=new NES_ppumemory;

 ram=(char *)calloc(1,0x10000);nes_initial_reset=true;
 msg.printf(2,"64K CPU address space created");

 spritemem=(NES_sprite *)malloc(256);
 msg.printf(2,"256 byte sprite mem created");
 NESinitialized=1;
}

//free data structures for nes hardware
void terminateNEShardware()
{
 if (!NESinitialized) return;
 m_stop();
 DELETE(ppu);
 FREE(ram);
 FREE(spritemem);
 NESinitialized=0;
}

//---------------------------------------------------------
//NES rom header

char *banktypestr[8]=
{
 "None",    //0
 "Sequential",  //1
 "Konami",   //2
 "VROM Switch", //3
 "5202 Chip", //4
 "Unknown", //5
 "Unknown", //5
 "Unknown", //5
};

// 0.42 (004232f0) checks only "NES".
int NESROMHEADER::validate() {return str[0]=='N' && str[1]=='E' && str[2]=='S';}
void NESROMHEADER::printinfo()
{
 if (this==&romhdr && rom_file_header.is_nes2())
  msg.printf(1,"NES 2.0 source: mapper %u, submapper %u (iNES compatibility)",rom_file_header.mapper_id(),rom_file_header.submapper_id());
 msg.printf(1,"# of 16K ROM banks: %d",num16k);
 msg.printf(1,"# of  8K VROM banks: %d",num8k);

 msg.printf(1,"%s mirroring",mirroring ? "Vertical" : "Horizontal");
 if (battery) msg.printf(1,"Battery backed RAM");
 if (trainer) msg.printf(1,"Trainer");
 if (reservedbit) msg.printf(1,"reserved bit set");


 msg.printf(1,"MMC #%d: %s",banktype,banktype<8 ? banktypestr[banktype] : "Windows 0.42 partial mapper");
}
//-----------------------------------------------
