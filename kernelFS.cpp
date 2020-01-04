//DODATI ZA SVAKU FJU PROVERU DA L JE PARTICIJA NAKACENA I DA LI SE TRENUTNO FORMATIRA!

#include "kernelFS.h"
#include "particija-VS2017/part.h"
#include "kernelFile.h"
#include "file.h"

#include <vector>
using std::vector;


CRITICAL_SECTION KernelFS::mutex;
HANDLE KernelFS::semFilesClosed = CreateSemaphore(NULL, 1, 32, NULL);
HANDLE KernelFS::semUnmount = CreateSemaphore(NULL, 1, 32, NULL);

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
	bitVector.push_back(0x3F); //1100 0000 C0 ; 3F 0011 1111
	for (unsigned long i = 1; i < totalBytes; i++)
		bitVector.push_back(-1); //treba -1 (jer je 1 za free, 0 za zauzet) (bilo je 0)
	myPartition->writeCluster(0, bitVector.data()); //VIDI DA L RADI

	//rootDir init
	vector<char> rootDir;
	for (unsigned long i = 0; i < ClusterSize; i++)
		rootDir.push_back(0);
	myPartition->writeCluster(1, rootDir.data());

	beingFormatted = false;
	
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
				
				unsigned char firstByte = fileInfoCluster[k];
				
				if (firstByte != 0) //izbaci firstByte i kucaj odmah fileInfoCluster[k]
					fileCount++;
				else
					continue; //mozda bolje brejk (pogledaj doesExist)
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

	if (doesExistNotSynch(fname)) { // PLUS PROVERA DA NIJE OTVOREN (tabela otvorenih fajlova
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

				for (int k = 0; k < ClusterSize; k += 32) {

					if (fileInfoCluster[k] == 0)
						continue;
					if (!getFileNameFromPath(fname).compare(getFileName(fileInfoCluster + k)) && !getFileExtFromPath(fname).compare(getFileExt(fileInfoCluster + k))) {
						
						return getFileFirstIndex(fileInfoCluster + k), level2Index, fileInfoIndex;
					}

				}
			}
		}
	}

	LeaveCriticalSection(&mutex);
	return 0;
}


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

				unsigned char firstByte = fileInfoCluster[k];

				if (firstByte == 0)
					continue; // da li ovde treba break, jer ako nema u prvom bajtu nista nece ni nadalje imati nista

				if (!getFileNameFromPath(fname).compare(getFileName(fileInfoCluster + k)) && !getFileExtFromPath(fname).compare(getFileExt(fileInfoCluster + k))) {
					LeaveCriticalSection(&mutex);
					return 1;
				}

			}
		}
	}
	return 0;
}

ClusterNo KernelFS::getFileFirstIndex(char* text) {
	return *(ClusterNo*)(text + 12);
}

BytesCnt KernelFS::getFileSize(char* text) {
	return *(BytesCnt*)(text + 16);
}

string KernelFS::getFileName(string fname) {
	unsigned short space = fname.find_first_of(" ");
	return fname.substr(0, ((8 < space) ? 8 : space));
}

string KernelFS::getFileExt(string fname) {
	unsigned short space = fname.find_first_of(" ", 8);
	return fname.substr(8, ((11 < space) ? (11 - 8) : (space - 8)));
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
