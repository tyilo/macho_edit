#include <iomanip>
#include <sstream>

#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <stdlib.h>

#include "fileutils.h"
#include "load_command.h"
#include "macros.h"
#include "magicnames.h"

LoadCommand::LoadCommand() {
}

LoadCommand::LoadCommand(uint32_t magic, FILE *f) {
	this->magic = magic;
	file_offset = ftello(f);

	load_command lc_header;
	PEEK(lc_header, f);

	cmd = SWAP32(lc_header.cmd, magic);
	cmdsize = SWAP32(lc_header.cmdsize, magic);

	raw_lc = (load_command *)malloc(cmdsize);
	fread((void *)raw_lc, cmdsize, 1, f);
}

LoadCommand::LoadCommand(uint32_t magic, off_t file_offset, load_command *raw_lc) {
	this->magic = magic;
	this->file_offset = file_offset;

	cmd = SWAP32(raw_lc->cmd, magic);
	cmdsize = SWAP32(raw_lc->cmdsize, magic);

	this->raw_lc = (load_command *)malloc(cmdsize);
	memcpy(this->raw_lc, raw_lc, cmdsize);
}

LoadCommand::~LoadCommand() {
	free(raw_lc);
}

LoadCommand::LoadCommand(const LoadCommand &other) {
	memcpy(this, &other, sizeof(*this));

	raw_lc = (load_command *)malloc(cmdsize);
	memcpy(raw_lc, other.raw_lc, cmdsize);
}

std::string LoadCommand::get_lc_str(lc_str lc_str) const {
	char *ptr = (char *)raw_lc;

	uint32_t offset = SWAP32(lc_str.offset, magic);
	return std::string(&ptr[offset], cmdsize - offset);
}

std::string LoadCommand::description() const {
	std::string name = cmd_name(cmd);

	std::ostringstream o;

	o << name;

	switch(cmd) {
		case LC_SEGMENT: {
			auto *c = (segment_command *)raw_lc;

			o << ": " << std::string(c->segname, ELEMENTS(c->segname));

			break;
		}
		case LC_SEGMENT_64: {
			auto *c = (segment_command_64 *)raw_lc;

			o << ": " << std::string(c->segname, ELEMENTS(c->segname));

			break;
		}
		case LC_LOAD_DYLINKER: {
			auto *c = (dylinker_command *)raw_lc;

			o << ": " << get_lc_str(c->name);

			break;
		}
		case LC_UUID: {
			auto *c = (uuid_command *)raw_lc;

			size_t form[] = {4, 2, 2, 2, 6};

			o << ": " << std::hex << std::setfill('0') << std::setw(2);

			size_t offset = 0;
			for(size_t i = 0; i < ELEMENTS(form); i++) {
				if(i != 0) {
					o << "-";
				}
				for(size_t j = 0; j < form[i]; j++) {
					o << (unsigned int)c->uuid[offset + j];
				}
				offset += i;
			}

			break;
		}
		case LC_VERSION_MIN_MACOSX:
		case LC_VERSION_MIN_IPHONEOS: {
			auto *c = (version_min_command *)raw_lc;

			size_t form[] = {2, 1, 1};

			o << ": ";

			size_t offset = 0;
			for(size_t i = 0; i < ELEMENTS(form); i++) {
				if(i != 0) {
					o << ".";
				}

				uint16_t version = 0;
				for(size_t j = 0; j < form[i]; j++) {
					version *= form[i];
					version += ((uint8_t *)&c->version)[offset + j];
				}

				o << version;

				offset += i;
			}

			break;
		}
		case LC_MAIN: {
			auto *c = (entry_point_command *)raw_lc;

			o << ": ";

			o << std::hex << "0x" << c->entryoff;

			break;
		}
		case LC_LOAD_DYLIB: {
			auto *c = (dylib_command *)raw_lc;

			o << ": " << get_lc_str(c->dylib.name);

			break;
		}
		case LC_RPATH: {
			auto *c = (rpath_command *)raw_lc;

			o << ": " << get_lc_str(c->path);

			break;
		}
	}
	
	return o.str();
}
