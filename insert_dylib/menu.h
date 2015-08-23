#ifndef __insert_dylib__menu__
#define __insert_dylib__menu__

#include "macho.h"

#include <string>
#include <vector>

__attribute__((format(printf, 1, 2))) bool ask(const char *format, ...);
template <typename T>
bool readline(T &line);
size_t select_option(const char *header, std::vector<std::string> options);
uint32_t select_executable(MachO &macho, const char *header, bool allow_all);
void fat_config(MachO &macho);
void lc_config(MachO &macho);
bool main_menu(MachO &macho);

#endif
