#include <stdlib.h>
#include <algorithm>
#include "n6502.h"
#include <string.h>

#include "nessound.h"
#include "message.h"

#include "r2img.h"
#include "font.h"
#include "dd.h"

#include "nes.h"

#include "sound.h"

#include "timing.h"


//primary output functions
void clearprimary();
void primaryoutput(short *buf,int nums);
void primaryskip(int nums);

extern byte soundinstalled;

//global nessound device
nessound *ns;

//---------------------------------
//sound enable stuff
byte soundenabled=1;
void disablesound()
{
 if (!soundenabled) return;
 clearprimary(); //clear the output buffer
 msg.printf(1,"Sound disabled");
 soundenabled=0;
}

void enablesound()
{
 if (soundenabled) return;
 if (!soundinstalled) {msg.error("Sound not initialized"); return;}
 soundenabled=1;
 msg.printf(1,"Sound enabled");
}

void togglesound()
{
 if (soundenabled) disablesound(); else enablesound();
}

//============================================
// channel i/o

void neschannel::print(int x,int y)
{
 font[(enabled&&noteon) ? 2 : 1]->printf(x,y,
    "%02X %cf=%03X t=%02X %X %d %X",
    r[0],
//   "pbr=%X dt=%X %c%c%c f=%03X t=%02X",
//   sr.cr1.playbackrate,
//   sr.cr1.dutycycle,
//   sr.cr1.holdnote ? 'H' : ' ' ,
//   sr.cr1.envelopefixed ? ' ' : 'E ' ,
   sr.cr2.freqvariable ? 'V' : 'F' ,
   sr.fr.getfreq(),
   sr.fr.gettime(),
   r[1],deltaK,K
   );
}


//------------------
//square wave

//duty cycles as % of period (0x10000)
int duty[4]={87.5*0x10000/100,75.0*0x10000/100,58.5*0x10000/100,25.0*0x10000/100};

//int duty[4]={77.5*0x10000/100,65.0*0x10000/100,50.0*0x10000/100,35.0*0x10000/100};
//int duty[4]={12.5*0x10000/100,25.0*0x10000/100,42.0*0x10000/100,75.0*0x10000/100};
//int duty[4]={50*0x10000/100,50*0x10000/100,50*0x10000/100,50*0x10000/100};


int vols[16]=
 {
//  0, 0x200, 0x400, 0x600, 0x800, 0xA00, 0xC00, 0xE00, 0xF00,
//  0x1000, 0x1100, 0x1200, 0x1300, 0x1400, 0x1500, 0x15FF
      0,0x200, 0x300, 0x380, 0x400, 0x480, 0x500, 0x580,
  0x600,0x640, 0x680, 0x6C0, 0x700, 0x740, 0x780, 0x7C0
//      0,0x200, 0x300, 0x380, 0x400, 0x420, 0x500, 0x580,
//  0x600,0x610, 0x620, 0x630, 0x640, 0x650, 0x660, 0x670
 };

// Recovered Windows 0.42 mixer, not a hardware APU model.
// Addresses and lookup tables are recorded in RECOVERY.md.
static const int lengths[32]={4,8,12,16,20,25,30,35,40,45,51,57,62,64,68,72,
    72,72,72,72,72,72,72,72,80,80,80,80,80,80,80,80};
