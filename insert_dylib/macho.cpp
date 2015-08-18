//
//  macho.cpp
//  insert_dylib
//
//  Created by Asger Hautop Drewsen on 18/08/15.
//  Copyright (c) 2015 Tyilo. All rights reserved.
//

#include "macho.h"

#include <fstream>

MachO::MachO(std::string &filename) {
	file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
	uint32_t magic;
	file >> magic;

	
}