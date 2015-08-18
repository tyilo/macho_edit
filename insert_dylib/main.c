#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <copyfile.h>
#include <mach-o/loader.h>
#include <mach-o/fat.h>

#include "macros.h"
#include "fileutils.h"

__attribute__((noreturn)) void usage(void) {
	puts("Usage: insert_dylib binary_path");

	exit(1);
}

__attribute__((format(printf, 1, 2))) bool ask(const char *format, ...) {
	char *question;
	asprintf(&question, "%s [y/n] ", format);

	va_list args;
	va_start(args, format);
	vprintf(question, args);
	va_end(args);

	free(question);

	while(true) {
		char *line = NULL;
		size_t size;
		getline(&line, &size, stdin);

		switch(line[0]) {
			case 'y':
			case 'Y':
				return true;
				break;
			case 'n':
			case 'N':
				return false;
				break;
			default:
				printf("Please enter y or n: ");
		}
	}
}

size_t select_option(const char *header, char **options, size_t noptions) {
	puts(header);

	for(size_t i = 0; i < noptions; i++) {
		printf("%zu: %s\n", i + 1, options[i]);
	}

	printf("Select an option: ");

	while(true) {
		char *line = NULL;
		size_t size;
		getline(&line, &size, stdin);

		size_t o = strtoull(line, NULL, 10);

		if(1 <= o && o <= noptions) {
			return o - 1;
		}

		printf("Please enter a valid option: ");
	}
}

void *read_load_command(FILE *f, uint32_t cmdsize) {
	void *lc = malloc(cmdsize);

	fpeek(lc, cmdsize, 1, f);

	return lc;
}

