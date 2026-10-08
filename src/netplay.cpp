// Windows 0.42 UDP wire format: packed connect/ping packets, ordered reliable
// envelopes and run-length encoded controller batches. See RECOVERY.md.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <thread>
#include <vector>
#include "types.h"
#include "netplay.h"
#include "net_codec.h"
#include "nes.h"
#include "input.h"
#include "dd.h"
#include "message.h"
#include "runtime.h"
#include "timing.h"
#include "persistence.h"

static SOCKET connection=INVALID_SOCKET;
static sockaddr_in peer{},local{};
static bool hosting,joined,playing,local_player1;
#ifdef _WIN32
static bool winsock_ready;
#endif
static std::string local_name,peer_name;
static uint16_t send_sequence,receive_sequence;
struct pending_packet {std::vector<byte> bytes;uint64_t due;};
static std::map<uint16_t,pending_packet> pending;
static std::map<uint16_t,std::vector<byte>> reordered;
static std::deque<byte> local_input,remote_input;
static size_t send_cursor;
static uint64_t last_seen,next_ping,next_connect,connect_deadline;
static unsigned retries,ping_frames;
bool net_remote_action=false;
int net_latency=5;
unsigned net_frame_count=0;

static uint64_t tick() {
 using namespace std::chrono;
 return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
static uint16_t word_at(const byte *p) {return uint16_t(p[0])|uint16_t(p[1])<<8;}
static void append_word(std::vector<byte> &p,unsigned value) {p.push_back(value);p.push_back(value>>8);}
bool net_active() {return connection!=INVALID_SOCKET;}
bool net_connected() {return net_active() && joined;}
static bool same_peer(const sockaddr_in &a) {return a.sin_addr.s_addr==peer.sin_addr.s_addr && a.sin_port==peer.sin_port;}
static void send_raw(const std::vector<byte> &packet,const sockaddr_in &address) {
 if (net_active()) sendto(connection,reinterpret_cast<const char*>(packet.data()),packet.size(),0,reinterpret_cast<const sockaddr*>(&address),sizeof(address));
}
static void reliable(std::vector<byte> payload) {
 if (!joined || pending.size()>=256) return;
 std::vector<byte> packet{1};append_word(packet,send_sequence);packet.insert(packet.end(),payload.begin(),payload.end());packet.push_back(0);
 send_raw(packet,peer);pending[send_sequence++]={packet,tick()+std::max(5u,ping_frames+5)*1000/60};
}
static std::vector<byte> identity(byte type) {
 std::vector<byte> packet(33,0);packet[0]=type;std::memcpy(packet.data()+1,&local,16);
 std::memcpy(packet.data()+17,local_name.c_str(),std::min(size_t(15),local_name.size()));return packet;
}
static void start_game(bool player1) {
 local_player1=player1;playing=true;local_input.assign(net_latency*2,0);remote_input.clear();send_cursor=0;net_frame_count=0;
 net_remote_action=true;m_reset();net_remote_action=false;
 msg.printf(1,"Netplay started as player %d",player1?1:2);
}
static void send_reset() {
 if (!joined || !romloaded) return;
 playing=false;std::vector<byte> p{13};
 append_word(p,net_latency);append_word(p,VBLANKLINES);append_word(p,HBLANKCYCLES);append_word(p,virtual_rate);append_word(p,VFRAMELINES);
 reliable(p);
}
bool net_start(bool server,const std::string &address,unsigned port,const std::string &name) {
 if (name.empty() || name.size()>15 || port<1 || port>65535) {msg.error("Invalid netplay name or UDP port");return false;}
#ifdef _WIN32
 if (!winsock_ready) {WSADATA data{};if (WSAStartup(MAKEWORD(2,2),&data)) {msg.error("Unable to initialize Winsock");return false;}winsock_ready=true;}
#endif
 net_disconnect();connection=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
 if (connection==INVALID_SOCKET) {msg.error("Unable to create netplay socket");return false;}
 hosting=server;local_name=name;local={};local.sin_family=AF_INET;local.sin_port=htons(server?port:0);
 if (bind(connection,reinterpret_cast<const sockaddr*>(&local),sizeof(local))==SOCKET_ERROR) {msg.error("Unable to bind netplay port");net_disconnect();return false;}
 socklen_t length=sizeof(local);getsockname(connection,reinterpret_cast<sockaddr*>(&local),&length);
#ifdef _WIN32
 u_long nonblocking=1;ioctlsocket(connection,FIONBIO,&nonblocking);
#else
 fcntl(connection,F_SETFL,fcntl(connection,F_GETFL,0)|O_NONBLOCK);
#endif
 if (!server) {
  peer={};peer.sin_family=AF_INET;peer.sin_port=htons(port);
  if (inet_pton(AF_INET,address.c_str(),&peer.sin_addr)!=1) {msg.error("Invalid IP address");net_disconnect();return false;}
  send_raw(identity(3),peer);next_connect=tick()+3333;connect_deadline=tick()+25000;
 }
 if (romloaded) {net_remote_action=true;freerom();net_remote_action=false;}
 joined=playing=false;pending.clear();reordered.clear();send_sequence=receive_sequence=0;retries=ping_frames=0;
 next_ping=tick()+5000;last_seen=tick();msg.printf(1,server?"NESticle UDP server started on %u":"Connecting to NESticle UDP port %u",port);return true;
}
void net_disconnect() {
 if (net_active()) {
  if (joined) send_raw({6,byte(hosting?1:0)},peer);
  closesocket(connection);connection=INVALID_SOCKET;
  if (playing && romloaded) {CPURunning=0;CPUpaused=1;}
  msg.printf(1,"Netplay disconnected");
 }
 joined=playing=false;pending.clear();reordered.clear();local_input.clear();remote_input.clear();
}
void net_rom_loaded() {
 if (!net_active() || net_remote_action) return;
 playing=false;
 if (joined) {
  std::vector<byte> p(35,0);p[0]=10;
  std::memcpy(p.data()+1,rom_basename.c_str(),std::min(size_t(31),rom_basename.size()));p[33]=numrom;p[34]=numvrom;reliable(p);
 }
}
void net_rom_freed() {if (net_active() && !net_remote_action && romloaded) {playing=false;if (joined) reliable({12});}}
void net_reset_requested() {if (!net_remote_action) send_reset();}
static void payload(const std::vector<byte> &p) {
 if (p.empty()) return;
 switch(p[0]) {
 case 9: {
  auto end=std::find(p.begin()+1,p.end(),byte(0));std::string text(p.begin()+1,end);
  netchat.printf(1,"<%s> %s",peer_name.c_str(),text.c_str());break;
 }
 case 10: {
  if (p.size()<35) break;
  auto end=std::find(p.begin()+1,p.begin()+33,byte(0));std::string name(p.begin()+1,end);
  if (name.empty() || name.find_first_of("/\\:")!=std::string::npos || name=="..") {reliable({11,0,0});break;}
  auto file=std::filesystem::path(name);if (!file.has_extension()) file+=".nes";
  auto path=(std::filesystem::path(rom_directory)/file).string();
  net_remote_action=true;loadrom(path.data());net_remote_action=false;
  byte result=!romloaded?0: numrom==p[33] && numvrom==p[34]?2:1;
  if (result==1) {net_remote_action=true;freerom();net_remote_action=false;}
  reliable({11,result,0});break;
 }
 case 11:
  if (p.size()<2) break;
  if (p[1]==2) send_reset();else {msg.error("Remote ROM unavailable or incompatible");net_remote_action=true;freerom();net_remote_action=false;}
  break;
 case 12:net_remote_action=true;freerom();net_remote_action=false;playing=false;break;
 case 13: {
  if (p.size()<11 || !romloaded) break;
  int latency=word_at(p.data()+1),blank=word_at(p.data()+3),cycles=word_at(p.data()+5),rate=word_at(p.data()+7),lines=word_at(p.data()+9);
  if (latency<1 || latency>64 || blank<8 || blank>1000 || cycles<1 || cycles>10000 || rate<1 || rate>1000 || lines<1 || lines>1000) break;
  net_latency=latency;VBLANKLINES=blank;HBLANKCYCLES=cycles;virtual_rate=rate;VFRAMELINES=lines;
  reliable({16});start_game(false);break;
 }
 case 16:if (romloaded) start_game(true);break;
 case 14: {
  if (!playing) break;
  std::vector<byte> decoded;if (!decode_net_inputs_042(p.data(),p.size(),decoded)) break;
  if (remote_input.size()+decoded.size()>256) {msg.error("Netplay input queue overflow");net_disconnect();return;}
  remote_input.insert(remote_input.end(),decoded.begin(),decoded.end());break;
 }
 }
}
static void receive_packet(const std::vector<byte> &p,const sockaddr_in &sender) {
 if (p.empty()) return;
 if (p[0]==3 && hosting && p.size()>=33) {
  bool first=!joined;
  if (joined && !same_peer(sender)) {send_raw({5,0},sender);return;}
  if (!joined) {
   peer=sender;joined=true;send_sequence=receive_sequence=0;
   auto end=std::find(p.begin()+17,p.begin()+33,byte(0));peer_name=std::string(p.begin()+17,end);
   msg.printf(1,"%s has joined",peer_name.c_str());
  }
  send_raw(identity(4),peer);last_seen=tick();if (first && romloaded) net_rom_loaded();return;
 }
 if (!same_peer(sender)) return;
 if (p[0]==4 && !hosting && !joined && p.size()>=33) {
  joined=true;auto end=std::find(p.begin()+17,p.begin()+33,byte(0));peer_name=std::string(p.begin()+17,end);
  msg.printf(1,"Connected to %s",peer_name.c_str());last_seen=tick();return;
 }
 if (!joined) {if (p[0]==5) {msg.error("Connection refused");net_disconnect();}return;}
 last_seen=tick();
 if (p[0]==6) {msg.printf(1,"Remote disconnected");net_disconnect();return;}
 if (p[0]==7 && p.size()>=5) {auto response=p;response[0]=8;send_raw(response,peer);return;}
 if (p[0]==8 && p.size()>=5) {
  uint32_t stamp;std::memcpy(&stamp,p.data()+1,4);ping_frames=uint32_t(tick()*60/1000)-stamp;return;
 }
 if (p[0]==2 && p.size()>=3) {pending.erase(word_at(p.data()+1));return;}
 if (p[0]!=1 || p.size()<4) return;
 uint16_t sequence=word_at(p.data()+1);std::vector<byte> ack{2};append_word(ack,sequence);send_raw(ack,peer);
 if (sequence<receive_sequence || unsigned(sequence-receive_sequence)>128) return;
 reordered.emplace(sequence,std::vector<byte>(p.begin()+3,p.end()));
 while (true) {
  auto it=reordered.find(receive_sequence);if (it==reordered.end()) break;
  auto data=it->second;reordered.erase(it);++receive_sequence;payload(data);if (!joined) break;
 }
}
void net_poll() {
 if (!net_active()) return;
 for (unsigned i=0;i<128 && net_active();++i) {
  std::array<byte,1024> buffer{};sockaddr_in sender{};socklen_t length=sizeof(sender);
  int n=recvfrom(connection,reinterpret_cast<char*>(buffer.data()),buffer.size(),0,reinterpret_cast<sockaddr*>(&sender),&length);
  if (n<0) break;
  receive_packet(std::vector<byte>(buffer.begin(),buffer.begin()+n),sender);
 }
 if (!net_active()) return;
 auto now=tick();
 if (!hosting && !joined) {
  if (now>connect_deadline) {msg.error("Connection timed out");net_disconnect();return;}
  if (now>=next_connect) {send_raw(identity(3),peer);next_connect=now+3333;}
 }
 if (joined) {
  if (now-last_seen>33333) {msg.error("Netplay peer timed out");net_disconnect();return;}
  if (now>=next_ping) {
   std::vector<byte> p{7};uint32_t stamp=now*60/1000;auto *bytes=reinterpret_cast<byte*>(&stamp);p.insert(p.end(),bytes,bytes+4);
   send_raw(p,peer);next_ping=now+5000;
  }
  for (auto &entry:pending) if (now>=entry.second.due) {
   send_raw(entry.second.bytes,peer);entry.second.due=now+std::max(5u,ping_frames+5)*1000/60;++retries;
  }
 }
}
// End of a --sync-frames run: acknowledge and retransmit, without acting on
// what arrives, until our reliable packets are through and the peer has said
// goodbye or gone quiet (while running it sends input every net_latency
// frames). Its ROM-freed and disconnect messages would otherwise stop it, or
// end our session before shutdown saves the battery.
void net_finish(unsigned quiet_ms,unsigned timeout_ms) {
 for (auto end=tick()+timeout_ms;joined && tick()<end;) {
  std::array<byte,1024> buffer{};sockaddr_in sender{};socklen_t length=sizeof(sender);
  int n=recvfrom(connection,reinterpret_cast<char*>(buffer.data()),buffer.size(),0,reinterpret_cast<sockaddr*>(&sender),&length);
  if (n>0) {
   if (!same_peer(sender)) continue;
   last_seen=tick();
   if (buffer[0]==6) return;
   if (buffer[0]==2 && n>=3) pending.erase(word_at(buffer.data()+1));
   if (buffer[0]==1 && n>=4) {std::vector<byte> ack{2};append_word(ack,word_at(buffer.data()+1));send_raw(ack,peer);}
   continue;
  }
  auto now=tick();
  if (pending.empty() && now-last_seen>=quiet_ms) return;
  for (auto &entry:pending) if (now>=entry.second.due) {
   send_raw(entry.second.bytes,peer);entry.second.due=now+std::max(5u,ping_frames+5)*1000/60;++retries;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
 }
}
static byte live_input() {
 auto *j=inputdevice[0];return (j->stat&15)|(j->but&0xf0);
}
static void set_input(unsigned i,byte b) {inputdevice[i]->stat=b&15;inputdevice[i]->but=b&0xf0;}
bool net_before_frame() {
 if (!net_active()) return true;
 if (!playing || !joined) return false;
 while (local_input.size()-send_cursor>=unsigned(net_latency)) {
  std::vector<byte> batch(local_input.begin()+send_cursor,local_input.begin()+send_cursor+net_latency);
  auto p=encode_net_inputs_042(batch);
  send_cursor+=net_latency;reliable(p);
 }
 if (remote_input.empty()) return false;
 local_input.push_back(live_input());
 set_input(local_player1?0:1,local_input.front());set_input(local_player1?1:0,remote_input.front());return true;
}
void net_after_frame() {
 if (!playing || !net_active()) return;
 ++net_frame_count;
 if (!local_input.empty()) {local_input.pop_front();if (send_cursor) --send_cursor;}
 if (!remote_input.empty()) remote_input.pop_front();
}
void net_chat(const std::string &text) {
 if (text.empty()) return;
 if (joined) {std::vector<byte> p{9};auto length=std::min(size_t(80),text.size());p.insert(p.end(),text.begin(),text.begin()+length);p.push_back(0);reliable(p);}
 netchat.printf(1,"> %s",text.c_str());
}
std::string net_status() {
 if (!net_active()) return "Disconnected";
 if (!joined) return hosting?"Waiting for connection":"Connecting";
 char text[160];std::snprintf(text,sizeof(text),"%s | ping %u | retry %u | pending %u",peer_name.c_str(),ping_frames,retries,unsigned(pending.size()));return text;
}
bool net_is_server() {return hosting;}
std::vector<net_peer_stats> net_peers() {
 if (!joined) return {};
 char address[48];inet_ntop(AF_INET,&peer.sin_addr,address,sizeof(address));
 return {{peer_name,address,ping_frames,retries,unsigned(pending.size())}};
}
