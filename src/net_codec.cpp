#include "net_codec.h"

std::vector<uint8_t> encode_net_inputs_042(const std::vector<uint8_t> &input) {
 if (input.empty() || input.size()>255) return {};
 std::vector<uint8_t> packet{14,0};
 for (size_t i=0;i<input.size();) {
  size_t count=1;while (i+count<input.size() && input[i+count]==input[i]) ++count;
  packet.push_back(count);packet.push_back(input[i]);++packet[1];i+=count;
 }
 return packet;
}
bool decode_net_inputs_042(const uint8_t *packet,size_t size,std::vector<uint8_t> &output) {
 if (size<2 || packet[0]!=14 || size<size_t(packet[1])*2+2) return false;
 std::vector<uint8_t> decoded;
 for (unsigned i=0;i<packet[1];++i) {
  unsigned count=packet[2+i*2];if (decoded.size()+count>256) return false;
  decoded.insert(decoded.end(),count,packet[3+i*2]);
 }
 output=std::move(decoded);return true;
}
