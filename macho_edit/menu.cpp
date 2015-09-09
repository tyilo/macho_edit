#include <iostream>

#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

#include "macros.h"
#include "magicnames.h"
#include "menu.h"

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

template <typename T>
bool readline(T &line) {
	std::cin >> line;

	std::ios_base::iostate state = std::cin.rdstate();

	std::cin.clear();

	// Consume newline
	std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

	if(state == std::ios_base::goodbit) {
		return true;
	}

	if(state == std::ios_base::eofbit) {
		std::cout << "<EOF>\n";
		exit(0);
	}

	return false;
}

size_t select_option(const char *header, std::vector<std::string> options) {
	std::cout << header << "\n";

	for(size_t i = 0; i < options.size(); i++) {
		std::cout << (i + 1) << " " << options[i] << "\n";
	}

	std::cout << "\nSelect an option: ";

	while(true) {
		size_t o;

		if(readline(o)) {
			if(1 <= o && o <= options.size()) {
				return o - 1;
			}
		}

		std::cout << "Please enter a valid option: ";
	}
}

#define CANCEL (-1)
#define ALL (-2)

uint32_t select_arch(MachO &macho, const char *header, bool allow_all) {
	std::vector<std::string> arch_options;
	for(auto &arch : macho.archs) {
		arch_options.push_back(arch.description());
	}

	uint32_t cancel_index = macho.n_archs;
	if(allow_all) {
		arch_options.push_back("All archs");
		cancel_index++;
	}
	arch_options.push_back("Cancel");

	uint32_t o = (uint32_t)select_option(header, arch_options);

	if(o == cancel_index) {
		return CANCEL;
	}
	if(allow_all && o == cancel_index - 1) {
		return ALL;
	}

	return o;
}

uint32_t select_load_command(MachOArch &arch, const char *header) {
	std::vector<std::string> lc_options;
	for(auto &lc : arch.load_commands) {
		lc_options.push_back(lc.description());
	}

	lc_options.push_back("Cancel");

	uint32_t o = (uint32_t)select_option(header, lc_options);

	if(o == lc_options.size() - 1) {
		return CANCEL;
	}

	return o;
}

bool fat_config(MachO &macho) {
	if(!macho.is_fat) {
		static std::vector<std::string> thin_options = {
			"Make binary fat",
			"Back"
		};

		switch(select_option("", thin_options)) {
			case 0: {
				macho.make_fat();
				break;
			}
			case 1:
				return false;
		}
	} else {
		static std::vector<std::string> fat_options = {
			"Make binary thin",
			"Extract arch",
			"Remove arch",
			"Insert arch",
			"Back"
		};

		switch(select_option("", fat_options)) {
			case 0: {
				if(macho.n_archs == 0) {
					std::cout << "Can't make binary thin with no archs.\n";
					break;
				}

				uint32_t thin_arch = 0;
				if(macho.n_archs > 1) {
					thin_arch = select_arch(macho, "Binary contains multiple archs.\nPlease choose which one to use in the thin binary:", false);
					if(thin_arch == CANCEL) {
						break;
					}
				}

				macho.make_thin(thin_arch);

				break;
			}
			case 1: {
				if(macho.n_archs == 0) {
					std::cout << "The fat binary contains no archs to extract.\n";
					break;
				}

				uint32_t arch = select_arch(macho, "Choose which arch to extract:", false);
				if(arch == CANCEL) {
					break;
				}

				std::cout << "Path to save arch to: ";
				std::string path;
				readline(path);

				if(macho.save_arch_to_file(arch, path.c_str())) {
					std::cout << "Arch successfully extracted.\n";
				} else {
					std::cout << "Failed to extract arch!\n";
				}

				break;
			}
			case 2: {
				if(macho.n_archs == 0) {
					std::cout << "The fat binary contains no archs to remove.\n";
					break;
				}

				uint32_t arch = select_arch(macho, "Choose which arch to remove:", false);
				if(arch == CANCEL) {
					break;
				}

				macho.remove_arch(arch);

				break;
			}
			case 3: {
				std::cout << "Enter path to binary: ";

				std::string path;

				readline(path);

				MachO macho_in;
				try {
					macho_in = MachO(path.c_str());
				} catch(const std::string &err) {
					std::cout << err << "\n";
					break;
				} catch(const char *err) {
					std::cout << err << "\n";
					break;
				}

				if(macho_in.n_archs == 0) {
					std::cout << "The binary has no archs in it to insert!\n";
					break;
				}

				uint32_t insert_arch = 0;
				if(macho_in.n_archs > 1) {
					insert_arch = select_arch(macho_in, "Binary contains multiple archs.\nPlease choose which one to insert:", true);
					if(insert_arch == CANCEL) {
						break;
					}
				}

				uint32_t first = insert_arch == ALL? 0: insert_arch;
				uint32_t last = insert_arch == ALL? macho_in.n_archs - 1: insert_arch;

				for(uint32_t i = first; i <= last; i++) {
					macho.insert_arch_from_macho(macho_in, i);
				}

				break;
			}
			case 4:
				return false;
		}
	}

	return true;
}

