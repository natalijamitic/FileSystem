//DODATI ZA SVAKU FJU PROVERU DA L JE PARTICIJA NAKACENA I DA LI SE TRENUTNO FORMATIRA!

//filesOpened kada se setuje? da li tu treba gledati iz tabele otvorenih fajlova
//KADA SE IZBACUJE IZ TABELE OTVORENIH FAJLOVA?!

#include "kernelFS.h"
#include "particija-VS2017/part.h"
#include "kernelFile.h"
#include "file.h"
#include "FCB.h"


CRITICAL_SECTION KernelFS::mutex;
HANDLE KernelFS::semFilesClosed = CreateSemaphore(NULL, 1, 32, NULL);
HANDLE KernelFS::semUnmount = CreateSemaphore(NULL, 1, 32, NULL);
HANDLE KernelFS::semFormat = CreateSemaphore(NULL, 1, 32, NULL);

Partition* KernelFS::myPartition = nullptr;
bool KernelFS::filesOpened = false;
bool KernelFS::beingFormatted = false;
ClusterNo KernelFS::clusterCount = 0;

map<string, FCB*>* KernelFS::openFileTable = new map<string, FCB*>();
map<string, HANDLE>* KernelFS::semMapFileClosed = new map<string, HANDLE>();

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
	EnterCriticalSection(&mutex);
	File* ret = nullptr;
	
	if (beingFormatted) {
		LeaveCriticalSection(&mutex);
		wait(semFormat);
		EnterCriticalSection(&mutex);
	}

	bool fileIsOpen = false;
	bool fileExists = false;
	
	if (doesExistNotSynch(fname)) {
		fileExists = true;

		string fileName = getFileNameFromPath(fname) + getFileExtFromPath(fname);
		
		if (openFileTable->find(fileName) == openFileTable->end())
			fileIsOpen = false;
		else
			fileIsOpen = true;
	}

	if (!fileExists) {
		switch (mode) {
		case 'w': {
			std::pair<ClusterNo, BytesCnt> pair = writeFileInRoot(fname);
			if (pair.first == 0 && pair.second == 0)
				ret = nullptr;
			else
				ret = createFile(fname, mode, pair);
			break;
		}
		case 'r':
		case 'a':
		default:
			ret = nullptr;
		}
	}
	else {
		if (!fileIsOpen) {
			switch (mode) {
			case 'w': {
				deleteFileNotSynch(fname);
				std::pair<ClusterNo, BytesCnt> pair = writeFileInRoot(fname);
				if (pair.first == 0 && pair.second == 0)
					ret = nullptr;
				else
					ret = createFile(fname, mode, pair);
				break;
			}
			case 'r': {
				KernelFS::FileIndexes fileIndexes = getFileIndexes(fname);
				ret = createFile(fname, mode, std::make_pair(fileIndexes.rootDataIndex, fileIndexes.offsetInRootDataIndex));
				break;
			}
			case 'a': {
				KernelFS::FileIndexes fileIndexes = getFileIndexes(fname);
				std::pair<ClusterNo, BytesCnt> pair = writeFileInRoot(fname);
				if (pair.first == 0 && pair.second == 0)
					ret = nullptr;
				else
					ret = createFile(fname, mode, pair);
				break;
			}
			default:
				ret = nullptr;
			}
		}
		else {
			switch (mode) {
			case 'w': //cekaj ducu
				break;
			case 'r': {
				string name = getFileNameFromPath(fname);
				string ext = getFileExtFromPath(fname);
				FCB* fcb = openFileTable->find(name + ext)->second;
				if (fcb->getMode() == 'w' || fcb->getMode() == 'a') {
					LeaveCriticalSection(&mutex);
					wait(semMapFileClosed->find(name + ext)->second);
					EnterCriticalSection(&mutex);
					semMapFileClosed->erase(name + ext);
					KernelFS::FileIndexes fileIndexes = getFileIndexes(fname);
					ret = createFile(fname, mode, std::make_pair(fileIndexes.rootDataIndex, fileIndexes.offsetInRootDataIndex));
				}
				else {
					fcb->addRef();
					ret = new File(fcb, 0);
				}
				break;
			}
			case 'a': //cekaj ducu
				break;
			default:
				ret = nullptr;
			}
		}
	}

	LeaveCriticalSection(&mutex);
	return nullptr;
}

