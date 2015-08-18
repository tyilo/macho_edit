#include "menu.h"

#include <stdio.h>
#include <stdarg.h>

#include <iostream>

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

size_t select_option(const char *header, std::vector<std::string> options) {
	puts(header);

	for(size_t i = 0; i < options.size(); i++) {
		std::cout << (i + 1) << " " << options[i] << "\n";
	}

	printf("Select an option: ");

	while(true) {
		char *line = NULL;
		size_t size;
		getline(&line, &size, stdin);

		size_t o = strtoull(line, NULL, 10);

		if(1 <= o && o <= options.size()) {
			return o - 1;
		}

		printf("Please enter a valid option: ");
	}
}

#define CANCEL (-1)
#define ALL (-2)

uint32_t select_arch(MachO &macho, const char *header, bool allow_all) {
	std::vector<std::string> arch_options;
	for(auto &arch : macho.archs) {
		arch_options.push_back(macho.arch_description(arch));
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

void fat_config(MachO &macho) {
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
				break;
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
					printf("Can't make binary thin with no archs.");
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
				uint32_t arch = select_arch(macho, "Choose which arch to extract:", false);
				if(arch == CANCEL) {
					break;
				}

				std::string path = macho.extract_arch(arch);

				std::cout << "Arch extracted to: " << path << "\n";

				break;
			}
			case 2: {
				uint32_t arch = select_arch(macho, "Choose which arch to remove:", false);
				if(arch == CANCEL) {
					break;
				}

				macho.remove_arch(arch);

				break;
			}
			case 3: {
				printf("Enter path to binary: ");

				char *line = NULL;
				size_t size;
				size_t linelen = getline(&line, &size, stdin);

				line[linelen - 1] = '\0';

				MachO macho_in;
				try {
					macho_in = MachO(line);
				} catch(std::string err) {
					std::cout << err << "\n";
					break;
				}

				if(macho_in.n_archs == 0) {
					std::cout << "The binary has no archs in it to insert!\n";
					break;
				}

				uint32_t insert_arch = 0;
				if(macho.n_archs > 1) {
					// XXX: Support ALL
					insert_arch = select_arch(macho, "Binary contains multiple archs.\nPlease choose which one to insert:", true);
					if(insert_arch == CANCEL) {
						break;
					}
				}

				macho.insert_arch(macho_in, insert_arch);

				break;
			}
			case 4:
				break;
		}
	}
}

void lc_config(MachO &macho) {
	static std::vector<std::string> lc_options = {
		"Remove code signature",
		"Remove load command",
		"Insert load command",
		"Cancel"
	};

	size_t o = select_option("What would you like to do?", lc_options);

	if(o == 3) {
		return;
	}

	uint32_t arch = 0;
	if(macho.n_archs > 1) {
		arch = select_arch(macho, "Pick arch to edit:", o == 0);
		if(arch == CANCEL) {
			return;
		}
	}

	switch(o) {
		case 0: {
			uint32_t first = arch == ALL? 0: arch;
			uint32_t last = arch == ALL? macho.n_archs - 1: arch;

			for(uint32_t i = first; i <= last; i++) {
				//remove_codesig();
			}
		}
		case 1:
		case 2:
			break;
	}
}

bool main_menu(MachO &macho) {
	static std::vector<std::string> main_options = {
		"Fat mach-o configuration",
		"Load command edit",
		"Exit"
	};

	switch(select_option("", main_options)) {
		case 0:
			fat_config(macho);
			break;
		case 1:
			lc_config(macho);
			break;
		case 2:
			return false;
	}
	
	return true;
}