bool ask_for_path(const char *prompt, std::string &path) {
	std::cout << prompt << " ";
	readline(path);

	struct stat s;
	if(path[0] != '@' && stat(path.c_str(), &s) != 0) {
		if(!ask("The path doesn't exist. Continue?")) {
			return false;
		}
	}

	return true;
}

#define PATH_PADDING 8

load_command *get_path_cmd(const char *prompt, size_t header_size, uint32_t *cmdsize) {
	std::string path;
	if(!ask_for_path(prompt, path)) {
		return NULL;
	}

	uint32_t path_size = (uint32_t)ROUND_UP(path.length() + 1, PATH_PADDING);
	*cmdsize = (uint32_t)header_size + path_size;

	load_command *lc = (load_command *)malloc(*cmdsize);
	memcpy(((uint8_t *)lc) + header_size, path.c_str(), path.length());
	return lc;
}

void lc_insert(MachO &macho, uint32_t arch) {
	static uint32_t insertable_cmds[] = {
		LC_LOAD_DYLIB,
		LC_LOAD_WEAK_DYLIB,
		LC_RPATH
	};

	std::vector<std::string> options;

	for(uint32_t i = 0; i < ELEMENTS(insertable_cmds); i++) {
		options.push_back(cmd_name(insertable_cmds[i]));
	}

	options.push_back("Cancel");

	size_t o = select_option("Select the cmd you want to insert:", options);
	if(o == ELEMENTS(insertable_cmds)) {
		return;
	}

	uint32_t cmd = insertable_cmds[o];

	uint32_t magic = macho.archs[arch].mach_header.magic;

	load_command *lc = NULL;

	switch(cmd) {
		case LC_LOAD_DYLIB:
		case LC_LOAD_WEAK_DYLIB: {
			uint32_t cmdsize;
			if((lc = get_path_cmd("Dylib path:", sizeof(dylib_command), &cmdsize))) {
				auto *c = (dylib_command *)lc;
				c->cmd = SWAP32(cmd, magic);
				c->cmdsize = SWAP32(cmdsize, magic);
				c->dylib = {
					.name.offset = SWAP32(sizeof(dylib_command), magic),
					.timestamp = 0,
					.current_version = 0,
					.compatibility_version = 0
				};
			}

			break;
		}
		case LC_RPATH: {
			uint32_t cmdsize;
			if((lc = get_path_cmd("Runpath:", sizeof(dylib_command), &cmdsize))) {
				auto *c = (rpath_command *)lc;
				c->cmd = SWAP32(cmd, magic);
				c->cmdsize = SWAP32(cmdsize, magic);
				c->path.offset = SWAP32(sizeof(dylib_command), magic);
			}

			break;
		}
	}

	if(lc) {
		macho.insert_load_command(arch, lc);
		free(lc);
	}
}

bool lc_config(MachO &macho) {
	static std::vector<std::string> lc_options = {
		"List load commands",
		"Remove load command",
		"Insert load command",
		"Move load command",
		"Remove code signature",
		"Cancel"
	};

	size_t o = select_option("", lc_options);

	if(o == 5) {
		return false;
	}

	uint32_t arch = 0;
	if(macho.n_archs > 1) {
		arch = select_arch(macho, "Pick arch to edit:", o == 0 || o == 4);
		if(arch == CANCEL) {
			return false;
		}
	}

	uint32_t first = arch == ALL? 0: arch;
	uint32_t last = arch == ALL? macho.n_archs - 1: arch;

	switch(o) {
		case 0:
			for(uint32_t i = first; i <= last; i++) {
				MachOArch &arch = macho.archs[i];
				std::cout << "\n" << arch.description() << ":\n";

				arch.print_load_commands();
			}
			break;
		case 1: {
			uint32_t lc = select_load_command(macho.archs[arch], "Select a load command to remove:\n");
			macho.remove_load_command(arch, lc);
			break;
		}
		case 2:
			lc_insert(macho, arch);

			break;
		case 3: {
			uint32_t lc1 = select_load_command(macho.archs[arch], "Select a load command to move:\n");
			uint32_t lc2 = select_load_command(macho.archs[arch], "Select load command to swap with:\n");

			macho.move_load_command(arch, lc1, lc2);

			break;
		}
		case 4: {
			for(uint32_t i = first; i <= last; i++) {
				if(!macho.archs[i].has_codesignature()) {
					std::cout << "Arch " << i << " doesn't have a codesignature.\n";
					continue;
				}
				macho.remove_codesignature(i);
				std::cout << "Removed codesignature from arch " << i << ".\n";
			}

			break;
		}
	}

	return true;
}

bool main_menu(MachO &macho) {
	static std::vector<std::string> main_options = {
		"Fat mach-o configuration",
		"Load command edit",
		"Exit"
	};

	switch(select_option("", main_options)) {
		case 0:
			while(fat_config(macho)) {
			}
			break;
		case 1:
			while(lc_config(macho)) {
			}
			break;
		case 2:
			return false;
	}
	
	return true;
}
