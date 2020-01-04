#include "kernelFile.h"
#include "kernelFS.h"

char KernelFile::seek(BytesCnt cnt) {
	if (cnt > size)
		return 0;
	cursor = cnt;
	return 1;
}

BytesCnt KernelFile::filePos() {
	return cursor;
}

char KernelFile::eof() {
	return cursor == size;
}

BytesCnt KernelFile::getFileSize() {
	return size;
}