#pragma once

#include <string>

#include <mach-o/arch.h>

uint32_t cpu_pagesize(cpu_type_t cputype);
std::string cpu_name(cpu_type_t cpu_type, cpu_subtype_t cpu_subtype);
