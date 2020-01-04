//DODATI ZA SVAKU FJU PROVERU DA L JE PARTICIJA NAKACENA I DA LI SE TRENUTNO FORMATIRA!

#include "kernelFS.h"
#include "particija-VS2017/part.h"
#include "kernelFile.h"
#include "file.h"


CRITICAL_SECTION KernelFS::mutex;
HANDLE KernelFS::semFilesClosed = CreateSemaphore(NULL, 1, 32, NULL);
HANDLE KernelFS::semUnmount = CreateSemaphore(NULL, 1, 32, NULL);
HANDLE KernelFS::semFormat = CreateSemaphore(NULL, 1, 32, NULL);

Partition* KernelFS::myPartition = nullptr;
bool KernelFS::filesOpened = false;
bool KernelFS::beingFormatted = false;

ClusterNo KernelFS::clusterCount = 0;

CriticalSectionInit KernelFS::init;


char KernelFS::mount(Partition* partition) {
	EnterCriticalSection(&mutex);

	if (partition == nullptr) {
		LeaveCriticalSection(&mutex);
		return 0;
	}

	if (myPartition != nullptr) {
		LeaveCriticalSection(&mutex);
		wait(semUnmount);
		EnterCriticalSection(&mutex);
	}

	myPartition = partition;
	clusterCount = partition->getNumOfClusters();
	
	LeaveCriticalSection(&mutex);
	return 1;
}

char KernelFS::unmount() {
	EnterCriticalSection(&mutex);

	if (myPartition == nullptr) {
		LeaveCriticalSection(&mutex);
		return 0;
	}

	if (filesOpened) {
		LeaveCriticalSection(&mutex);
		wait(semFilesClosed);
		EnterCriticalSection(&mutex);
	}

	myPartition = nullptr;

	signal(semUnmount);
	LeaveCriticalSection(&mutex);
	return 1;
}

char KernelFS::format() { 
	EnterCriticalSection(&mutex);

	if (myPartition == nullptr) {
		LeaveCriticalSection(&mutex);
		return 0;
	}

	beingFormatted = true;

	if (filesOpened) {
		LeaveCriticalSection(&mutex);
		wait(semFilesClosed);
		EnterCriticalSection(&mutex);
	}
	
	//bitVector init
	vector<char> bitVector;
	unsigned long totalBytes = ceil(clusterCount / BITS_IN_BYTE);
	bitVector.push_back(0x3F); //3F 0011 1111
	for (unsigned long i = 1; i < totalBytes; i++)
		bitVector.push_back(-1); 
	myPartition->writeCluster(0, bitVector.data()); 

	//rootDir init
	vector<char> rootDir;
	for (unsigned long i = 0; i < ClusterSize; i++)
		rootDir.push_back(0);
	myPartition->writeCluster(1, rootDir.data());

	beingFormatted = false;

	signal(semFormat);
	LeaveCriticalSection(&mutex);
	return 1;
}

FileCnt KernelFS::readRootDir() {
	EnterCriticalSection(&mutex);

	char rootDir[ClusterSize];
	myPartition->readCluster(1, rootDir);
	FileCnt fileCount = 0;

	for (unsigned long i = 0; i < ClusterSize; i += 4) {
		
		unsigned long secondIndex = *(unsigned long*)(rootDir + i);
		
		if (secondIndex == 0)
			continue;

		char secondIndexCluster[ClusterSize];
		myPartition->readCluster(secondIndex, secondIndexCluster);

		for (unsigned long j = 0; j < ClusterSize; j += 4) {

			unsigned long fileInfoIndex = *(unsigned long*)(secondIndexCluster + j);

			if (fileInfoIndex == 0)
				continue;

			char fileInfoCluster[ClusterSize];
			myPartition->readCluster(fileInfoIndex, fileInfoCluster);

			for (int k = 0; k < ClusterSize; k += 32) {
				
				if (fileInfoCluster[k] != 0)
					fileCount++;
				else
					continue; 
			}
		}
	}

	LeaveCriticalSection(&mutex);
	return fileCount;
}

char KernelFS::doesExist(char* fname) {
	EnterCriticalSection(&mutex);

	doesExistNotSynch(fname);

	LeaveCriticalSection(&mutex);
	return 0;
}


File* KernelFS::open(char* fname, char mode) {
	return 0;
}

char KernelFS::deleteFile(char* fname) {
	EnterCriticalSection(&mutex);

	if (true) { // PLUS PROVERA DA NIJE OTVOREN (tabela otvorenih fajlova
		FileIndexes fileIndexes(getFileIndexes(fname));

		if (fileIndexes.fileFirstIndex == 0 && fileIndexes.rootDataIndex == 0 && fileIndexes.rootSecondIndex == 0) {
			LeaveCriticalSection(&mutex);
			return 0;
		}
			
		deleteFileIndexes(fileIndexes);
		deleteRootIndexes(fname, fileIndexes);
	}
	else {
		LeaveCriticalSection(&mutex);
		return 0;
	}

	LeaveCriticalSection(&mutex);
	return 1;
}

