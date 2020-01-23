#include "criticalSectionInit.h"

#include "kernelFS.h"

CriticalSectionInit::CriticalSectionInit() {
	InitializeCriticalSection(&KernelFS::mutex);
}