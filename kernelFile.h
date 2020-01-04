#ifndef _KERNELFILE_H_
#define _KERNELFILE_H_

#include "particija-VS2017/part.h"
#include "fs.h"
#include <string>
using std::string;

class KernelFile {
public:
	//KernelFile(ClusterNo indexCluster, char writable, string name, string ext, BytesCnt size);
	//~KernelFile();

	//char write(BytesCnt, char* buffer);
	//BytesCnt read(BytesCnt, char* buffer);
	char seek(BytesCnt);
	BytesCnt filePos();
	char eof();
	BytesCnt getFileSize();
	//char truncate();

	string getName() const { return name; }
	string getExt() const { return ext; }

	//void updateSize();

	//const BytesCnt MAX_SIZE = Index::MAX_CLUSTERS * Cluster_Size;
private:
	string name;
	string ext;
	BytesCnt cursor = 0;
	BytesCnt size;

	char writable;

	//Index index;
	//ClusterCache clusterCache;

};

#endif