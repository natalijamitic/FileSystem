#ifndef _KERNELFS_H_
#define _KERNELFS_H_

#include <iostream>
#include <Windows.h>
#include <string>
using std::string;
#include "particija-VS2017/part.h"
#include "fs.h"
#include "criticalSectionInit.h"

#define signal(x) ReleaseSemaphore(x,1,NULL)
#define wait(x) WaitForSingleObject(x,INFINITE)

//const unsigned short ENTRIES_PER_CLUSTER = ClusterSize / 4;
class Partition;
class File;
class KernelFile;

const unsigned short BITS_IN_BYTE = 8;

class KernelFS {
public:
	static char mount(Partition* partition); 
	static char unmount(); 
	static char format();

	static FileCnt readRootDir();

	static char doesExist(char* fname); 
	static File* open(char* fname, char mode);
	static char deleteFile(char* fname);

public:

	static CRITICAL_SECTION mutex;
	static HANDLE semFilesClosed;
	static HANDLE semUnmount;
	static CriticalSectionInit init;

	friend class CriticalSectionInit;

	static Partition* myPartition;
	static bool filesOpened;
	static bool beingFormatted;
	static ClusterNo clusterCount;

	static ClusterNo getFileFirstIndex(char* text);
	static BytesCnt getFileSize(char* text);
	static string getFileName(string text);
	static string getFileExt(string text);
	static string getFileNameFromPath(string text);
	static string getFileExtFromPath(string text);

	static char doesExistNotSynch(char* fname);

};


#endif