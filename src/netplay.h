#pragma once
#include <string>
#include <vector>
bool net_active();
bool net_connected();
void net_finish(unsigned quiet_ms,unsigned timeout_ms);
bool net_start(bool server,const std::string &address,unsigned port,const std::string &name);
void net_disconnect();
void net_poll();
bool net_before_frame();
void net_after_frame();
void net_rom_loaded();
void net_rom_freed();
void net_reset_requested();
void net_chat(const std::string &text);
std::string net_status();
bool net_is_server();
struct net_peer_stats {std::string name,address;unsigned ping,retry,pending;};
std::vector<net_peer_stats> net_peers();
extern int net_latency;
extern bool net_remote_action;
extern unsigned net_frame_count;