void KernelFS::deleteRootIndexes(char* fname, KernelFS::FileIndexes fileIndexes) {
	vector<ClusterNo> clustersToFree;
	int count = 0;

	char fileData[ClusterSize];
	myPartition->readCluster(fileIndexes.rootDataIndex, fileData);
	
	for (unsigned long i = 0; i < ClusterSize && count <= 2; i += 32) {
		if (fileData[i] == 0)
			continue;
		if (!getFileName(fileData + i).compare(getFileNameFromPath(fname)) && !getFileExt(fileData + i).compare(getFileExtFromPath(fname))) 
			fileData[i] = 0;
		count++;
	}
	
	if (count == 1) {
		for (unsigned long i = 0; i < ClusterSize; i += 1)
			fileData[i] = 0;
		clustersToFree.push_back(fileIndexes.rootDataIndex);

		int count2 = 0;
		
		myPartition->writeCluster(fileIndexes.rootDataIndex, fileData);

		char secondCluster[ClusterSize];
		myPartition->readCluster(fileIndexes.rootSecondIndex, secondCluster);

		for (unsigned long i = 0; i < ClusterSize && count2 <= 2; i += 4) {
			if (*(unsigned long*)(secondCluster + i) == 0)
				continue;
			if (*(unsigned long*)(secondCluster + i) == fileIndexes.rootDataIndex)
				*(unsigned long*)(secondCluster + i) = 0;
			count2++;
		}

		myPartition->writeCluster(fileIndexes.rootSecondIndex, secondCluster);

		if (count2 == 0) {
			clustersToFree.push_back(fileIndexes.rootSecondIndex);

			char rootDir[ClusterSize];
			myPartition->readCluster(1, rootDir);

			for (unsigned long i = 0; i < ClusterSize; i += 4) {
				if (*(unsigned long*)(rootDir + i) == fileIndexes.rootSecondIndex)
					*(unsigned long*)(rootDir + i) = 0;
			}

			myPartition->writeCluster(1, rootDir);
		}
	}
	else
		myPartition->writeCluster(fileIndexes.rootDataIndex, fileData);

	freeClusterInBitVector(clustersToFree);
}

void KernelFS::deleteFileIndexes(KernelFS::FileIndexes fileIndexes) {
	bool breakLoop = false;
	vector<ClusterNo> clustersToFree;

	ClusterNo fileFirstIndex = fileIndexes.fileFirstIndex;
	char fileFirstIndexCluster[ClusterSize];
	myPartition->readCluster(fileFirstIndex, fileFirstIndexCluster);

	for (unsigned long i = 0; i < ClusterSize && !breakLoop; i += 4) {

		unsigned long fileSecondIndex = *(unsigned long*)(fileFirstIndexCluster + i);

		if (fileSecondIndex == 0)
			break;

		char fileSecondIndexCluster[ClusterSize];
		myPartition->readCluster(fileSecondIndex, fileSecondIndexCluster);

		for (unsigned long j = 0; j < ClusterSize && !breakLoop; j += 4) {

			unsigned long fileDataIndex = *(unsigned long*)(fileSecondIndexCluster + j);

			if (fileDataIndex == 0) {
				breakLoop = true;
				break;
			}

			myPartition->writeCluster(fileDataIndex, emptyCluster);
			clustersToFree.push_back(fileDataIndex);
		}

		for (int k = 0; k < ClusterSize; k += 1)
			fileSecondIndexCluster[k] = 0;

		myPartition->writeCluster(fileSecondIndex, fileSecondIndexCluster);
		clustersToFree.push_back(fileSecondIndex);
	}

	for (int k = 0; k < ClusterSize; k += 1)
		fileFirstIndexCluster[k] = 0;

	myPartition->writeCluster(fileFirstIndex, fileFirstIndexCluster);
	clustersToFree.push_back(fileFirstIndex);

	freeClusterInBitVector(clustersToFree);
}

