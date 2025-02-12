#ifndef _KERNELFS_H_
#define _KERNELFS_H_

#include <iostream>
#include <Windows.h>
#include <string>
using std::string;
#include <vector>
using std::vector;
#include <map>
using std::map;

#include "particija-VS2017/part.h"
#include "fs.h"
#include "criticalSectionInit.h"
#include "FCB.h"

#define signal(x) ReleaseSemaphore(x,1,NULL)
#define wait(x) WaitForSingleObject(x,INFINITE)

//const unsigned short ENTRIES_PER_CLUSTER = ClusterSize / 4;
class Partition;
class File;
class KernelFile;

const unsigned short BITS_IN_BYTE = 8;
const char emptyCluster[ClusterSize] = { 0 };


class KernelFS {
public:
	static char mount(Partition* partition);
	static char unmount();
	static char format();

	static FileCnt readRootDir();

	static char doesExist(char* fname);
	static File* open(char* fname, char mode);
	static char deleteFile(char* fname);

private:
	friend class KernelFile;
	friend class CriticalSectionInit;

	static CRITICAL_SECTION mutex;
	static CRITICAL_SECTION mutexAllocator;
	static HANDLE semFilesClosed;
	static HANDLE semUnmount;
	static HANDLE semFormat;
	static CriticalSectionInit init;

	static Partition* myPartition;
	static bool beingFormatted;
	static ClusterNo clusterCount;

	static map<string, FCB>* openFileTable; //string=NAME+EXT

	static int getCountOfOpenFiles();
	static bool isFileOpen(char* fname);

	static ClusterNo getFileFirstIndex(char* text);
	static void setFileFirstIndex(char* text, ClusterNo clusterNumber);
	static BytesCnt getFileSize(char* text);
	static void setFileSize(char* text, BytesCnt fileSize);
	static string getFileName(string text);
	static void setFileName(string& text, string fname);
	static string getFileExt(string text);
	static void setFileExt(string& text, string fext);
	static string getFileNameFromPath(string text);
	static string getFileExtFromPath(string text);
	static string setFileData(ClusterNo clusterNumber, BytesCnt fileSize, const char* fname, const char* fext);

	
	static FileIndexes getFileIndexes(char* fname);

	static char deleteFileNotSynch(char* fname);
	static void deleteFileIndexes(FileIndexes fileIndexes);
	//static void deleteRootIndexes(char* fname, FileIndexes fileIndexes);
	static void freeClusterInBitVector(vector<ClusterNo>clusterNumbers);

	static char doesExistNotSynch(char* fname);

	static std::pair<ClusterNo, BytesCnt> findFreeSpotInCluster();
	static ClusterNo findFreeCluster();
	static std::pair<ClusterNo, BytesCnt> writeFileInRoot(char* fname);
	static File* createFile(char* fname, char mode, std::pair<ClusterNo, BytesCnt> pair);
	
	static File* openForWrite(char* fname, char mode, bool fileIsOpen, bool fileExists);
	static File* openForRead(char* fname, char mode, bool fileIsOpen, bool fileExists);
	static File* openForAppend(char* fname, char mode, bool fileIsOpen, bool fileExists);
};


#endif