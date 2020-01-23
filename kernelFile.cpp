#include "kernelFile.h"
#include "kernelFS.h"

KernelFile::~KernelFile() {
	EnterCriticalSection(&KernelFS::mutex);
	
	string searchName = KernelFS::getFileNameFromPath(name) + KernelFS::getFileExtFromPath(name);
	std::map<string, FCB>::iterator it = KernelFS::openFileTable->begin;
	it = KernelFS::openFileTable->find(searchName);
	it->second.numOfRefs--;
	if (it->second.numOfRefs == 0) {
		it->second.numOfRefs = 0;
		signal(it->second.semFile);
		KernelFS::openFileTable->erase(it);
	}

	if (KernelFS::getCountOfOpenFiles() == 0)
		signal(KernelFS::semFilesClosed);

	LeaveCriticalSection(&KernelFS::mutex);
}

BytesCnt KernelFile::read(BytesCnt size, char* buffer) {
	if (size || buffer || eof)
		return 0;

	BytesCnt limit = (size + cursor < this->size) ? (size + cursor) : this->size;
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

		while (true) {
			unsigned long end;
			if (limit - i > ClusterSize)
				end = ClusterSize;
			else
				end = limit - i + startInCluster;

			if (j >= end)
				break;

			buffer[position++] = dataIndexCluster[j];
		}
	}

	cursor = limit;
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

