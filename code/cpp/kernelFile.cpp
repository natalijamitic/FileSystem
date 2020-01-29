#include "kernelFile.h"

KernelFile::KernelFile(string name, char mode, BytesCnt size, ClusterNo cluster, BytesCnt cursor) {
	this->name = name;
	this->mode = mode;
	this->size = size;
	this->firstIndex = cluster;
	this->cursor = cursor;
	this->searchName = KernelFS::getFileNameFromPath(name) + KernelFS::getFileExtFromPath(name);
}

KernelFile::~KernelFile() {
	EnterCriticalSection(&KernelFS::mutex);
	
	map<string, FCB>::iterator it = KernelFS::openFileTable->begin();
	it = KernelFS::openFileTable->find(searchName);

	if (it == KernelFS::openFileTable->end()) {
		std::cout << "GRESKA";
		LeaveCriticalSection(&KernelFS::mutex);
		return;
	}

	it->second.numOfRefs--;
	if (it->second.numOfRefs == 0)
		signal(it->second.semFile);

	if (KernelFS::getCountOfOpenFiles() == 0)
		signal(KernelFS::semFilesClosed);

	LeaveCriticalSection(&KernelFS::mutex);
}

char KernelFile::write(BytesCnt cnt, char* buffer) {
	if (mode == 'r' || buffer == nullptr || (cursor + cnt) > MAX_FILE_SIZE)
		return 0;

	EnterCriticalSection(&KernelFS::mutex);

	unsigned long newDataNeeded;
	if (cnt < (size - cursor))
		newDataNeeded = 0;
	else
		newDataNeeded = cnt - (size - cursor);

	if (cursor % ClusterSize) { //dataCluster isn't full;
		if (newDataNeeded < (ClusterSize - cursor))
			newDataNeeded = 0;
		else
			newDataNeeded -= ClusterSize - cursor;
	}

	unsigned long newDataClustersNeeded = newDataNeeded / ClusterSize + (newDataNeeded % ClusterSize > 0);
	unsigned long newSecondIndexClustersNeeded = newDataClustersNeeded / (ClusterSize / sizeof(ClusterNo)) + (newDataClustersNeeded % (ClusterSize / sizeof(ClusterNo)) > 0);
	unsigned long newClustersNeeded = newDataClustersNeeded + newSecondIndexClustersNeeded;

	if (this->firstIndex == 0)
		newClustersNeeded++;

	// TODO: vrati ovo
	/* 
	if (newClustersNeeded && !enoughFreeClusters(newClustersNeeded)) {
		std::cout << "Nema dovoljno memorije da se upisuje u fajl " << this->name;
		LeaveCriticalSection(&KernelFS::mutex);
		return 0;
	}
	*/


	std::map<string, FCB>::iterator it = KernelFS::openFileTable->begin();
	it = KernelFS::openFileTable->find(searchName);
	
	char firstIndexCluster[ClusterSize] = { 0 };
	if (firstIndex) {
		if(!KernelFS::myPartition->readCluster(firstIndex, firstIndexCluster))
			std::cout << "NEUSPEH PRILIKOM CITANJA KLASTERA";
	}
	else {
		unsigned long newFirstIndex = KernelFS::findFreeCluster();
		if (newFirstIndex == 0) {
			std::cout << "Nema dovoljno memorije da se upisuje u fajl " << this->name;
			LeaveCriticalSection(&KernelFS::mutex);
			return 0;
		}

		char dataCluster[ClusterSize];
		KernelFS::myPartition->readCluster(it->second.rootDataIndex, dataCluster);
		
		it->second.fileFirstIndex = firstIndex = newFirstIndex;
		KernelFS::setFileFirstIndex(dataCluster + it->second.offsetInRootDataIndex, firstIndex);
		
		if(!KernelFS::myPartition->writeCluster(it->second.rootDataIndex, dataCluster))
			std::cout << "NEUSPEH PRILIKOM UPISA KLASTERA";
	}

	unsigned long position = 0;

	while (position < cnt) {
		unsigned long secondIndex = *(unsigned long*)(firstIndexCluster + entryNoFirstIndex(cursor));

		char secondIndexCluster[ClusterSize] = { 0 };
		if (secondIndex == 0) {
			unsigned long newSecondIndex = KernelFS::findFreeCluster();
			if (newSecondIndex == 0) {
				std::cout << "Nema dovoljno memorije da se upisuje u fajl " << this->name;
				LeaveCriticalSection(&KernelFS::mutex);
				return 0;
			}

			secondIndex = newSecondIndex;

			char* newSecondIndexCluster = (char*)(&newSecondIndex);
			memcpy(firstIndexCluster + entryNoFirstIndex(cursor), newSecondIndexCluster, 4); 
			KernelFS::myPartition->writeCluster(firstIndex, firstIndexCluster);
		}
		else
			KernelFS::myPartition->readCluster(secondIndex, secondIndexCluster);

		unsigned long dataIndex = *(unsigned long*)(secondIndexCluster + entryNoSecondIndex(cursor));
		
		char dataCluster[ClusterSize] = { 0 };
		if (dataIndex == 0) {
			unsigned long newDataIndex = KernelFS::findFreeCluster();
			if (newDataIndex == 0) {
				std::cout << "Nema dovoljno memorije da se upisuje u fajl " << this->name;
				LeaveCriticalSection(&KernelFS::mutex);
				return 0;
			}

			dataIndex = newDataIndex;

			char* newDataCluster = (char*)(&newDataIndex);
			memcpy(secondIndexCluster + entryNoSecondIndex(cursor), newDataCluster, 4); 
			KernelFS::myPartition->writeCluster(secondIndex, secondIndexCluster);

		}
		else
			KernelFS::myPartition->readCluster(dataIndex, dataCluster);


		for (int j = cursor % ClusterSize; j < ClusterSize; j++, cursor++) {
			if (position >= cnt)
				break;
			dataCluster[j] = buffer[position++];
		}

		KernelFS::myPartition->writeCluster(dataIndex, dataCluster);

		if (size < cursor)
			size = cursor;

		it->second.fileSize = size;

	}

	char dataCluster[ClusterSize];
	KernelFS::myPartition->readCluster(it->second.rootDataIndex, dataCluster);
	KernelFS::setFileSize(dataCluster + it->second.offsetInRootDataIndex, size);
	KernelFS::myPartition->writeCluster(it->second.rootDataIndex, dataCluster);

	LeaveCriticalSection(&KernelFS::mutex);

	return 1;
}