/*
bool check_load_commands(FILE *f, struct mach_header *mh, size_t header_offset, size_t commands_offset, const char *dylib_path, off_t *slice_size) {
	fseeko(f, commands_offset, SEEK_SET);

	uint32_t ncmds = SWAP32(mh->ncmds, mh->magic);

	off_t linkedit_32_pos = -1;
	off_t linkedit_64_pos = -1;
	struct segment_command linkedit_32;
	struct segment_command_64 linkedit_64;

	off_t symtab_pos = -1;
	uint32_t symtab_size = 0;

	for(int i = 0; i < ncmds; i++) {
		struct load_command lc;
		fpeek(&lc, sizeof(lc), 1, f);

		uint32_t cmdsize = SWAP32(lc.cmdsize, mh->magic);
		uint32_t cmd = SWAP32(lc.cmd, mh->magic);

		switch(cmd) {
			case LC_CODE_SIGNATURE:
				if(i == ncmds - 1) {
					if(codesig_flag == 2) {
						return true;
					}

					if(codesig_flag == 0 && !ask("LC_CODE_SIGNATURE load command found. Remove it?")) {
						return true;
					}

					struct linkedit_data_command *cmd = read_load_command(f, cmdsize);

					fbzero(f, ftello(f), cmdsize);

					uint32_t dataoff = SWAP32(cmd->dataoff, mh->magic);
					uint32_t datasize = SWAP32(cmd->datasize, mh->magic);

					free(cmd);

					uint64_t linkedit_fileoff = 0;
					uint64_t linkedit_filesize = 0;

					if(linkedit_32_pos != -1) {
						linkedit_fileoff = SWAP32(linkedit_32.fileoff, mh->magic);
						linkedit_filesize = SWAP32(linkedit_32.filesize, mh->magic);
					} else if(linkedit_64_pos != -1) {
						linkedit_fileoff = SWAP64(linkedit_64.fileoff, mh->magic);
						linkedit_filesize = SWAP64(linkedit_64.filesize, mh->magic);
					} else {
						fprintf(stderr, "Warning: __LINKEDIT segment not found.\n");
					}

					if(linkedit_32_pos != -1 || linkedit_64_pos != -1) {
						if(linkedit_fileoff + linkedit_filesize != *slice_size) {
							fprintf(stderr, "Warning: __LINKEDIT segment is not at the end of the file, so codesign will not work on the patched binary.\n");
						} else {
							if(dataoff + datasize != *slice_size) {
								fprintf(stderr, "Warning: Codesignature is not at the end of __LINKEDIT segment, so codesign will not work on the patched binary.\n");
							} else {
								*slice_size -= datasize;
								//int64_t diff_size = 0;
								if(symtab_pos == -1) {
									fprintf(stderr, "Warning: LC_SYMTAB load command not found. codesign might not work on the patched binary.\n");
								} else {
									fseeko(f, symtab_pos, SEEK_SET);
									struct symtab_command *symtab = read_load_command(f, symtab_size);

									uint32_t strsize = SWAP32(symtab->strsize, mh->magic);
									int64_t diff_size = SWAP32(symtab->stroff, mh->magic) + strsize - (int64_t)*slice_size;
									if(-0x10 <= diff_size && diff_size <= 0) {
										symtab->strsize = SWAP32((uint32_t)(strsize - diff_size), mh->magic);
										fwrite(symtab, symtab_size, 1, f);
									} else {
										fprintf(stderr, "Warning: String table doesn't appear right before code signature. codesign might not work on the patched binary. (0x%llx)\n", diff_size);
									}

									free(symtab);
								}

								linkedit_filesize -= datasize;
								uint64_t linkedit_vmsize = ROUND_UP(linkedit_filesize, 0x1000);

								if(linkedit_32_pos != -1) {
									linkedit_32.filesize = SWAP32((uint32_t)linkedit_filesize, mh->magic);
									linkedit_32.vmsize = SWAP32((uint32_t)linkedit_vmsize, mh->magic);

									fseeko(f, linkedit_32_pos, SEEK_SET);
									fwrite(&linkedit_32, sizeof(linkedit_32), 1, f);
								} else {
									linkedit_64.filesize = SWAP64(linkedit_filesize, mh->magic);
									linkedit_64.vmsize = SWAP64(linkedit_vmsize, mh->magic);

									fseeko(f, linkedit_64_pos, SEEK_SET);
									fwrite(&linkedit_64, sizeof(linkedit_64), 1, f);
								}

								goto fix_header;
							}
						}
					}

					// If we haven't truncated the file, zero out the code signature
					fbzero(f, header_offset + dataoff, datasize);

				fix_header:
					mh->ncmds = SWAP32(ncmds - 1, mh->magic);
					mh->sizeofcmds = SWAP32(SWAP32(mh->sizeofcmds, mh->magic) - cmdsize, mh->magic);

					return true;
				} else {
					printf("LC_CODE_SIGNATURE is not the last load command, so couldn't remove.\n");
				}
				break;
			case LC_LOAD_DYLIB:
			case LC_LOAD_WEAK_DYLIB: {
				struct dylib_command *dylib_command = read_load_command(f, cmdsize);

				union lc_str offset = dylib_command->dylib.name;
				char *name = &((char *)dylib_command)[SWAP32(offset.offset, mh->magic)];

				int cmp = strcmp(name, dylib_path);

				free(dylib_command);

				if(cmp == 0) {
					if(!ask("Binary already contains a load command for that dylib. Continue anyway?")) {
						return false;
					}
				}

				break;
			}
			case LC_SEGMENT:
			case LC_SEGMENT_64:
				if(cmd == LC_SEGMENT) {
					struct segment_command *cmd = read_load_command(f, cmdsize);
					if(strcmp(cmd->segname, "__LINKEDIT") == 0) {
						linkedit_32_pos = ftello(f);
						linkedit_32 = *cmd;
					}
					free(cmd);
				} else {
					struct segment_command_64 *cmd = read_load_command(f, cmdsize);
					if(strcmp(cmd->segname, "__LINKEDIT") == 0) {
						linkedit_64_pos = ftello(f);
						linkedit_64 = *cmd;
					}
					free(cmd);
				}
			case LC_SYMTAB:
				symtab_pos = ftello(f);
				symtab_size = cmdsize;
		}

		fseeko(f, SWAP32(lc.cmdsize, mh->magic), SEEK_CUR);
	}

	return true;
}

bool insert_dylib(FILE *f, size_t header_offset, const char *dylib_path, off_t *slice_size) {
	fseeko(f, header_offset, SEEK_SET);

	struct mach_header mh;
	fread(&mh, sizeof(struct mach_header), 1, f);

	if(mh.magic != MH_MAGIC_64 && mh.magic != MH_CIGAM_64 && mh.magic != MH_MAGIC && mh.magic != MH_CIGAM) {
		printf("Unknown magic: 0x%x\n", mh.magic);
		return false;
	}

	size_t commands_offset = header_offset + (IS_64_BIT(mh.magic)? sizeof(struct mach_header_64): sizeof(struct mach_header));

	bool cont = check_load_commands(f, &mh, header_offset, commands_offset, dylib_path, slice_size);

	if(!cont) {
		return true;
	}

	// Even though a padding of 4 works for x86_64, codesign doesn't like it
	size_t path_padding = 8;

	size_t dylib_path_len = strlen(dylib_path);
	size_t dylib_path_size = (dylib_path_len & ~(path_padding - 1)) + path_padding;
	uint32_t cmdsize = (uint32_t)(sizeof(struct dylib_command) + dylib_path_size);

	struct dylib_command dylib_command = {
		.cmd = SWAP32(weak_flag? LC_LOAD_WEAK_DYLIB: LC_LOAD_DYLIB, mh.magic),
		.cmdsize = SWAP32(cmdsize, mh.magic),
		.dylib = {
			.name = SWAP32(sizeof(struct dylib_command), mh.magic),
			.timestamp = 0,
			.current_version = 0,
			.compatibility_version = 0
		}
	};

	uint32_t sizeofcmds = SWAP32(mh.sizeofcmds, mh.magic);

	fseeko(f, commands_offset + sizeofcmds, SEEK_SET);
	char space[cmdsize];

	fread(&space, cmdsize, 1, f);

	bool empty = true;
	for(int i = 0; i < cmdsize; i++) {
		if(space[i] != 0) {
			empty = false;
			break;
		}
	}

	if(!empty) {
		if(!ask("It doesn't seem like there is enough empty space. Continue anyway?")) {
			return false;
		}
	}

	fseeko(f, -((off_t)cmdsize), SEEK_CUR);

	char *dylib_path_padded = calloc(dylib_path_size, 1);
	memcpy(dylib_path_padded, dylib_path, dylib_path_len);

	fwrite(&dylib_command, sizeof(dylib_command), 1, f);
	fwrite(dylib_path_padded, dylib_path_size, 1, f);

	free(dylib_path_padded);

	mh.ncmds = SWAP32(SWAP32(mh.ncmds, mh.magic) + 1, mh.magic);
	sizeofcmds += cmdsize;
	mh.sizeofcmds = SWAP32(sizeofcmds, mh.magic);

	fseeko(f, header_offset, SEEK_SET);
	fwrite(&mh, sizeof(mh), 1, f);

	return true;
}
*/

