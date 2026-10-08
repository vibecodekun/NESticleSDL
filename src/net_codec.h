#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
std::vector<uint8_t> encode_net_inputs_042(const std::vector<uint8_t> &input);
bool decode_net_inputs_042(const uint8_t *packet,size_t size,std::vector<uint8_t> &output);
