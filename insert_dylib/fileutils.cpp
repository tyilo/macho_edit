#include "fileutils.h"
#include "macros.h"

#define BUFSIZE 512

void fzero(FILE *f, off_t offset, size_t len) {
	static unsigned char zeros[BUFSIZE] = {0};
	fseeko(f, offset, SEEK_SET);
	while(len != 0) {
		size_t size = MIN(len, sizeof(zeros));
		fwrite(zeros, size, 1, f);
		len -= size;
	}
}

void fmove(FILE *f, off_t dst, off_t src, size_t len) {
	if(dst == src) {
		return;
	}

	unsigned char buf[BUFSIZE];
	if(dst < src) {
		while(len != 0) {
			size_t size = MIN(len, sizeof(buf));
			fseeko(f, src, SEEK_SET);
			fread(&buf, size, 1, f);
			fseeko(f, dst, SEEK_SET);
			fwrite(buf, size, 1, f);

			len -= size;
			src += size;
			dst += size;
		}
	} else {
		while(len != 0) {
			size_t size = MIN(len, sizeof(buf));
			fseeko(f, src + len - size, SEEK_SET);
			fread(&buf, size, 1, f);
			fseeko(f, dst + len - size, SEEK_SET);
			fwrite(buf, size, 1, f);

			len -= size;
		}
	}
}

void fcpy(FILE *fdst, off_t dst, FILE *fsrc, off_t src, size_t len) {
	unsigned char buf[BUFSIZE];

	fseeko(fdst, dst, SEEK_SET);
	fseeko(fsrc, src, SEEK_SET);

	while(len != 0) {
		size_t size = MIN(len, sizeof(buf));
		fread(&buf, size, 1, fsrc);
		fwrite(buf, size, 1, fdst);

		len -= size;
	}
}

size_t fpeek(void *ptr, size_t size, size_t nitems, FILE *stream) {
	size_t result = fread(ptr, size, nitems, stream);
	fseeko(stream, result * size, SEEK_SET);
	return result;
}