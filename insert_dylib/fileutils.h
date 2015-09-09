#pragma once

#include <stdio.h>

void fzero(FILE *f, off_t offset, size_t len);
void fmove(FILE *f, off_t dst, off_t src, size_t len);
void fcpy(FILE *fdst, off_t dst, FILE *fsrc, off_t src, size_t len);
size_t fpeek(void *ptr, size_t size, size_t nitems, FILE *stream);
