//DODATI ZA SVAKU FJU PROVERU DA L JE PARTICIJA NAKACENA I DA LI SE TRENUTNO FORMATIRA!

//KADA SE IZBACUJE IZ TABELE OTVORENIH FAJLOVA?!

#include "kernelFS.h"
#include "particija-VS2017/part.h"
#include "kernelFile.h"
#include "file.h"
#include "FCB.h"

CriticalSectionInit KernelFS::init;
CRITICAL_SECTION KernelFS::mutex;
HANDLE KernelFS::semFilesClosed = CreateSemaphore(NULL, 1, 32, NULL);
HANDLE KernelFS::semUnmount = CreateSemaphore(NULL, 1, 32, NULL);
HANDLE KernelFS::semFormat = CreateSemaphore(NULL, 1, 32, NULL);

Partition* KernelFS::myPartition = nullptr;
bool KernelFS::beingFormatted = false;
ClusterNo KernelFS::clusterCount = 0;

map<string, FCB>* KernelFS::openFileTable = new map<string, FCB>();



char KernelFS::mount(Partition* partition) {
	EnterCriticalSection(&mutex);

	if (partition == nullptr) {
		LeaveCriticalSection(&mutex);
		return 0;
	}

	while (myPartition != nullptr) { //WHILE ILI IF
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

	while (getCountOfOpenFiles() > 0) { //WHILE ILI IF
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

	while (getCountOfOpenFiles() > 0) { //WHILE ILI IF
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
	if (myPartition == nullptr)
		return -1;
	//AKO NEKO RADI MOUNT UNMOUNT ILI FORMAT ONDA ZABRANITI???
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
//treba odvojeno da izbegnem dupli mutex
char KernelFS::doesExistNotSynch(char* fname) {
	if (myPartition == nullptr)
		return 0;
	//AKO NEKO RADI MOUNT UNMOUNT ILI FORMAT ONDA ZABRANITI???

	FileIndexes fileIndexes(getFileIndexes(fname));
	if (fileIndexes.fileFirstIndex == 0 && fileIndexes.rootDataIndex == 0 && fileIndexes.rootSecondIndex == 0)
		return 0;
	else
		return 1;
}



File* KernelFS::open(char* fname, char mode) {
	if (fname == nullptr || myPartition == nullptr)
		return nullptr;
	//AKO NEKO RADI MOUNT UNMOUNT ILI FORMAT ONDA ZABRANITI???
	EnterCriticalSection(&mutex);
	File* ret = nullptr;
	
	while (beingFormatted) {	//WHILE ILI IF
		LeaveCriticalSection(&mutex);
		wait(semFormat);
		EnterCriticalSection(&mutex);
	}

	if (myPartition == nullptr)
		return nullptr;

	
	bool fileIsOpen = isFileOpen(fname);

	if (fileIsOpen) {
		FCB fcb = openFileTable->find(getFileNameFromPath(fname) + getFileExtFromPath(fname))->second;
		if (mode != 'r' || (mode == 'r' && fcb.mode != mode)) {
			while (!fcb.closedFile) { // WHILE ILI IF
				LeaveCriticalSection(&mutex);
				wait(fcb.semFile);
				EnterCriticalSection(&mutex);
			}
			fileIsOpen = false; //PROVERA
		}
	}

	bool fileExists = doesExistNotSynch(fname);

	switch (mode) {
	case 'w':
		openForWrite(fname, mode, fileIsOpen, fileExists);
	case 'r':
		openForRead(fname, mode, fileIsOpen, fileExists);
	case 'a':
		openForAppend(fname, mode, fileIsOpen, fileExists);
	default: 
		ret = nullptr;
	}
	


	LeaveCriticalSection(&mutex);
	return ret;
}

File* KernelFS::openForWrite(char* fname, char mode, bool fileIsOpen, bool fileExists) {
	File* ret = nullptr;

	if (!fileExists) {
		std::pair<ClusterNo, BytesCnt> pair = writeFileInRoot(fname);
		if (pair.first == 0 && pair.second == 0)
			ret = nullptr;
		else
			ret = createFile(fname, mode, pair);
	}
	else {
		if (!fileIsOpen) {
			deleteFileNotSynch(fname);
			std::pair<ClusterNo, BytesCnt> pair = writeFileInRoot(fname);
			if (pair.first == 0 && pair.second == 0)
				ret = nullptr;
			else
				ret = createFile(fname, mode, pair);
		}
		else {
			//obradjeno prethodno nece uci nikad
		}
	}
	return ret;
}

File* KernelFS::openForRead(char* fname, char mode, bool fileIsOpen, bool fileExists) {
	File* ret = nullptr;

	if (!fileExists) {
		ret = nullptr;
	}
	else {
		if (!fileIsOpen) {
			FileIndexes fileIndexes = getFileIndexes(fname);
			ret = createFile(fname, mode, std::make_pair(fileIndexes.rootDataIndex, fileIndexes.offsetInRootDataIndex));
		}
		else {
			FCB fcb = openFileTable->find(getFileNameFromPath(fname) + getFileExtFromPath(fname))->second;
			if (fcb.mode == 'w' || fcb.mode == 'a') {
				//obradjeno prethodno nece uci nikad				
			}
			else {
				fcb.numOfRefs++;
				ret = new File(fname, mode, fcb.fileSize, fcb.fileFirstIndex, 0);
			}
		}
	}
	return ret;
}

File* KernelFS::openForAppend(char* fname, char mode, bool fileIsOpen, bool fileExists) {
	File* ret = nullptr;

	if (!fileExists) {
		ret = nullptr;
	}
	else {
		if (!fileIsOpen) {
			FileIndexes fileIndexes = getFileIndexes(fname);
			std::pair<ClusterNo, BytesCnt> pair = writeFileInRoot(fname);
			if (pair.first == 0 && pair.second == 0)
				ret = nullptr;
			else
				ret = createFile(fname, mode, pair);
		}
		else {
			//obradjeno prethodno nece uci nikad
		}
	}
	return ret;
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

		//provera da l moze oba klastera da nadje
		ClusterNo newClusterSecondIndex = findFreeCluster();
		ClusterNo newFileInfoNo = findFreeCluster();
		if (newClusterSecondIndex == -1 || newFileInfoNo == -1)
			return std::make_pair(0, 0);

		myPartition->writeCluster(newClusterSecondIndex, emptyCluster);

		char* newClusterSecond = (char*)(&newClusterSecondIndex);
		memcpy(rootDir + freeInFirstIndex, newClusterSecond, 4);
		myPartition->writeCluster(1, rootDir);

		//dodaje prazan fileData cluster
		char secondIndexCluster[ClusterSize];
		myPartition->readCluster(newClusterSecondIndex, secondIndexCluster);

		myPartition->writeCluster(newFileInfoNo, emptyCluster);

		char* newFileInfo = (char*)(&newFileInfoNo);
		memcpy(secondIndexCluster, newFileInfo, 4);
		myPartition->readCluster(newClusterSecondIndex, secondIndexCluster);

		return std::make_pair(newFileInfoNo, 0);
	}

	return std::make_pair(0, 0);

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

	char fileData[ClusterSize];
	myPartition->readCluster(pair.first, fileData);

	FCB fcb(getFileFirstIndex(fileData), pair.first, pair.second, mode, fileName);
	
	map<string, FCB>::iterator it = openFileTable->begin();
	it = openFileTable->find(fname);
	openFileTable->erase(it);

	openFileTable->insert(std::pair<string, FCB>(fileName, fcb));

	switch (mode) {
	case 'r':
		fcb.fileSize = getFileSize(fileData + pair.second);
		return new File(fname, mode, fcb.fileSize, fcb.fileFirstIndex, 0);
	case 'w':
		fcb.fileSize = 0;
		return new File(fname, mode, fcb.fileSize, fcb.fileFirstIndex, 0);
	case 'a':
		fcb.fileSize = getFileSize(fileData + pair.second);
		return new File(fname, mode, fcb.fileSize, fcb.fileFirstIndex, fcb.fileSize);
	default:
		return nullptr;
	}

	return nullptr;
}



char KernelFS::deleteFile(char* fname) {
	if (myPartition == nullptr)
		return 0;
	//AKO NEKO RADI MOUNT UNMOUNT ILI FORMAT ONDA ZABRANITI???
	EnterCriticalSection(&mutex);

	char ret = deleteFileNotSynch(fname);

	LeaveCriticalSection(&mutex);
	return ret;
}
char KernelFS::deleteFileNotSynch(char* fname) {
	if (!isFileOpen(fname)) { 
		FileIndexes fileIndexes = getFileIndexes(fname); //ili fileIndexes(getFileIndexes(fname));

		//file doesn't exist
		if (fileIndexes.fileFirstIndex == 0 && fileIndexes.rootDataIndex == 0 && fileIndexes.rootSecondIndex == 0)
			return 0;

		deleteFileIndexes(fileIndexes);
		deleteRootIndexes(fname, fileIndexes); //da li treba ovo?
	}
	else
		return 0;

	return 1;
}

FileIndexes KernelFS::getFileIndexes(char* fname) {
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

void KernelFS::deleteRootIndexes(char* fname, FileIndexes fileIndexes) {
	vector<ClusterNo> clustersToFree;
	int count = 0;

	char fileData[ClusterSize];
	myPartition->readCluster(fileIndexes.rootDataIndex, fileData);
	
	fileData[fileIndexes.offsetInRootDataIndex] = 0;
	for (unsigned long i = 0; i < ClusterSize && count == 0; i += 32) {
		if (fileData[i] == 0)
			continue;
		count++;
	}

	if (count == 0) {
		for (unsigned long i = 0; i < ClusterSize; i++)
			fileData[i] = 0;
		clustersToFree.push_back(fileIndexes.rootDataIndex);
		myPartition->writeCluster(fileIndexes.rootDataIndex, fileData);

		int count2 = 0;

		char secondCluster[ClusterSize];
		myPartition->readCluster(fileIndexes.rootSecondIndex, secondCluster);

		*(unsigned long*)(secondCluster + fileIndexes.rootDataIndex) = 0;
		for (unsigned long i = 0; i < ClusterSize && count2 == 0; i += 4) {
			if (*(unsigned long*)(secondCluster + i) == 0)
				continue;
			count2++;
		}

		myPartition->writeCluster(fileIndexes.rootSecondIndex, secondCluster);

		if (count2 == 0) {
			clustersToFree.push_back(fileIndexes.rootSecondIndex);

			char rootDir[ClusterSize];
			myPartition->readCluster(1, rootDir);

			*(unsigned long*)(rootDir + fileIndexes.rootSecondIndex) = 0;

			myPartition->writeCluster(1, rootDir);
		}
	}
	else
		myPartition->writeCluster(fileIndexes.rootDataIndex, fileData);

	if (clustersToFree.size() > 0)
		freeClusterInBitVector(clustersToFree);
}
//proveri jos jednom da l moze da se desi da ako nadjem prvu nulu da posle toga ipak bude negde unos validan
void KernelFS::deleteFileIndexes(FileIndexes fileIndexes) {
	bool breakLoop = false;
	vector<ClusterNo> clustersToFree;

	ClusterNo fileFirstIndex = fileIndexes.fileFirstIndex;
	
	if (fileFirstIndex == 0)
		return;

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

	if (clustersToFree.size() > 0)
		freeClusterInBitVector(clustersToFree);
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



bool KernelFS::isFileOpen(char* fname) {
	return openFileTable->find(getFileNameFromPath(fname) + getFileExtFromPath(fname)) != openFileTable->end();
}

int KernelFS::getCountOfOpenFiles() {
	int count = 0;
	for (auto it = openFileTable->begin(); it != openFileTable->end(); it++)
		count += (*it).second.numOfRefs;
	return count;
}



//Get&Set  FileData

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