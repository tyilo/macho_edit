#include "macho.h"

#include "macros.h"
#include "fileutils.h"
#include "cpuinfo.h"

#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <iostream>
#include <sstream>

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

		struct fat_header fat_header;
		READ(fat_header, file);
		n_archs = SWAP32(fat_header.nfat_arch, magic);

		archs.resize(n_archs);
		for(uint32_t i = 0; i < n_archs; i++) {
			READ(archs[i], file);
			swap_arch(&archs[i]);
		}

		mach_headers.resize(n_archs);
		for(uint32_t i = 0; i < n_archs; i++) {
			fseek(file, archs[i].offset, SEEK_SET);
			READ(mach_headers[i], file);
			swap_mach_header(&mach_headers[i]);
		}
	} else {
		fat_magic = FAT_CIGAM;

		n_archs = 1;

		struct mach_header mh;
		READ(mh, file);

		struct fat_arch arch = arch_from_mach_header(mh, file_size);

		swap_arch(&arch);
		swap_mach_header(&mh);

		archs = {arch};
		mach_headers = {mh};
	}
}

void MachO::swap_arch(struct fat_arch *arch) {
	uint32_t *fields = (uint32_t *)arch;
	for(size_t i = 0; i < sizeof(*arch) / sizeof(uint32_t); i++) {
		fields[i] = SWAP32(fields[i], fat_magic);
	}
}

void MachO::swap_mach_header(struct mach_header *mh) {
	uint32_t *fields = (uint32_t *)mh;

	// Don't swap magic (field 0)
	for(size_t i = 1; i < sizeof(*mh) / sizeof(uint32_t); i++) {
		fields[i] = SWAP32(fields[i], mh->magic);
	}
}

void MachO::write_fat_header() {
	struct fat_header fat_header;

	fat_header.magic = fat_magic;
	fat_header.nfat_arch = SWAP32(n_archs, fat_magic);

	WRITE(fat_header, file);
}

void MachO::write_fat_archs() {
	for(auto arch : archs) {
		swap_arch(&arch);
		WRITE(arch, file);
	}
}

std::string MachO::arch_description(struct fat_arch &arch) {
	std::string name = cpu_name(arch.cputype, arch.cpusubtype);

	std::ostringstream o;
	o << name << " arch (offset 0x" << std::hex << arch.offset << ")";
	return o.str();
}

void MachO::print_description() {
	if(is_fat) {
		std::cout << "Fat mach-o binary with " << n_archs << " archs:\n";
	} else {
		std::cout << "Thin mach-o binary:\n";
	}

	for(auto &arch : archs) {
		std::cout << "\t" << arch_description(arch) << "\n";
	}
}

#if 0
void print_commands(FILE *f, size_t header_offset, int indent) {
	fseeko(f, header_offset, SEEK_SET);

	struct mach_header mh;
	fread(&mh, sizeof(struct mach_header), 1, f);

	printf_indent(indent, "%s executable\n", cpu_name(SWAP32(mh.cputype, mh.magic), SWAP32(mh.cpusubtype, mh.magic)));

	if(IS_64_BIT(mh.magic)) {
		fseeko(f, sizeof(struct mach_header_64) - sizeof(mh), SEEK_CUR);
	}

	uint32_t ncmds = SWAP32(mh.ncmds, mh.magic);
	for(uint32_t i = 0; i < ncmds; i++) {
		struct load_command lc;
		fread(&lc, sizeof(lc), 1, f);

		uint32_t cmd = SWAP32(lc.cmd, mh.magic);
		printf_indent(indent + 1, "%s\n", cmd_name(cmd));

		fseeko(f, SWAP32(lc.cmdsize, mh.magic) - sizeof(lc), SEEK_CUR);
	}
}
#endif

struct fat_arch MachO::arch_from_mach_header(struct mach_header &mh, uint32_t size) {
	struct fat_arch arch;

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

	uint32_t offset = ROUND_UP(sizeof(fat_header), 1 << arch.align);

	ftruncate(fd, file_size + offset);

	fmove(file, offset, 0, file_size);
	fzero(file, offset, 0);

	rewind(file);

	// dyld doesn't like FAT_MAGIC
	fat_magic = FAT_CIGAM;
	write_fat_header();

	arch.offset = offset;
	write_fat_archs();

	fflush(file);

	is_fat = true;
	file_size += offset;
}

void MachO::make_thin(uint32_t arch_index) {
	assert(is_fat);

	archs = {archs[arch_index]};
	mach_headers = {mach_headers[arch_index]};

	auto &arch = archs[0];

	uint32_t size = arch.size;
	fmove(file, 0, arch.offset, size);

	fflush(file);
	ftruncate(fd, size);

	file_size = size;
	n_archs = 1;
	is_fat = false;

	swap_arch(&arch);
}

std::string MachO::extract_arch(uint32_t arch_index) {
	auto &arch = archs[arch_index];

	char output[] = "/tmp/macho-XXXXXX";
	int fd = mkstemp(output);
	fchmod(fd, S_IRWXU);

	FILE *tmp = fdopen(fd, "w");

	fcpy(tmp, 0, file, arch.offset, arch.size);

	fclose(tmp);

	return std::string(output);
}

void MachO::remove_arch(uint32_t arch_index) {
	auto &arch = archs[arch_index];

	fzero(file, arch.offset, arch.size);

	uint32_t new_offset;
	if(arch_index == 0) {
		new_offset = sizeof(fat_header);
	} else {
		auto &prev = archs[arch_index - 1];
		new_offset = prev.offset - prev.size;
	}

	archs.erase(archs.begin() + arch_index);
	mach_headers.erase(mach_headers.begin() + arch_index);
	n_archs--;

	for(uint32_t i = arch_index; i < n_archs; i++) {
		auto &arch = archs[i];

		uint32_t offset = arch.offset;
		uint32_t size =  arch.size;

		new_offset = ROUND_UP(new_offset, 1 << arch.align);
		arch.offset = new_offset;

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

void MachO::insert_arch(MachO &macho, uint32_t arch_index) {
	n_archs++;

	struct fat_arch arch = macho.archs[arch_index];
	macho.swap_arch(&arch);

	swap_arch(&arch);

	archs.push_back(arch);

	uint32_t offset = ROUND_UP(file_size, 1 << arch.align);
	uint32_t new_size = file_size + offset;

	ftruncate(fd, new_size);
	fzero(file, file_size, offset - file_size);

	fcpy(file, offset, macho.file, 0, arch.size);

	file_size = new_size;

	rewind(file);
	write_fat_header();
	write_fat_archs();
}