std::pair<ClusterNo, BytesCnt> KernelFS::findFreeSpotInCluster() {
	bool full = false;
	unsigned long freeInFirstIndex = -1;
	unsigned long freeInSecondIndex = -1;
	unsigned long freeInSecondIndexOrigin = -1;

	char rootDir[ClusterSize];
	myPartition->readCluster(1, rootDir);

	for (int i = 0; i < ClusterSize; i += 4) {
		unsigned long secondIndex = *(unsigned long*)(rootDir + i);

		if (secondIndex == 0) {
			if (freeInFirstIndex == -1)
				freeInFirstIndex = i;
			continue;
		}

		char secondIndexCluster[ClusterSize];
		myPartition->readCluster(secondIndex, secondIndexCluster);

		for (int j = 0; j < ClusterSize; j += 4) {
			unsigned long fileInfoIndex = *(unsigned long*)(secondIndex + j);

			if (fileInfoIndex == 0) {
				if (freeInSecondIndex == -1) {
					freeInSecondIndex = j;
					freeInSecondIndexOrigin = secondIndex;
				}
				break;
			}

			char fileInfoCluster[ClusterSize];
			myPartition->readCluster(fileInfoIndex, fileInfoCluster);
			
			for (int k = 0; k < ClusterSize; k += 32)
				if (fileInfoCluster[k] == 0)
					return std::make_pair(fileInfoIndex, k);
		}
	}

	if (freeInSecondIndex != -1) {
		//dodaje prazan fileData cluster
		char secondIndexCluster[ClusterSize];
		myPartition->readCluster(freeInSecondIndexOrigin, secondIndexCluster);

		ClusterNo newClusterNo = findFreeCluster();
		if (newClusterNo == -1)
			return std::make_pair(0, 0);
		myPartition->writeCluster(newClusterNo, emptyCluster);

		char* newCluster = (char*)(&newClusterNo);
		memcpy(secondIndexCluster + freeInSecondIndex, newCluster, 4);
		myPartition->writeCluster(freeInSecondIndexOrigin, secondIndexCluster);

		return std::make_pair(newClusterNo, 0);
	}
	if (freeInFirstIndex != -1) {
		//dodaje prazan indeks/klaster drugog nivoa
		char rootDir[ClusterSize];
		myPartition->readCluster(1, rootDir);

		ClusterNo newClusterSecondIndex = findFreeCluster();
		if (newClusterSecondIndex == -1)
			return std::make_pair(0, 0);
		myPartition->writeCluster(newClusterSecondIndex, emptyCluster);

		char* newClusterSecond = (char*)(&newClusterSecondIndex);
		memcpy(rootDir + freeInFirstIndex, newClusterSecond, 4);
		myPartition->writeCluster(1, rootDir);

		//dodaje prazan fileData cluster
		char secondIndexCluster[ClusterSize];
		myPartition->readCluster(newClusterSecondIndex, secondIndexCluster);

		ClusterNo newFileInfoNo = findFreeCluster();
		if (newFileInfoNo == -1)
			return std::make_pair(0, 0);
		myPartition->writeCluster(newFileInfoNo, emptyCluster);

		char* newFileInfo = (char*)(&newFileInfoNo);
		memcpy(secondIndexCluster, newFileInfo, 4);
		myPartition->readCluster(newClusterSecondIndex, secondIndexCluster);

		return std::make_pair(newFileInfoNo, 0);
	}

	return std::make_pair(0,0);

}

ClusterNo KernelFS::findFreeCluster() {
	unsigned char bitVector[ClusterSize];
	myPartition->readCluster(0, (char*)bitVector);

	for (int i = 0; i < ceil(clusterCount / 8); i++) {
		if (bitVector[i] == 0)
			continue;

		for (int j = 0; j < 8; j++) {
			if (bitVector[i] & (0x80 >> j)) { //0x80 1000 0000
				bitVector[i] &= ~(0x80 >> j);
				myPartition->writeCluster(0, (char*)bitVector);
				return i * 8 + j;
			}
		}
	}
	return -1;
}