void remove_cmd(FILE *f, uint32_t magic, ) {

}

void remove_codesig(FILE *f, uint32_t magic, struct fat_arch *arch, struct mach_header *mh) {
	fseeko(f, SWAP32(arch->offset, magic), SEEK_SET);

	uint32_t size = SWAP32(arch->size, magic);

	uint32_t ncmds = SWAP32(mh->ncmds, mh->magic);

	off_t linkedit_32_pos = -1;
	off_t linkedit_64_pos = -1;
	struct segment_command linkedit_32;
	struct segment_command_64 linkedit_64;

	off_t symtab_pos = -1;
	uint32_t symtab_size = 0;

	for(int i = 0; i < ncmds; i++) {
		struct load_command lc;
		fpeek(&lc, sizeof(lc), 1, f);

		uint32_t cmdsize = SWAP32(lc.cmdsize, mh->magic);
		uint32_t cmd = SWAP32(lc.cmd, mh->magic);

		switch(cmd) {
			case LC_CODE_SIGNATURE:
				if(i == ncmds - 1) {
					struct linkedit_data_command *cmd = read_load_command(f, cmdsize);

					fzero(f, ftello(f), cmdsize);

					uint32_t dataoff = SWAP32(cmd->dataoff, mh->magic);
					uint32_t datasize = SWAP32(cmd->datasize, mh->magic);

					free(cmd);

					uint64_t linkedit_fileoff = 0;
					uint64_t linkedit_filesize = 0;

					if(linkedit_32_pos != -1) {
						linkedit_fileoff = SWAP32(linkedit_32.fileoff, mh->magic);
						linkedit_filesize = SWAP32(linkedit_32.filesize, mh->magic);
					} else if(linkedit_64_pos != -1) {
						linkedit_fileoff = SWAP64(linkedit_64.fileoff, mh->magic);
						linkedit_filesize = SWAP64(linkedit_64.filesize, mh->magic);
					} else {
						fprintf(stderr, "Warning: __LINKEDIT segment not found.\n");
					}

					if(linkedit_32_pos != -1 || linkedit_64_pos != -1) {
						if(linkedit_fileoff + linkedit_filesize != size) {
							fprintf(stderr, "Warning: __LINKEDIT segment is not at the end of the file, so codesign will not work on the patched binary.\n");
						} else {
							if(dataoff + datasize != size) {
								fprintf(stderr, "Warning: Code signature is not at the end of __LINKEDIT segment, so codesign will not work on the patched binary.\n");
							} else {
								size -= datasize;
								if(symtab_pos == -1) {
									fprintf(stderr, "Warning: LC_SYMTAB load command not found. codesign might not work on the patched binary.\n");
								} else {
									fseeko(f, symtab_pos, SEEK_SET);
									struct symtab_command *symtab = read_load_command(f, symtab_size);

									uint32_t strsize = SWAP32(symtab->strsize, mh->magic);
									int64_t diff_size = SWAP32(symtab->stroff, mh->magic) + strsize - (int64_t)*slice_size;
									if(-0x10 <= diff_size && diff_size <= 0) {
										symtab->strsize = SWAP32((uint32_t)(strsize - diff_size), mh->magic);
										fwrite(symtab, symtab_size, 1, f);
									} else {
										fprintf(stderr, "Warning: String table doesn't appear right before code signature. codesign might not work on the patched binary. (0x%llx)\n", diff_size);
									}

									free(symtab);
								}

								linkedit_filesize -= datasize;
								uint64_t linkedit_vmsize = ROUND_UP(linkedit_filesize, 0x1000);

								if(linkedit_32_pos != -1) {
									linkedit_32.filesize = SWAP32((uint32_t)linkedit_filesize, mh->magic);
									linkedit_32.vmsize = SWAP32((uint32_t)linkedit_vmsize, mh->magic);

									fseeko(f, linkedit_32_pos, SEEK_SET);
									fwrite(&linkedit_32, sizeof(linkedit_32), 1, f);
								} else {
									linkedit_64.filesize = SWAP64(linkedit_filesize, mh->magic);
									linkedit_64.vmsize = SWAP64(linkedit_vmsize, mh->magic);

									fseeko(f, linkedit_64_pos, SEEK_SET);
									fwrite(&linkedit_64, sizeof(linkedit_64), 1, f);
								}

								goto fix_header;
							}
						}
					}

					// If we haven't truncated the file, zero out the code signature
					fzero(f, header_offset + dataoff, datasize);

				fix_header:
					mh->ncmds = SWAP32(ncmds - 1, mh->magic);
					mh->sizeofcmds = SWAP32(SWAP32(mh->sizeofcmds, mh->magic) - cmdsize, mh->magic);
				} else {
					printf("LC_CODE_SIGNATURE is not the last load command, so couldn't remove.\n");
				}
				break;
			case LC_SEGMENT:
			case LC_SEGMENT_64:
				if(cmd == LC_SEGMENT) {
					struct segment_command *cmd = read_load_command(f, cmdsize);
					if(strcmp(cmd->segname, "__LINKEDIT") == 0) {
						linkedit_32_pos = ftello(f);
						linkedit_32 = *cmd;
					}
					free(cmd);
				} else {
					struct segment_command_64 *cmd = read_load_command(f, cmdsize);
					if(strcmp(cmd->segname, "__LINKEDIT") == 0) {
						linkedit_64_pos = ftello(f);
						linkedit_64 = *cmd;
					}
					free(cmd);
				}
			case LC_SYMTAB:
				symtab_pos = ftello(f);
				symtab_size = cmdsize;
		}

		fseeko(f, SWAP32(lc.cmdsize, mh->magic), SEEK_CUR);
	}
}