void squarewave::setvolume() { vol=(r[0]&0x10) ? (r[0]&15)<<7 : 0x600; }
void squarewave::mix16(short *b,int nums) {
 if (!noteon) return;
 nums=std::max(0,std::min(nums,dur));
 if (vol>0) for (int i=0;i<nums;++i) {
  b[i]+=count<duty[r[0]>>6] ? vol : -vol;
  count=(count+K)&0xffff;
 }
 dur-=nums;
 if (!(r[0]&0x10)) vol-=2; // Once per mixer call, not once per sample.
 K+=deltaK;
 if (dur<1) noteon=0;
}
void trianglewave::setvolume() {
 if (!r[0]) {noteon=0;return;}
 vol=(r[0]&0xf0) ? 0x800 : (r[0]&15)<<8;
}
void trianglewave::setduration() { dur=(sr.fr.gettime()+1)*SOUNDRATE/8; }
void trianglewave::mix16(short *b,int nums) {
 if (!noteon) return;
 nums=std::max(0,std::min(nums,dur));
 if (vol>0) for (int i=0;i<nums;++i) {
  count=static_cast<unsigned>(count)+K;
  int x=(count&0x10000) ? 0x8000-(count&0xffff) : (count&0xffff)-0x8000;
  b[i]+=x*vol/0x8000;
 }
 dur-=nums; if (dur<1) noteon=0;
}
void neschannel::setfrequency() {
 freq=0x369e99/((sr.fr.getfreq()+1)*32);
 if (freq>SOUNDRATE/2) {noteon=0;return;}
 K=(freq<<16)/SOUNDRATE;
}
void neschannel::setduration() { dur=(r[0]&0x20) ? SOUNDRATE : lengths[sr.fr.gettime()]*SOUNDRATE/64; }
void neschannel::startnote() { noteon=enabled; setfrequency(); setduration(); }
void neschannel::setdeltaK() {
 if (!(r[1]&0x80) || !(r[1]&7) || !(r[1]&0x70)) {deltaK=0;return;}
 deltaK=(((r[1]>>4)&7)*(r[1]&7)*0x10000/8)*256/(SOUNDRATE<<8);
 if (r[1]&8) deltaK=-deltaK;
}
void noisewave::setduration() { dur=(sr.fr.gettime()+1)*SOUNDRATE/64; }
void noisewave::setfrequency() {
 freq=0x369e99/((sr.fr.getfreq()+1)*32); K=static_cast<int32_t>(unsigned(freq)<<16)/SOUNDRATE;
}
void noisewave::setvolume() {sample=0;vol=(r[0]&15)<<((r[0]&8) ? 6 : 7);}
// The noise is 0.42's only rand() caller (00419dd0). Its Visual C++ CRT rand
// (0042a340), never reseeded, is kept here: glibc's has another sequence and
// a 31-bit range that overflows the sample formula.
static int rand_042() {
 static uint32_t holdrand=1;
 holdrand=holdrand*0x343fd+0x269ec3;return holdrand>>16&0x7fff;
}
void noisewave::mix16(short *b,int nums) {
 if (!noteon) return;
 nums=std::max(0,std::min(nums,dur));
 if (vol>0) for (int i=0;i<nums;++i) {
  b[i]+=sample;
  unsigned old=count; count=static_cast<unsigned>(count)+K;
  if ((old^count)&0x10000) sample=rand_042()*vol*2/0x8000-vol;
 }
 dur-=nums; if (dur<1) noteon=0;
}
// 0.42's 'DMC' is only a 2048-sample ring filled by direct $4011 writes.
// $4010/$4012/$4013 have no effect and PCM is disabled by default.
bool pcm_enabled=false;
unsigned sound_channels=15;
static short pcm_ring[2048];
static unsigned pcm_read,pcm_write;
static int pcm_cycle;
static byte pcm_last;
void sound_reset_pcm() {pcm_read=pcm_write=0;}
// 0.42 rebases with the raw total counter (0043b20c), not elapsed cycles.
void sound_start_frame() {pcm_cycle=pcm_cycle<1 ? 0 : pcm_cycle-ncpu.totalcycles;}
void pcmwave::write(byte a,byte d) {
 if (!pcm_enabled || a!=1) return;
 enabled=1;
 int step=1600000/SOUNDRATE;
 int n=(nes_cpu_cycles()-pcm_cycle)/step;
 short value=static_cast<signed char>(pcm_last-0x80)*64;
 pcm_cycle+=step*(n+1);pcm_last=d;
 for (;n>=0;--n) {pcm_ring[pcm_write]=value;pcm_write=(pcm_write+1)&0x7ff;}
}
void pcmwave::mix16(short *b,int nums) {
 for (int i=0;i<nums && pcm_read!=pcm_write;++i) {
  b[i]+=pcm_ring[pcm_read];pcm_read=(pcm_read+1)&0x7ff;
 }
}

void neschannel::write(byte a,byte d)
{
 //msg.printf(1,"write: %X %X",a,d);
 r[a]=d;
 switch (a)
 {
  case 0: setvolume(); break; //control reg 1
  case 1: setdeltaK(); break; //control reg 2
  case 2: //freq regs
  case 3: startnote(); break;
 }
}

void neschannel::stopnote() {noteon=0;}
void neschannel::reset() {r[0]=r[1]=r[2]=r[3]=0; deltaK=0; stopnote(); enabled=0;}
void neschannel::setenable(byte x)
{
 enabled=x ? 1 : 0;
 if (!enabled) stopnote();
}


//===================================================
// main sound device
void nessound::write(byte a,byte d) {ch[a/4]->write(a&3,d);}

nessound::nessound()
{
 ch[0]=new squarewave;
 ch[1]=new squarewave;
 ch[2]=new trianglewave;
 ch[3]=new noisewave;
 ch[4]=new pcmwave;
 reset();
 clearprimary();
}

nessound::~nessound()
{
 clearprimary();
 for (int i=0; i<5; i++) delete ch[i];
}

void nessound::reset()
{
 sound_reset_pcm();
 for (int i=0; i<5; i++) ch[i]->reset();
 memset(mixingbuf,0,4096*2);
}

//decode enable bit for each channel
void nessound::setenable(byte d)
{
 enablebits=d;
 for (int i=0; i<5; i++)
  ch[i]->setenable((i==4 ? !pcm_enabled : !(sound_channels&(1<<i))) ? 0 : d&(1<<i));
}

//print all channels
void nessound::print()
{
 //for (int i=0; i<5; i++)
//  ch[i]->print(50,50+i*10);
}

//------
//mixing


//mixes nums 16 samples to *b
void nessound::mix16(short *b,int nums)
{
 //clear buffer
 memset(b,0,nums*2);
 if (!CPURunning) return;

 //mix all sound channels
 for (int i=0; i<5; i++)
   if (ch[i]->enabled)   ch[i]->mix16(b,nums);
}


short nessound::mixingbuf[4096];

void nessound::update(int nums)
{
 if (!soundinstalled) return;
 if (!soundenabled) {primaryskip(nums); return;}

 mix16(mixingbuf,nums); //do 16bit mixing
 primaryoutput(mixingbuf,nums); //output it to primary buffer
}






