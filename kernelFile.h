#ifndef _KERNELFILE_H_
#define _KERNELFILE_H_


#include <string>
using std::string;
#include "particija-VS2017/part.h"
#include "fs.h"

class FCB;

class KernelFile {
public:
	//KernelFile(ClusterNo indexCluster, char writable, string name, string ext, BytesCnt size);
	KernelFile(FCB* f, BytesCnt c) {
		fcb = f;
		cursor = c;
	}
	//~KernelFile();

	//char write(BytesCnt, char* buffer);
	//BytesCnt read(BytesCnt, char* buffer);
	char seek(BytesCnt);
	BytesCnt filePos();
	char eof();
	BytesCnt getFileSize();
	//char truncate();

	//void updateSize();

	//const BytesCnt MAX_SIZE = Index::MAX_CLUSTERS * Cluster_Size;
private:
	BytesCnt cursor = 0;
	BytesCnt fileSize;
	char writable;
	FCB* fcb;

};

#endif