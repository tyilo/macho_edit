#include <sstream>

#include "cpuinfo.h"

uint32_t cpu_pagesize(cpu_type_t cputype) {
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

std::string cpu_name(cpu_type_t cpu_type, cpu_subtype_t cpu_subtype) {
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

	std::ostringstream o;
	o << "unknown (0x" << std::hex << cpu_type << ", 0x" << cpu_subtype << ")";
	return o.str();
}
