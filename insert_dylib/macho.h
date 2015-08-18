#ifndef __insert_dylib__macho__
#define __insert_dylib__macho__

#include <string>
#include <vector>
#include <fstream>
#include <mach-o/fat.h>
#include <mach-o/loader.h>

class MachO {
	std::fstream file;

public:
	bool is_fat;
	struct fat_header fat_header;
	std::vector<struct fat_arch> archs;

	std::vector<struct mach_header> mach_headers;

	MachO(std::string &filename);
};

#endif