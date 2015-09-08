#include "macho.h"

#include "macros.h"
#include "fileutils.h"
#include "magicnames.h"
#include "cpuinfo.h"

#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <iostream>
#include <sstream>
#include <iomanip>

MachO::MachO() {
}

MachO::MachO(const char *filename) {
	file = fopen(filename, "r+");
	if(!file) {
		throw "Couldn't open file!";
	}

	fd = fileno(file);

	fseeko(file, 0, SEEK_END);
	off_t fsize = ftello(file);
	rewind(file);

	if(fsize > UINT32_MAX) {
		throw "File size larger than 2^32 bytes!";
	}

	file_size = (uint32_t)fsize;

	uint32_t magic;
	PEEK(magic, file);

	if(!IS_MAGIC(magic)) {
		std::ostringstream o;
		o << "Unknown magic: 0x" << std::hex << magic;
		throw o.str();
	}

	is_fat = IS_FAT(magic);

	if(is_fat) {
		fat_magic = magic;

		fat_header fat_header;
		READ(fat_header, file);
		n_archs = SWAP32(fat_header.nfat_arch, magic);

		for(uint32_t i = 0; i < n_archs; i++) {
			fat_arch arch;
			READ(arch, file);
			swap_arch(&arch);
			archs.push_back(MachOArch(arch, file));
		}
	} else {
		fat_magic = FAT_CIGAM;

		n_archs = 1;

		mach_header mh;
		READ(mh, file);

		fat_arch arch = arch_from_mach_header(mh, file_size);
		swap_arch(&arch);
		archs = {MachOArch(arch, file)};
	}
}

void MachO::swap_arch(fat_arch *arch) const {
	uint32_t *fields = (uint32_t *)arch;
	for(size_t i = 0; i < sizeof(*arch) / sizeof(uint32_t); i++) {
		fields[i] = SWAP32(fields[i], fat_magic);
	}
}

void MachO::write_fat_header() const {
	fat_header fat_header;

	fat_header.magic = fat_magic;
	fat_header.nfat_arch = SWAP32(n_archs, fat_magic);

	WRITE(fat_header, file);
}

void MachO::write_fat_archs() const {
	for(auto &arch : archs) {
		fat_arch raw_arch = arch.raw_arch;
		swap_arch(&raw_arch);
		WRITE(raw_arch, file);
	}
}

void MachO::print_description() const {
	if(is_fat) {
		std::cout << "Fat mach-o binary with " << n_archs << " archs:\n";
	} else {
		std::cout << "Thin mach-o binary:\n";
	}

	for(auto &arch : archs) {
		std::cout << "\t" << arch.description() << "\n";
	}
}

fat_arch MachO::arch_from_mach_header(mach_header &mh, uint32_t size) const {
	fat_arch arch;

	arch.offset = SWAP32(0, fat_magic);
	arch.size = SWAP32(size, fat_magic);

	cpu_type_t cputype = SWAP32(mh.cputype, mh.magic);
	arch.cputype = SWAP32(cputype, fat_magic);
	arch.cpusubtype = SWAP32(SWAP32(mh.cpusubtype, mh.magic), fat_magic);

	uint32_t align = cpu_pagesize(cputype);
	arch.align = SWAP32(align, fat_magic);

	return arch;
}

void MachO::make_fat() {
	assert(!is_fat);

	auto &arch = archs[0];

	uint32_t offset = ROUND_UP(sizeof(fat_header), 1 << arch.raw_arch.align);

	ftruncate(fd, file_size + offset);

	fmove(file, offset, 0, file_size);
	fzero(file, offset, 0);

	rewind(file);

	// dyld doesn't like FAT_MAGIC
	fat_magic = FAT_CIGAM;
	write_fat_header();

	arch.raw_arch.offset = offset;
	write_fat_archs();

	fflush(file);

	is_fat = true;
	file_size += offset;
}

void MachO::make_thin(uint32_t arch_index) {
	assert(is_fat);

	auto &arch = archs[arch_index];

	archs = {arch};

	uint32_t size = arch.raw_arch.size;
	fmove(file, 0, arch.raw_arch.offset, size);

	fflush(file);
	ftruncate(fd, size);

	file_size = size;
	n_archs = 1;
	is_fat = false;

	//swap_arch ????
}

bool MachO::save_arch_to_file(uint32_t arch_index, const char *filename) const {
	auto &arch = archs[arch_index];

	FILE *f = fopen(filename, "w");
	if(!f) {
		return false;
	}

	fcpy(f, 0, file, arch.raw_arch.offset, arch.raw_arch.size);

	fclose(f);

	chmod(filename, S_IRWXU);

	return true;
}

void MachO::remove_arch(uint32_t arch_index) {
	auto &arch = archs[arch_index];

	fzero(file, arch.raw_arch.offset, arch.raw_arch.size);

	uint32_t new_offset;
	if(arch_index == 0) {
		new_offset = sizeof(fat_header);
	} else {
		auto &prev = archs[arch_index - 1];
		new_offset = prev.raw_arch.offset + prev.raw_arch.size;
	}

	archs.erase(archs.begin() + arch_index);
	n_archs--;

	for(uint32_t i = arch_index; i < n_archs; i++) {
		auto &arch = archs[i];

		uint32_t offset = arch.raw_arch.offset;
		uint32_t size =  arch.raw_arch.size;

		new_offset = ROUND_UP(new_offset, 1 << arch.raw_arch.align);
		arch.raw_arch.offset = new_offset;

		fmove(file, new_offset, offset, size);
		fzero(file, new_offset + size, offset - new_offset);

		new_offset += size;
	}

	rewind(file);

	write_fat_header();
	write_fat_archs();

	fflush(file);
	ftruncate(fd, new_offset);

	file_size = new_offset;
}

void MachO::insert_arch_from_macho(MachO &macho, uint32_t arch_index) {
	n_archs++;

	MachOArch arch = macho.archs[arch_index];
	fat_arch &raw_arch = arch.raw_arch;

	macho.swap_arch(&raw_arch);
	swap_arch(&raw_arch);

	//arch.swap_mach_header(); ????

	uint32_t offset = ROUND_UP(file_size, 1 << raw_arch.align);

	raw_arch.offset = offset;

	archs.push_back(arch);

	uint32_t new_size = file_size + offset;

	ftruncate(fd, new_size);
	fzero(file, file_size, offset - file_size);

	fcpy(file, offset, macho.file, 0, raw_arch.size);

	file_size = new_size;

	rewind(file);
	write_fat_header();
	write_fat_archs();
}

void MachO::remove_load_command(uint32_t arch_index, uint32_t lc_index) {
	/*mach_header &mh = mach_headers[arch_index];

	load_command *lc = load_commands[arch_index][lc_index];
*/

}

void MachO::move_load_command(uint32_t arch_index, uint32_t lc_index, uint32_t new_index) {
	/*if(lc_index == new_index) {
		return;
	}
	if(lc_index > new_index) {
		move_load_command(arch_index, new_index, lc_index);
		return;
	}

	mach_header &mh = mach_headers[arch_index];
	uint32_t magic = mh.magic;
	load_command *prev = load_commands[arch_index][lc_index];

	for(uint32_t i = lc_index + 1; i < new_index; i++) {
		load_command *lc = load_commands[arch_index][i];

	}*/
}