BytesCnt KernelFile::read(BytesCnt size, char* buffer) {
	if (size == 0 || buffer == nullptr || eof() || firstIndex == 0 || mode == 'w')
		return 0;

	EnterCriticalSection(&KernelFS::mutex);

	BytesCnt limit = (size + cursor < this->size) ? (size + cursor) : this->size; //end position of reading
	unsigned long ret = limit - cursor; 
	BytesCnt position = 0;

	char firstIndexCluster[ClusterSize];
	KernelFS::myPartition->readCluster(firstIndex, firstIndexCluster);

	for (BytesCnt i = cursor; i < limit; i += ClusterSize) {
		char secondIndexCluster[ClusterSize];
		unsigned long secondIndex = *(unsigned long*)(firstIndexCluster + entryNoFirstIndex(i));
		KernelFS::myPartition->readCluster(secondIndex, secondIndexCluster);

		char dataIndexCluster[ClusterSize];
		unsigned long dataIndex = *(unsigned long*)(secondIndexCluster + entryNoSecondIndex(i));
		KernelFS::myPartition->readCluster(dataIndex, dataIndexCluster);

		unsigned long startInCluster = i % ClusterSize;
		unsigned long j = startInCluster;
		unsigned long end;

		if (limit - i > ClusterSize) //not final cluster to read
			end = ClusterSize;
		else
			end = limit - i + startInCluster;

		while (j < end) {
			buffer[position] = dataIndexCluster[j];
			position++;
			j++;
		}
	}

	cursor = limit;

	LeaveCriticalSection(&KernelFS::mutex);

	return ret;
}

