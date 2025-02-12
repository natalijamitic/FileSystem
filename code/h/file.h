#ifndef _FILE_H_
#define _FILE_H_

#include "fs.h"
#include "FCB.h"

class KernelFile;

class File {
public:
	~File(); //zatvaranje fajla
	char write(BytesCnt, char* buffer);
	BytesCnt read(BytesCnt, char* buffer);
	char seek(BytesCnt);
	BytesCnt filePos();
	char eof();
	BytesCnt getFileSize();
	char truncate();
private:
	friend class FS;
	friend class KernelFS;
	File(); //objekat fajla se mo�e kreirati samo otvaranjem
	File(string name, char mode, BytesCnt size, ClusterNo cluster, BytesCnt cursor);
	KernelFile* myImpl;
};
#endif