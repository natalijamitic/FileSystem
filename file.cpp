#include "file.h"
#include "kernelFile.h"

File::~File() {
	delete myImpl;
}

char File::write(BytesCnt cnt, char* buffer) {
	//return myImpl->write(cnt, buffer);
	return 0;
}

BytesCnt File::read(BytesCnt cnt, char* buffer) {
	//return myImpl->read(cnt, buffer);
	return 0;

}

char File::seek(BytesCnt cnt) {
	return myImpl->seek(cnt);
}

BytesCnt File::filePos() {
	return myImpl->filePos();
}

char File::eof() {
	return myImpl->eof();
}

BytesCnt File::getFileSize() {
	return myImpl->getFileSize();
}

char File::truncate() {
	//return myImpl->truncate();
	return 0;
}

File::File(string name, char mode, BytesCnt size, ClusterNo cluster, BytesCnt cursor){
	myImpl = new KernelFile(name, mode, size, cluster, cursor);
}


