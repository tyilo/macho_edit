#include <iostream>

#include "menu.h"

__attribute__((noreturn)) void usage(void) {
	std::cout << "Usage: macho_edit binary_path\n";

	exit(1);
}

/*
bool check_load_commands(FILE *f, mach_header *mh, size_t header_offset, size_t commands_offset, const char *dylib_path, off_t *slice_size) {
	fseeko(f, commands_offset, SEEK_SET);

	uint32_t ncmds = SWAP32(mh->ncmds, mh->magic);

	off_t linkedit_32_pos = -1;
	off_t linkedit_64_pos = -1;
	segment_command linkedit_32;
	segment_command_64 linkedit_64;

	off_t symtab_pos = -1;
	uint32_t symtab_size = 0;

	for(int i = 0; i < ncmds; i++) {
		load_command lc;
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

					linkedit_data_command *cmd = read_load_command(f, cmdsize);

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
									symtab_command *symtab = read_load_command(f, symtab_size);

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
				dylib_command *dylib_command = read_load_command(f, cmdsize);

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
					segment_command *cmd = read_load_command(f, cmdsize);
					if(strcmp(cmd->segname, "__LINKEDIT") == 0) {
						linkedit_32_pos = ftello(f);
						linkedit_32 = *cmd;
					}
					free(cmd);
				} else {
					segment_command_64 *cmd = read_load_command(f, cmdsize);
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

	mach_header mh;
	fread(&mh, sizeof(mach_header), 1, f);

	if(mh.magic != MH_MAGIC_64 && mh.magic != MH_CIGAM_64 && mh.magic != MH_MAGIC && mh.magic != MH_CIGAM) {
		printf("Unknown magic: 0x%x\n", mh.magic);
		return false;
	}

	size_t commands_offset = header_offset + (IS_64_BIT(mh.magic)? sizeof(mach_header_64): sizeof(mach_header));

	bool cont = check_load_commands(f, &mh, header_offset, commands_offset, dylib_path, slice_size);

	if(!cont) {
		return true;
	}

	// Even though a padding of 4 works for x86_64, codesign doesn't like it
	size_t path_padding = 8;

	size_t dylib_path_len = strlen(dylib_path);
	size_t dylib_path_size = (dylib_path_len & ~(path_padding - 1)) + path_padding;
	uint32_t cmdsize = (uint32_t)(sizeof(dylib_command) + dylib_path_size);

	dylib_command dylib_command = {
		.cmd = SWAP32(weak_flag? LC_LOAD_WEAK_DYLIB: LC_LOAD_DYLIB, mh.magic),
		.cmdsize = SWAP32(cmdsize, mh.magic),
		.dylib = {
			.name = SWAP32(sizeof(dylib_command), mh.magic),
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

void remove_codesig(FILE *f, uint32_t magic, fat_arch *arch, mach_header *mh) {
	fseeko(f, SWAP32(arch->offset, magic), SEEK_SET);

	uint32_t size = SWAP32(arch->size, magic);

	uint32_t ncmds = SWAP32(mh->ncmds, mh->magic);

	off_t linkedit_32_pos = -1;
	off_t linkedit_64_pos = -1;
	segment_command linkedit_32;
	segment_command_64 linkedit_64;

	off_t symtab_pos = -1;
	uint32_t symtab_size = 0;

	for(int i = 0; i < ncmds; i++) {
		load_command lc;
		fpeek(&lc, sizeof(lc), 1, f);

		uint32_t cmdsize = SWAP32(lc.cmdsize, mh->magic);
		uint32_t cmd = SWAP32(lc.cmd, mh->magic);

		switch(cmd) {
			case LC_CODE_SIGNATURE:
				if(i == ncmds - 1) {
					linkedit_data_command *cmd = read_load_command(f, cmdsize);

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
									symtab_command *symtab = read_load_command(f, symtab_size);

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
					segment_command *cmd = read_load_command(f, cmdsize);
					if(strcmp(cmd->segname, "__LINKEDIT") == 0) {
						linkedit_32_pos = ftello(f);
						linkedit_32 = *cmd;
					}
					free(cmd);
				} else {
					segment_command_64 *cmd = read_load_command(f, cmdsize);
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
