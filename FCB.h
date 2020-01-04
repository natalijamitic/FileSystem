#ifndef _FCB_H_
#define _FCB_H_

#include "kernelFS.h"

class FCB {
public:
	ClusterNo fileInfoIndex;
	BytesCnt offset;
	char mode;
	string fname;
	unsigned int numOfRefs = 0;

public:
	FCB(ClusterNo n, BytesCnt cnt, char m, string fn) {
		mode = m;
		fileInfoIndex = n;
		offset = cnt;
		fname = fn;
	}

	void addRef() {
		numOfRefs++;
	}

	void subRef() {
		numOfRefs--;
	}

	unsigned int getNumOfRefs() {
		return numOfRefs;
	}

	char getMode() {
		return mode;
	}

	ClusterNo getFileInfoIndex() {
		return fileInfoIndex;
	}

	string getFileName() {
		return fname;
	}
};

#endif