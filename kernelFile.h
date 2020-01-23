#ifndef _KERNELFILE_H_
#define _KERNELFILE_H_

#include <string>
using std::string;
#include "particija-VS2017/part.h"

const unsigned long ClusterSize = 2048;
typedef unsigned long ClusterNo;
typedef unsigned long BytesCnt;

#define MAX_FILE_SIZE (ClusterSize / sizeof(ClusterNo))*(ClusterSize / sizeof(ClusterNo))*ClusterSize

class KernelFS;

class KernelFile {
public:
	KernelFile(string name, char mode, BytesCnt size, ClusterNo cluster, BytesCnt cursor) {
		this->name = name;
		this->mode = mode;
		this->size = size;
		this->firstIndex = cluster;
		this->cursor = cursor;
	}
	~KernelFile();

	//char write(BytesCnt, char* buffer);
	BytesCnt read(BytesCnt, char* buffer);
	char seek(BytesCnt);
	BytesCnt filePos();
	char eof();
	BytesCnt getFileSize();
	//char truncate();


private:

	string name;
	BytesCnt cursor = 0;
	BytesCnt size;
	char mode;
	char writable;
	ClusterNo firstIndex;

	unsigned long entryNoFirstIndex(unsigned long position) {
		return position / ClusterSize / (ClusterSize / sizeof(ClusterNo));
	}
	unsigned long entryNoSecondIndex(unsigned long position) {
		return position / ClusterSize % (ClusterSize / sizeof(ClusterNo));
	}

};

#endif