#pragma once
#include <string>
#include <vector>
// Windows 0.42's "-option" command line and NEStcl95.ini (see options042.cpp).
void options042_parse_line(const char *line);
void options042_add_arguments(const std::vector<std::string> &tokens);
void options042_read_file(const char *path);
void options042_execute(bool run_commands); // false: system options, true: commands
bool options042_flag(const char *name);     // a system option was given (-nosound)
void options042_write(const char *path);
extern std::string palette_file;
extern int sound_block_size;
