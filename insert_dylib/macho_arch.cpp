#include <iostream>
#include <sstream>

#include "cpuinfo.h"
#include "fileutils.h"
#include "macho_arch.h"
#include "macros.h"

MachOArch::MachOArch() {
}

MachOArch::MachOArch(struct fat_arch *fat_arch, FILE *f) {
	off_t original_offset = ftello(f);

	this->fat_arch = *fat_arch;

	fseeko(f, fat_arch->offset, SEEK_SET);
	PEEK(mach_header, f);

	uint32_t mh_magic = mach_header.magic;

	swap_mach_header(&mach_header);

	fseeko(f, MH_SIZE(mh_magic), SEEK_CUR);
	for(size_t i = 0; i < mach_header.ncmds; i++) {
		load_commands.push_back(LoadCommand(mh_magic, f));
	}

	fseeko(f, original_offset, SEEK_SET);
}

void MachOArch::swap_mach_header(struct mach_header *mh) const {
	uint32_t *fields = (uint32_t *)mh;

	// Don't swap magic (field 0)
	for(size_t i = 1; i < sizeof(mach_header) / sizeof(uint32_t); i++) {
		fields[i] = SWAP32(fields[i], mach_header.magic);
	}
}

std::string MachOArch::description() const {
	std::string name = cpu_name(fat_arch.cputype, fat_arch.cpusubtype);

	std::ostringstream o;
	o << name << " arch (offset 0x" << std::hex << fat_arch.offset << ")";
	return o.str();
}

void MachOArch::print_load_commands() const {
	uint32_t i = 0;
	for(auto &lc : load_commands) {
		std::cout << "\t" << i << ": " << lc.description() << "\n";
		i++;
	}
}