char KernelFile::seek(BytesCnt cnt) {
	if (cnt > size)
		return 0;
	cursor = cnt;
	return 1;
}

BytesCnt KernelFile::filePos() {
	return cursor;
}

char KernelFile::eof() {
	return cursor == size;
}

BytesCnt KernelFile::getFileSize() {
	return size;
}

char KernelFile::truncate() {
	if (mode == 'r' || firstIndex == 0)
		return 0;

	vector<ClusterNo> clustersToFree;

	char firstIndexCluster[ClusterSize];
	KernelFS::myPartition->readCluster(firstIndex, firstIndexCluster);

	unsigned long entryPointInFirstIndex = entryNoFirstIndex(cursor);

	for (unsigned long i = entryPointInFirstIndex; i < ClusterSize; i += 4) { //prolazak kroz ceo klaster prvog nivoa od mesta gde je cursor
		unsigned long secondIndex = *(unsigned long*)(firstIndexCluster + i);

		if (secondIndex == 0)
			break;

		char secondIndexCluster[ClusterSize];
		KernelFS::myPartition->readCluster(secondIndex, secondIndexCluster);

		unsigned long j = 0;

		if (i == entryPointInFirstIndex) 
			j = entryNoSecondIndex(cursor);
		else 
			clustersToFree.push_back(secondIndex);

		for (; j < ClusterSize; j += 4) {
			unsigned long dataIndex = *(unsigned long*)(secondIndexCluster + j);
			if (dataIndex == 0)
				break;

			if (i = entryPointInFirstIndex) {
				char dataCluster[ClusterSize];
				KernelFS::myPartition->readCluster(dataIndex, dataCluster);
				
				for (unsigned long k = cursor % ClusterSize; k < ClusterSize; k++)
					dataCluster[k] = 0;


				KernelFS::myPartition->writeCluster(dataIndex, dataCluster);
			}
			else 
				clustersToFree.push_back(dataIndex);
		}

	}
	
	std::map<string, FCB>::iterator it = KernelFS::openFileTable->begin();
	it = KernelFS::openFileTable->find(searchName);
	it->second.fileSize = size = cursor;

	char dataCluster[ClusterSize];
	KernelFS::myPartition->readCluster(it->second.rootDataIndex, dataCluster);
	KernelFS::setFileSize(dataCluster + it->second.offsetInRootDataIndex, size);

	if (size == 0) {
		clustersToFree.push_back(firstIndex);
		it->second.fileFirstIndex = firstIndex = 0;
		KernelFS::setFileFirstIndex(dataCluster + it->second.offsetInRootDataIndex, firstIndex);
	}
	
	KernelFS::myPartition->writeCluster(it->second.rootDataIndex, dataCluster);

	if (clustersToFree.size() > 0)
		KernelFS::freeClusterInBitVector(clustersToFree);

	return 1;
}

unsigned long KernelFile::entryNoFirstIndex(unsigned long position) {
	return 4 * (position / ClusterSize / (ClusterSize / sizeof(ClusterNo)));
}
unsigned long KernelFile::entryNoSecondIndex(unsigned long position) {
	return 4 * (position / ClusterSize % (ClusterSize / sizeof(ClusterNo)));
}

bool KernelFile::enoughFreeClusters(unsigned long count) {
	EnterCriticalSection(&KernelFS::mutexAllocator);

	unsigned long free = 0;
	char bitVector[ClusterSize];
	KernelFS::myPartition->readCluster(0, bitVector);

	for (int i = 0; i < ceil(KernelFS::clusterCount / 8); i++) {
		if (bitVector[i] == 0)
			continue;
		for (int bit = 0; bit < 8; bit++)
			if (bitVector[i] & (0x80 >> bit)) { //0x80 1000 0000
				free++;
				if (free >= count) {
					LeaveCriticalSection(&KernelFS::mutexAllocator);
					return true;
				}
			}
	}

	LeaveCriticalSection(&KernelFS::mutexAllocator);
	return free >= count;
}