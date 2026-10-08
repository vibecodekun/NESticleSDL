#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cstddef>

struct rom_patch {
 uint16_t address = 0;
 uint8_t value = 0;
 int compare = -1;
 bool enabled = false;
 std::string name;
 std::string code() const;
};
bool decode_patch_042(const std::string &code,rom_patch &patch);
const std::vector<rom_patch> &patches_042();
bool add_patch_042(const std::string &code,const std::string &name,bool enabled=false);
void toggle_patch_042(unsigned index);
void remove_patch_042(unsigned index);
void clear_patches_042();
void disable_patches_042();
bool load_patches_042(const std::string &path);
bool save_patches_042(const std::string &path);
void apply_patches_042();
void map_prg_042(unsigned address,const void *source,size_t size);
extern std::string patch_directory;
std::string patch_path_042();
