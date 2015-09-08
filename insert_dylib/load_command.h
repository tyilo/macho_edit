#pragma once

#include <string>

#include <stdio.h>
#include <mach-o/loader.h>

class LoadCommand {
public:
// Fields
	uint32_t magic;
	load_command *raw_lc = NULL;
	off_t file_offset;

	uint32_t cmd;
	uint32_t cmdsize;

// Methods
	LoadCommand();
	LoadCommand(uint32_t magic, FILE *f);
	~LoadCommand();

	LoadCommand(const LoadCommand &other);

	char *get_lc_str(union lc_str lc_str) const;
	std::string description() const;
};