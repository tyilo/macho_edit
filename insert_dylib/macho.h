#ifndef __insert_dylib__macho__
#define __insert_dylib__macho__

#include <string>
#include <vector>

#include <cstdio>
#include <mach-o/fat.h>
#include <mach-o/loader.h>

class MachO {
public:
// Fields
	std::FILE *file;
	int fd;
	uint32_t file_size;

	uint32_t fat_magic;

	bool is_fat;
	uint32_t n_archs;

	std::vector<struct fat_arch> archs;

	std::vector<struct mach_header> mach_headers;

// Methods
	MachO();
	MachO(const char *filename);

	void swap_arch(struct fat_arch *arch);
	void swap_mach_header(struct mach_header *mh);

	void write_fat_header();
	void write_fat_archs();

	std::string arch_description(struct fat_arch &arch);
	void print_description();

	struct fat_arch arch_from_mach_header(struct mach_header &mach_header, uint32_t size);

	void make_fat();
	void make_thin(uint32_t arch_index);

	std::string extract_arch(uint32_t arch_index);
	void remove_arch(uint32_t arch_index);
	void insert_arch(MachO &macho, uint32_t arch_index);
};

#endif