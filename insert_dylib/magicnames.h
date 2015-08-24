#ifndef insert_dylib_magicnames_h
#define insert_dylib_magicnames_h

#include <stdint.h>

#include <string>

std::string magic_name(uint32_t magic);
std::string cmd_name(uint32_t cmd);

#endif
