#pragma once

#include <string>
#include <vector>

#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <stdio.h>

#include "macho_arch.h"

class MachO {
public:
// Fields
	std::FILE *file;
	int fd;
	uint32_t file_size;

	uint32_t fat_magic;

	bool is_fat;
	uint32_t n_archs;

	std::vector<MachOArch> archs;

// Methods
	MachO();
	MachO(const char *filename);

	void swap_arch(fat_arch *arch) const;

	void write_fat_header() const;
	void write_fat_archs();
	void write_mach_header(MachOArch &arch) const;
	void write_load_command(LoadCommand &lc) const;

	void print_description() const;

	fat_arch arch_from_mach_header(mach_header &mach_header, uint32_t size) const;

	void make_fat();
	void make_thin(uint32_t arch_index);

	bool save_arch_to_file(uint32_t arch_index, const char *filename) const;
	void remove_arch(uint32_t arch_index);
	void insert_arch_from_macho(MachO &macho, uint32_t arch_index);

	void remove_load_command(uint32_t arch_index, uint32_t lc_index);
	void move_load_command(uint32_t arch_index, uint32_t lc_index, uint32_t new_index);
	void insert_load_command(uint32_t arch_index, load_command *raw_lc);

	bool remove_codesignature(uint32_t arch_index);
};
