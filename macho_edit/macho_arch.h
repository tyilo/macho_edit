#pragma once

#include <vector>

#include <mach-o/fat.h>
#include <mach-o/loader.h>

#include "load_command.h"

class MachOArch {
public:
// Fields
	fat_arch fat_arch;
	mach_header mach_header;
	std::vector<LoadCommand> load_commands;

// Methods
	MachOArch();
	MachOArch(struct fat_arch *fat_arch, FILE *f);

	void swap_mach_header(struct mach_header *mh) const;

	std::string description() const;
	void print_load_commands() const;

	bool has_codesignature() const;
};