//ovo vrv suvisno, moze direkt  u doesExist
char KernelFS::doesExistNotSynch(char* fname) {
	char rootDir[ClusterSize];
	myPartition->readCluster(1, rootDir);

	for (unsigned long i = 0; i < ClusterSize; i += 4) {

		unsigned long secondIndex = *(unsigned long*)(rootDir + i);

		if (secondIndex == 0)
			continue;

		char secondIndexCluster[ClusterSize];
		myPartition->readCluster(secondIndex, secondIndexCluster);

		for (unsigned long j = 0; j < ClusterSize; j += 4) {

			unsigned long fileInfoIndex = *(unsigned long*)(secondIndexCluster + j);

			if (fileInfoIndex == 0)
				continue;

			char fileInfoCluster[ClusterSize];
			myPartition->readCluster(fileInfoIndex, fileInfoCluster);

			for (int k = 0; k < ClusterSize; k += 32) {

				if (fileInfoCluster[k] == 0)
					continue; 

				if (!getFileNameFromPath(fname).compare(getFileName(fileInfoCluster + k)) && !getFileExtFromPath(fname).compare(getFileExt(fileInfoCluster + k))) {
					LeaveCriticalSection(&mutex);
					return 1;
				}

			}
		}
	}
	return 0;
}

KernelFS::FileIndexes KernelFS::getFileIndexes(char* fname) {
	char rootDir[ClusterSize];
	for (unsigned long i = 0; i < ClusterSize; i += 4) {

		unsigned long secondIndex = *(unsigned long*)(rootDir + i);

		if (secondIndex == 0)
			continue;

		char secondIndexCluster[ClusterSize];
		myPartition->readCluster(secondIndex, secondIndexCluster);

		for (unsigned long j = 0; j < ClusterSize; j += 4) {

			unsigned long fileInfoIndex = *(unsigned long*)(secondIndexCluster + j);

			if (fileInfoIndex == 0)
				continue;

			char fileInfoCluster[ClusterSize];
			myPartition->readCluster(fileInfoIndex, fileInfoCluster);

			for (unsigned long k = 0; k < ClusterSize; k += 32) {

				if (fileInfoCluster[k] == 0)
					continue;

				if (!getFileNameFromPath(fname).compare(getFileName(fileInfoCluster + k)) && !getFileExtFromPath(fname).compare(getFileExt(fileInfoCluster + k)))
					return FileIndexes(getFileFirstIndex(fileInfoCluster + k), secondIndex, fileInfoIndex);

			}
		}
	}
	return FileIndexes(0, 0, 0);
}

void KernelFS::freeClusterInBitVector(vector<ClusterNo> clusterNumbers) {
	if (clusterNumbers.size() <= 0)
		return;

	unsigned char bitVector[ClusterSize];
	myPartition->readCluster(0, (char*)bitVector);

	for (unsigned long i = 0; i < clusterNumbers.size(); i++) {
		ClusterNo byte = clusterNumbers[i] / 8;
		int offset = clusterNumbers[i] % 8;
		bitVector[byte] |= 1 << (7 - offset);
	}

	myPartition->writeCluster(0, (char*)bitVector);
}

ClusterNo KernelFS::getFileFirstIndex(char* text) {
	return *(ClusterNo*)(text + 12);
}

void KernelFS::setFileFirstIndex(char* text, ClusterNo clusterNumber) {
	ClusterNo cl = *(ClusterNo*)(text + 12) = clusterNumber;

}

BytesCnt KernelFS::getFileSize(char* text) {
	return *(BytesCnt*)(text + 16);
}

void KernelFS::setFileSize(char* text, BytesCnt fileSize) {
	*(BytesCnt*)(text + 16) = fileSize;
}

string KernelFS::getFileName(string fname) {
	unsigned short space = fname.find_first_of(" ");
	return fname.substr(0, ((8 < space) ? 8 : space));
}

void KernelFS::setFileName(string& text, string fname) {
	text.replace(0, fname.size(), fname);
	text.replace(fname.size(), 8 - fname.size(), 8 - fname.size(), ' ');
}

string KernelFS::getFileExt(string fname) {
	unsigned short space = fname.find_first_of(" ", 8);
	return fname.substr(8, ((11 < space) ? (11 - 8) : (space - 8)));
}

void KernelFS::setFileExt(string& text, string fext) {
	text.replace(8, fext.size(), fext);
	text.replace(8 + fext.size(), 3 - fext.size(), 3 - fext.size(), ' ');
	text[11] = 0;
}

string KernelFS::getFileNameFromPath(string path) {
	unsigned short name = path.find_last_of("/");
	unsigned short ext = path.find_last_of(".");
	return path.substr(name + 1, ext - name - 1);
}

string KernelFS::getFileExtFromPath(string path) {
	unsigned short ext = path.find_last_of(".");
	return path.substr(ext + 1);
}

string KernelFS::setFileData(ClusterNo clusterNumber, BytesCnt fileSize, char* fname, char* fext) {
	char* fileData = new char[32];
	setFileSize(fileData, fileSize);
	setFileFirstIndex(fileData, clusterNumber);


	string ret(fileData, 32);
	setFileName(ret, fname);
	setFileExt(ret, fext);

	delete [] fileData;

	return ret;
}