#define RET_NAME(x) \
	case x: \
		return #x

const char *magic_name(uint32_t magic) {
	switch(magic) {
		RET_NAME(FAT_MAGIC);
		RET_NAME(FAT_CIGAM);
		RET_NAME(MH_MAGIC);
		RET_NAME(MH_MAGIC_64);
		RET_NAME(MH_CIGAM);
		RET_NAME(MH_CIGAM_64);
	}

	char *unknown;
	asprintf(&unknown, "UNKNOWN_MAGIC (0x%x)", magic);
	return unknown;
}

const char *cmd_name(uint32_t cmd) {
	switch(cmd) {
		RET_NAME(LC_SEGMENT);
		RET_NAME(LC_SYMTAB);
		RET_NAME(LC_SYMSEG);
		RET_NAME(LC_THREAD);
		RET_NAME(LC_UNIXTHREAD);
		RET_NAME(LC_LOADFVMLIB);
		RET_NAME(LC_IDFVMLIB);
		RET_NAME(LC_IDENT);
		RET_NAME(LC_FVMFILE);
		RET_NAME(LC_PREPAGE);
		RET_NAME(LC_DYSYMTAB);
		RET_NAME(LC_LOAD_DYLIB);
		RET_NAME(LC_ID_DYLIB);
		RET_NAME(LC_LOAD_DYLINKER);
		RET_NAME(LC_ID_DYLINKER);
		RET_NAME(LC_PREBOUND_DYLIB);
		RET_NAME(LC_ROUTINES);
		RET_NAME(LC_SUB_FRAMEWORK);
		RET_NAME(LC_SUB_UMBRELLA);
		RET_NAME(LC_SUB_CLIENT);
		RET_NAME(LC_SUB_LIBRARY);
		RET_NAME(LC_TWOLEVEL_HINTS);
		RET_NAME(LC_PREBIND_CKSUM);
		RET_NAME(LC_LOAD_WEAK_DYLIB);
		RET_NAME(LC_SEGMENT_64);
		RET_NAME(LC_ROUTINES_64);
		RET_NAME(LC_UUID);
		RET_NAME(LC_RPATH);
		RET_NAME(LC_CODE_SIGNATURE);
		RET_NAME(LC_SEGMENT_SPLIT_INFO);
		RET_NAME(LC_REEXPORT_DYLIB);
		RET_NAME(LC_LAZY_LOAD_DYLIB);
		RET_NAME(LC_ENCRYPTION_INFO);
		RET_NAME(LC_DYLD_INFO);
		RET_NAME(LC_DYLD_INFO_ONLY);
		RET_NAME(LC_LOAD_UPWARD_DYLIB);
		RET_NAME(LC_VERSION_MIN_MACOSX);
		RET_NAME(LC_VERSION_MIN_IPHONEOS);
		RET_NAME(LC_FUNCTION_STARTS);
		RET_NAME(LC_DYLD_ENVIRONMENT);
		RET_NAME(LC_MAIN);
		RET_NAME(LC_DATA_IN_CODE);
		RET_NAME(LC_SOURCE_VERSION);
		RET_NAME(LC_DYLIB_CODE_SIGN_DRS);
		RET_NAME(LC_ENCRYPTION_INFO_64);
		RET_NAME(LC_LINKER_OPTION);
		RET_NAME(LC_LINKER_OPTIMIZATION_HINT);
	}

	char *unknown;
	asprintf(&unknown, "UNKNOWN_COMMAND (0x%x)", cmd);
	return unknown;
}

