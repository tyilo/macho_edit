#include <iostream>

#include "menu.h"

__attribute__((noreturn)) void usage(void) {
	std::cout << "Usage: macho_edit binary_path\n";

	exit(1);
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