std::pair<ClusterNo, BytesCnt> KernelFS::writeFileInRoot(char* fname) {
	std::pair<ClusterNo, BytesCnt> pair = findFreeSpotInCluster();

	if (pair.first == 0 && pair.second == 0)
		return pair;

	string fileInfo = setFileData(0, 0, getFileNameFromPath(fname).data(), getFileExtFromPath(fname).data());
	
	unsigned char fileDataCluster[ClusterSize];
	myPartition->readCluster(pair.first, (char*)fileDataCluster);
	memcpy(fileDataCluster + pair.second, fileInfo.data(), 32);
	myPartition->writeCluster(pair.first, (char*)fileDataCluster);

	return pair;
}

File* KernelFS::createFile(char* fname, char mode, std::pair<ClusterNo, BytesCnt> pair) {
	string fileName = getFileNameFromPath(fname) + getFileExtFromPath(fname);
	FCB* fcb = new FCB(pair.first, pair.second, mode, fileName);
	openFileTable->insert(std::pair<string, FCB*>(getFileNameFromPath(fname) + getFileExtFromPath(fname), fcb));
	semMapFileClosed->insert(std::pair<string, HANDLE>(getFileNameFromPath(fname) + getFileExtFromPath(fname), CreateSemaphore(NULL, 0, 32, NULL)));
	
	switch (mode) {
	case 'r':
	case 'w':
		return new File(fcb, 0);
	case 'a':
		char fileData[ClusterSize];
		myPartition->readCluster(pair.first, fileData);
		return new File(fcb, getFileSize(fileData + pair.second));
	}
	//return null;
}

char KernelFS::deleteFile(char* fname) {
	EnterCriticalSection(&mutex);

	if (deleteFileNotSynch(fname) == 0) {
		LeaveCriticalSection(&mutex);
		return 0;
	}

	LeaveCriticalSection(&mutex);
	return 1;
}

char KernelFS::deleteFileNotSynch(char* fname) {
	if (!isFileOpen(fname)) { 
		FileIndexes fileIndexes(getFileIndexes(fname));

		if (fileIndexes.fileFirstIndex == 0 && fileIndexes.rootDataIndex == 0 && fileIndexes.rootSecondIndex == 0)
			return 0;

		deleteFileIndexes(fileIndexes);
		deleteRootIndexes(fname, fileIndexes);
	}
	else
		return 0;

	return 1;
}

bool KernelFS::isFileOpen(char* fname) {
	return openFileTable->find(getFileNameFromPath(fname) + getFileExtFromPath(fname)) != openFileTable->end();
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
				* (unsigned long*)(secondCluster + i) = 0;
			count2++;
		}

		myPartition->writeCluster(fileIndexes.rootSecondIndex, secondCluster);

		if (count2 == 0) {
			clustersToFree.push_back(fileIndexes.rootSecondIndex);

			char rootDir[ClusterSize];
			myPartition->readCluster(1, rootDir);

			for (unsigned long i = 0; i < ClusterSize; i += 4) {
				if (*(unsigned long*)(rootDir + i) == fileIndexes.rootSecondIndex)
					* (unsigned long*)(rootDir + i) = 0;
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

//treba odvojeno da izbegnem dupli mutex
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

			for (unsigned long k = 0; k < ClusterSize; k += 32) {

				if (fileInfoCluster[k] == 0)
					continue;

				if (!getFileNameFromPath(fname).compare(getFileName(fileInfoCluster + k)) && !getFileExtFromPath(fname).compare(getFileExt(fileInfoCluster + k)))
					return FileIndexes(getFileFirstIndex(fileInfoCluster + k), secondIndex, fileInfoIndex, k);

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
	*(ClusterNo*)(text + 12) = clusterNumber;
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

string KernelFS::setFileData(ClusterNo clusterNumber, BytesCnt fileSize, const char* fname, const char* fext) {
	char* fileData = new char[32];
	setFileSize(fileData, fileSize);
	setFileFirstIndex(fileData, clusterNumber);

	string ret(fileData, 32);
	setFileName(ret, fname);
	setFileExt(ret, fext);

	delete [] fileData;
	return ret;
}