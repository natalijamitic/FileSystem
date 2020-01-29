#ifndef _FCB_H_
#define _FCB_H_

#include <windows.h>
#include <string>
using std::string;

typedef unsigned long ClusterNo;
typedef unsigned long BytesCnt;

struct FileIndexes {
	ClusterNo fileFirstIndex, rootSecondIndex, rootDataIndex, offsetInRootDataIndex;
	FileIndexes(ClusterNo f, ClusterNo s, ClusterNo d, ClusterNo o = -1) {
		fileFirstIndex = f;
		rootSecondIndex = s;
		rootDataIndex = d;
		offsetInRootDataIndex = o;
	}
};

struct FCB {
	string fname;
	char mode;
	BytesCnt fileSize;

	unsigned int numOfRefs;

	ClusterNo fileFirstIndex;
	ClusterNo rootDataIndex; 
	BytesCnt offsetInRootDataIndex; 

	HANDLE semFile = CreateSemaphore(NULL, 0, 1, NULL);

	FCB(ClusterNo clFileFirst, ClusterNo clRoot, BytesCnt cnt, char m, string fn, BytesCnt size = 0) {
		fname = fn;
		mode = m;
		fileSize = size;

		numOfRefs = 1;

		fileFirstIndex = clFileFirst;
		rootDataIndex = clRoot;
		offsetInRootDataIndex = cnt;
	}
};

#endif