const char *cpu_name(cpu_type_t cpu_type, cpu_subtype_t cpu_subtype) {
	switch(cpu_type) {
		case CPU_TYPE_POWERPC:
			return "ppc";
		case CPU_TYPE_POWERPC64:
			return "ppc64";
		case CPU_TYPE_X86:
			return "i386";
		case CPU_TYPE_X86_64:
			return "x86_64";
		case CPU_TYPE_ARM:
			switch(cpu_subtype) {
				case CPU_SUBTYPE_ARM_V6:
					return "armv6";
				case CPU_SUBTYPE_ARM_V7:
					return "armv7";
				case CPU_SUBTYPE_ARM_V7S:
					return "armv7s";
				case CPU_SUBTYPE_ARM_V8:
					return "armv8";
			}
			return "arm";
		case CPU_TYPE_ARM64:
			return "arm64";
	}

	char *unknown;
	asprintf(&unknown, "unknown (0x%x, 0x%x)", cpu_type, cpu_subtype);
	return unknown;
}

void __attribute__((format(printf, 2, 3))) printf_indent(int indent, const char *format, ...) {
	char *indented_format;
	asprintf(&indented_format, "%*s%s", indent * 2, "", format);

	va_list args;
	va_start(args, format);
	vprintf(indented_format, args);
	va_end(args);

	free(indented_format);
}

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

char *executable_description(uint32_t magic, struct fat_arch *arch) {
	char *buf;
	asprintf(&buf, "%s executable (offset 0x%x)", cpu_name(SWAP32(arch->cputype, magic), SWAP32(arch->cpusubtype, magic)), SWAP32(arch->offset, magic));
	return buf;
}

#define CANCEL (-1)
#define ALL (-2)

uint32_t select_executable(uint32_t magic, struct fat_arch *archs, uint32_t nfat_arch, char *header, bool allow_all) {
	char **executable_options = malloc(sizeof(*executable_options) * (nfat_arch + 1));
	for(uint32_t i = 0; i < nfat_arch; i++) {
		executable_options[i] = executable_description(magic, archs + i);
	}
	uint32_t cancel_index = nfat_arch;
	if(allow_all) {
		executable_options[cancel_index] = "All executables";
		cancel_index++;
	}
	executable_options[nfat_arch] = "Cancel";

	uint32_t o = (uint32_t)select_option(header, executable_options, cancel_index + 1);

	for(uint32_t i = 0; i < nfat_arch; i++) {
		free(executable_options[i]);
	}
	free(executable_options);

	if(o == cancel_index) {
		return CANCEL;
	}
	if(allow_all && o == cancel_index - 1) {
		return ALL;
	}

	return o;
}

