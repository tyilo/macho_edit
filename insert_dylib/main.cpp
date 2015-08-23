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

#include "menu.h"

#include <iostream>

__attribute__((noreturn)) void usage(void) {
	puts("Usage: insert_dylib binary_path");

	exit(1);
}

/*
void *read_load_command(FILE *f, uint32_t cmdsize) {
	void *lc = malloc(cmdsize);

	fpeek(lc, cmdsize, 1, f);

	return lc;
}
 */

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

/*void remove_cmd(FILE *f, uint32_t magic, ) {

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
}*/

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

int main(int argc, const char *argv[]) {
	if(argc != 2) {
		usage();
	}

	const char *binary_path = argv[1];

	MachO macho = MachO(binary_path);

	macho.print_description();

	while(main_menu(macho)) {
	}

    return 0;
}
