#ifndef _KERNELFILE_H_
#define _KERNELFILE_H_

#include <string>
using std::string;
#include "kernelFS.h"

#define MAX_FILE_SIZE (ClusterSize / sizeof(ClusterNo))*(ClusterSize / sizeof(ClusterNo))*ClusterSize

class KernelFile {
public:
	KernelFile(string name, char mode, BytesCnt size, ClusterNo cluster, BytesCnt cursor);
	~KernelFile();

	char write(BytesCnt, char* buffer);
	BytesCnt read(BytesCnt, char* buffer);
	char seek(BytesCnt);
	BytesCnt filePos();
	char eof();
	BytesCnt getFileSize();
	char truncate();


private:
 
	string name;
	string searchName;
	BytesCnt cursor = 0;
	BytesCnt size;
	char mode;
	ClusterNo firstIndex;

	unsigned long entryNoFirstIndex(unsigned long position);
	unsigned long entryNoSecondIndex(unsigned long position);

	bool enoughFreeClusters(unsigned long count);

};

#endif