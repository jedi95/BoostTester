#pragma once
class CPUInfo
{
public:
	int numaNodeCount = 0;
	int packageCount = 0;
	int physicalCoreCount = 0;
	int logicalCoreCount = 0;
	int L1CacheCount = 0;
	int L2CacheCount = 0;
	int L3CacheCount = 0;
	char vendor[13];
	int cpuidFamily;
	double ticksPerNanosecond;
	int getThreadsPerCore() { return logicalCoreCount / physicalCoreCount; }
	int getCoresPerNode() { return physicalCoreCount / numaNodeCount; }
	int getCoresPerPackage() { return physicalCoreCount / packageCount; }
	int getCoresPerL3() { return physicalCoreCount / L3CacheCount; }
	int getCoresPerL2() { return physicalCoreCount / L2CacheCount; }
	int getL3PerPackage() { return L3CacheCount / packageCount; }
	int getL3PerNUMANode() { return L3CacheCount / numaNodeCount; }
};