uint32_t get_align(cpu_type_t cputype) {
	switch(cputype) {
		case CPU_TYPE_POWERPC:
		case CPU_TYPE_POWERPC64:
		case CPU_TYPE_X86:
		case CPU_TYPE_X86_64:
			return 12;
		case CPU_TYPE_ARM:
		case CPU_TYPE_ARM64:
		default:
			return 14;
	}
}

uint32_t init_arch_from_mh(uint32_t magic, struct mach_header *mh, struct fat_arch *arch) {
	cpu_type_t cputype = SWAP32(mh->cputype, mh->magic);
	arch->cputype = SWAP32(cputype, magic);
	arch->cpusubtype = SWAP32(SWAP32(mh->cpusubtype, mh->magic), magic);

	uint32_t align = get_align(cputype);
	arch->align = SWAP32(align, magic);

	return 1 << align;
}

int main(int argc, const char *argv[]) {
	if(argc != 2) {
		usage();
	}

	const char *binary_path = argv[1];

	struct stat s;

	if(stat(binary_path, &s) != 0) {
		perror(binary_path);
		exit(1);
	}

	FILE *f = fopen(binary_path, "r+");

	if(!f) {
		printf("Couldn't open file %s\n", argv[1]);
		exit(1);
	}

	bool success = true;

	fseeko(f, 0, SEEK_END);
	off_t file_size = ftello(f);
	rewind(f);

	uint32_t magic;
	fread(&magic, sizeof(uint32_t), 1, f);

	if(!IS_MAGIC(magic)) {
		fprintf(stderr, "Unknown magic: 0x%x\n", magic);
		return 1;
	}

	bool is_fat = IS_FAT(magic);

	struct fat_header fh;
	uint32_t nfat_arch;
	struct fat_arch *archs;

	if(is_fat) {
		fseeko(f, 0, SEEK_SET);
		fread(&fh, sizeof(fh), 1, f);

		nfat_arch = SWAP32(fh.nfat_arch, magic);

		size_t archs_size = sizeof(*archs) * nfat_arch;
		archs = malloc(archs_size);
		fread(archs, archs_size, 1, f);
	} else {
		nfat_arch = 1;

		archs = malloc(sizeof(*archs));
		archs[0].offset = SWAP32(0, magic);
		archs[0].size = SWAP32((uint32_t)file_size, magic);
	}

	int indent = 0;

	if(is_fat) {
		printf_indent(indent, "Fat mach-o binary with %u archs:\n", nfat_arch);
	} else {
		printf_indent(indent, "Thin mach-o binary:\n");
	}

	struct mach_header *mach_headers = malloc(sizeof(struct mach_header) * nfat_arch);

	for(uint32_t i = 0; i < nfat_arch; i++) {
		fseeko(f, SWAP32(archs[i].offset, magic), SEEK_SET);
		fread(mach_headers + i, sizeof(*mach_headers), 1, f);

		printf_indent(indent + 1, "%s\n", executable_description(magic, archs + i));
		//print_commands(f, SWAP32(archs[i].offset, magic), indent + 1);
	}

	while(true) {
		char *main_options[] = {"Fat mach-o configuration", "Load command edit", "Exit"};
		switch(select_option("", main_options, ELEMENTS(main_options))) {
			case 0:
				if(!is_fat) {
					char *thin_options[] = {"Make binary fat", "Back"};
					switch(select_option("", thin_options, ELEMENTS(thin_options))) {
						case 0: {
							uint32_t offset = init_arch_from_mh(magic, mach_headers, archs);

							ftruncate(fileno(f), file_size + offset);

							fmove(f, offset, 0, file_size);

							fzero(f, 0, offset);

							rewind(f);

							magic = FAT_CIGAM;
							fh.magic = magic;
							fh.nfat_arch = SWAP32(1, magic);
							fwrite(&fh, sizeof(fh), 1, f);

							archs[0].offset = SWAP32(offset, magic);
							archs[0].size = SWAP32((uint32_t)file_size, magic);
							fwrite(archs, sizeof(archs[0]), 1, f);

							fflush(f);

							is_fat = true;
							file_size += offset;
							break;
						}
						case 1:
							break;
					}
				} else {
					char *fat_options[] = {"Make binary thin", "Extract executable", "Remove executable", "Insert executable", "Back"};
					switch(select_option("", fat_options, ELEMENTS(fat_options))) {
						case 0: {
							if(nfat_arch == 0) {
								printf("Can't make binary thin with no executables.");
								break;
							}
							uint32_t thin_arch = 0;
							if(nfat_arch > 1) {
								thin_arch = select_executable(magic, archs, nfat_arch, "Binary contains multiple executables.\nPlease choose which one to use in the thin binary:", false);
								if(thin_arch == CANCEL) {
									break;
								}
							}

							uint32_t arch_size = SWAP32(archs[thin_arch].size, magic);
							fmove(f, 0, SWAP32(archs[thin_arch].offset, magic), arch_size);

							fflush(f);
							ftruncate(fileno(f), arch_size);

							archs[0] = archs[thin_arch];
							realloc(archs, sizeof(archs[0]));

							mach_headers[0] = mach_headers[thin_arch];
							realloc(mach_headers, sizeof(mach_headers[0]));

							file_size = arch_size;
							nfat_arch = 1;
							is_fat = false;

							break;
						}
						case 1: {
							uint32_t arch = select_executable(magic, archs, nfat_arch, "Choose which executable to extract:", false);
							if(arch == CANCEL) {
								break;
							}

							char output[] = "/tmp/macho-XXXXXX";
							int fd = mkstemp(output);
							fchmod(fd, S_IRWXU);

							FILE *t = fdopen(fd, "w");

							fcpy(t, 0, f, SWAP32(archs[arch].offset, magic), SWAP32(archs[arch].size, magic));

							fclose(t);

							printf("Extracted executable to: %s\n", output);

							break;
						}
						case 2: {
							uint32_t arch = select_executable(magic, archs, nfat_arch, "Choose which executable to remove:", false);
							if(arch == CANCEL) {
								break;
							}

							fzero(f, SWAP32(archs[arch].offset, magic), SWAP32(archs[arch].size, magic));

							uint32_t new_offset;
							if(arch == 0) {
								new_offset = sizeof(fh);
							} else {
								new_offset = SWAP32(archs[arch - 1].offset, magic) + SWAP32(archs[arch - 1].size, magic);
							}

							for(uint32_t i = arch + 1; i < nfat_arch; i++) {
								archs[i - 1] = archs[i];
								mach_headers[i - 1] = mach_headers[i];

								uint32_t offset = SWAP32(archs[i - 1].offset, magic);
								uint32_t size =  SWAP32(archs[i - 1].size, magic);

								new_offset = ROUND_UP(new_offset, 1 << SWAP32(archs[i - 1].align, magic));
								archs[i - 1].offset = SWAP32(new_offset, magic);

								fmove(f, new_offset, offset, size);
								fzero(f, new_offset + size, offset - new_offset);

								new_offset += size;
							}

							nfat_arch--;

							fh.nfat_arch = SWAP32(nfat_arch, magic);

							archs = realloc(archs, sizeof(*archs) * nfat_arch);
							mach_headers = realloc(mach_headers, sizeof(*mach_headers) * nfat_arch);

							rewind(f);
							fwrite(&fh, sizeof(fh), 1, f);
							fwrite(archs, sizeof(*archs), nfat_arch, f);
							fzero(f, ftello(f), sizeof(*archs));

							fflush(f);
							ftruncate(fileno(f), new_offset);

							file_size = new_offset;

							break;
						}
						case 3: {
							printf("Enter path to executable: ");

							char *line = NULL;
							size_t size;
							size_t linelen = getline(&line, &size, stdin);

							line[linelen - 1] = '\0';

							FILE *in = fopen(line, "r");
							if(!in) {
								printf("Couldn't open file!\n");
								break;
							}

							uint32_t in_magic;
							fread(&in_magic, sizeof(in_magic), 1, in);

							if(!IS_MAGIC(in_magic)) {
								printf("Unknown magic in input file: 0x%x\n", in_magic);
								break;
							}

							if(IS_FAT(in_magic)) {
								printf("Input file is a fat mach-o binary. Only thin mach-o binaries can be inserted!\n");
								break;
							}

							struct stat s;
							fstat(fileno(in), &s);
							uint32_t in_size = (uint32_t)s.st_size;

							struct mach_header in_mh;
							rewind(in);
							fread(&in_mh, sizeof(in_mh), 1, in);

							nfat_arch++;

							fh.nfat_arch = SWAP32(nfat_arch, magic);

							archs = realloc(archs, sizeof(*archs) * nfat_arch);
							mach_headers = realloc(mach_headers, sizeof(*mach_headers) * nfat_arch);

							mach_headers[nfat_arch - 1] = in_mh;
							size_t alignment = init_arch_from_mh(magic, &in_mh, archs + nfat_arch - 1);

							archs[nfat_arch - 1].size = SWAP32(in_size, magic);

							uint32_t offset = (uint32_t)ROUND_UP(file_size, alignment);
							archs[nfat_arch - 1].offset = SWAP32(offset, magic);

							uint32_t new_size = offset + in_size;
							ftruncate(fileno(f), new_size);
							fzero(f, file_size, offset - file_size);

							fcpy(f, offset, in, 0, in_size);

							file_size = new_size;

							rewind(f);
							fwrite(&fh, sizeof(fh), 1, f);
							fwrite(archs, sizeof(*archs), nfat_arch, f);
							fzero(f, ftello(f), sizeof(*archs));

							break;
						}
						case 4:
							break;
					}
				}
				break;
			case 1: {
				char *lc_options[] = {"Remove code signature", "Remove load command", "Insert load command", "Cancel"};
				size_t o = select_option("What would you like to do?", lc_options, ELEMENTS(lc_options));

				if(o == 3) {
					break;
				}

				uint32_t arch = 0;
				if(nfat_arch > 1) {
					arch = select_executable(magic, archs, nfat_arch, "Pick executable to edit:", o == 0);
					if(arch == CANCEL) {
						break;
					}
				}

				switch(o) {
					case 0: {
						uint32_t first = arch == ALL? 0: arch;
						uint32_t last = arch == ALL? nfat_arch - 1: arch;

						for(uint32_t i = first; i <= last; i++) {
							remove_codesig();
						}
					}
					case 1:
					case 2:
						break;
				}

				break;
			}
			case 2:
				puts("Bye!");
				goto out;
		}
	}
out:

	/*switch(magic) {
		case FAT_MAGIC:
		case FAT_CIGAM: {
			fseeko(f, 0, SEEK_SET);

			struct fat_header fh;
			fread(&fh, sizeof(fh), 1, f);

			uint32_t nfat_arch = SWAP32(fh.nfat_arch, magic);

			printf("Binary is a fat binary with %d archs.\n", nfat_arch);

			struct fat_arch archs[nfat_arch];
			fread(archs, sizeof(archs), 1, f);

			int fails = 0;

			uint32_t offset = 0;
			if(nfat_arch > 0) {
				offset = SWAP32(archs[0].offset, magic);
			}

			for(int i = 0; i < nfat_arch; i++) {
				off_t orig_offset = SWAP32(archs[i].offset, magic);
				off_t orig_slice_size = SWAP32(archs[i].size, magic);
				offset = ROUND_UP(offset, 1 << SWAP32(archs[i].align, magic));
				if(orig_offset != offset) {
					fmemmove(f, offset, orig_offset, orig_slice_size);
					fbzero(f, MIN(offset, orig_offset) + orig_slice_size, ABSDIFF(offset, orig_offset));

					archs[i].offset = SWAP32(offset, magic);
				}

				off_t slice_size = orig_slice_size;
				bool r = insert_dylib(f, offset, dylib_path, &slice_size);
				if(!r) {
					printf("Failed to add %s to arch #%d!\n", lc_name, i + 1);
					fails++;
				}

				if(slice_size < orig_slice_size && i < nfat_arch - 1) {
					fbzero(f, offset + slice_size, orig_slice_size - slice_size);
				}

				file_size = offset + slice_size;
				offset += slice_size;
				archs[i].size = SWAP32((uint32_t)slice_size, magic);
			}

			rewind(f);
			fwrite(&fh, sizeof(fh), 1, f);
			fwrite(archs, sizeof(archs), 1, f);

			// We need to flush before truncating
			fflush(f);
			ftruncate(fileno(f), file_size);

			if(fails == 0) {
				printf("Added %s to all archs in %s\n", lc_name, binary_path);
			} else if(fails == nfat_arch) {
				printf("Failed to add %s to any archs.\n", lc_name);
				success = false;
			} else {
				printf("Added %s to %d/%d archs in %s\n", lc_name, nfat_arch - fails, nfat_arch, binary_path);
			}

			break;
		}
		case MH_MAGIC_64:
		case MH_CIGAM_64:
		case MH_MAGIC:
		case MH_CIGAM:
			if(insert_dylib(f, 0, dylib_path, &file_size)) {
				ftruncate(fileno(f), file_size);
				printf("Added %s to %s\n", lc_name, binary_path);
			} else {
				printf("Failed to add %s!\n", lc_name);
				success = false;
			}
			break;
		default:
			printf("Unknown magic: 0x%x\n", magic);
			exit(1);
	}*/

	fclose(f);

	if(!success) {
		exit(1);
	}

    return 0;
}
