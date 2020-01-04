#include "kernelFile.h"
#include "kernelFS.h"
#include "FCB.h"

char KernelFile::seek(BytesCnt cnt) {
	if (cnt > fileSize)
		return 0;
	cursor = cnt;
	return 1;
}

BytesCnt KernelFile::filePos() {
	return cursor;
}

char KernelFile::eof() {
	return cursor == fileSize;
}

BytesCnt KernelFile::getFileSize() {
	return fileSize;
}