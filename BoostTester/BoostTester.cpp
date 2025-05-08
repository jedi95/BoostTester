// Test tool for finding maximum CPU boost clocks.
// This file is in the public domain.

#include <cstdlib>
#include <iostream>
#include <ctime>
#include <chrono>
#include <intrin.h>
#include "windows.h"
#include "CPUInfo.h"

using namespace std;

const unsigned int HALF_ARRAY = 0x1FFFFFF + 1;
const unsigned int ARRAY_SIZE = HALF_ARRAY * 2;

unsigned int* mem;

typedef BOOL(WINAPI* LPFN_GLPI)(
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION,
    PDWORD);

DWORD CountSetBits(ULONG_PTR bitMask)
{
    DWORD LSHIFT = sizeof(ULONG_PTR) * 8 - 1;
    DWORD bitSetCount = 0;
    ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;
    DWORD i;

    for (i = 0; i <= LSHIFT; ++i)
    {
        bitSetCount += ((bitMask & bitTest) ? 1 : 0);
        bitTest /= 2;
    }

    return bitSetCount;
}

char* getCpuidVendor(char* vendor) {
    int data[4];
    __cpuid(data, 0);
    *reinterpret_cast<int*>(vendor) = data[1];
    *reinterpret_cast<int*>(vendor + 4) = data[3];
    *reinterpret_cast<int*>(vendor + 8) = data[2];
    vendor[12] = 0;
    return vendor;
}

int getCpuidFamily() {
    int data[4];
    __cpuid(data, 1);
    int family = ((data[0] >> 8) & 0x0F);
    int extendedFamily = (data[0] >> 20) & 0xFF;
    int displayFamily = (family != 0x0F) ? family : (extendedFamily + family);
    return displayFamily;
}

CPUInfo getCPUInfo()
{
    LPFN_GLPI glpi;
    BOOL done = FALSE;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;
    DWORD returnLength = 0;
    DWORD byteOffset = 0;
    PCACHE_DESCRIPTOR Cache;
    CPUInfo info;

    info.cpuidFamily = getCpuidFamily();
    getCpuidVendor(info.vendor);

    glpi = (LPFN_GLPI)GetProcAddress(GetModuleHandle(TEXT("kernel32")), "GetLogicalProcessorInformation");
    if (NULL == glpi)
    {
        cout << "GetLogicalProcessorInformation is not supported";
        return info;
    }

    while (!done)
    {
        DWORD rc = glpi(buffer, &returnLength);
        if (FALSE == rc)
        {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            {
                if (buffer)
                {
                    free(buffer);
                }

                buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(returnLength);

                if (NULL == buffer)
                {
                    cout << "Error: Allocation failure";
                    return info;
                }
            }
            else
            {
                cout << "Error: " << GetLastError();
                return info;
            }
        }
        else
        {
            done = TRUE;
        }
    }

    ptr = buffer;

    while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength)
    {
        switch (ptr->Relationship)
        {
        case RelationNumaNode:
            // Non-NUMA systems report a single record of this type.
            info.numaNodeCount++;
            break;

        case RelationProcessorCore:
            info.physicalCoreCount++;
            info.logicalCoreCount += CountSetBits(ptr->ProcessorMask);
            break;

        case RelationCache:
            Cache = &ptr->Cache;
            if (Cache->Level == 1)
            {
                if (Cache->Type == CacheData) {
                    info.L1CacheCount++;
                }
            }
            else if (Cache->Level == 2)
            {
                info.L2CacheCount++;
            }
            else if (Cache->Level == 3)
            {
                info.L3CacheCount++;
            }
            break;

        case RelationProcessorPackage:
            info.packageCount++;
            break;

        default:
            break;
        }
        byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        ptr++;
    }
    free(buffer);
    return info;
}

//The goal of this function is to create a "100%" load at extremely low IPC.
//The best way I can think of to do this is by constantly stalling waiting for data from RAM.
//Added modifications from mann1x to the code
//https://github.com/mann1x/BoostTesterMannix/tree/master
int runTest(int core) {
	//Setup
	SetThreadAffinityMask(GetCurrentThread(), (static_cast<DWORD_PTR>(1) << core));

	//Randomly jump through the array
	//This will alternate between the high and low half
	//This is certain to run to completion because no element can contain the index for itself.
	//This process should defeat branch predictors and prefetches 
	//and result in needing data from RAM on every loop iteration.
	unsigned int value = mem[0];
    unsigned int qvalue = mem[0];

    for (int n = 0; n < 100; n++)
    {
        for (int i = 0; i < ARRAY_SIZE / 8192; i++)
        {
            //Set value equal to the value stored at an array index
            value = mem[value];
        }
        Sleep(50);
    }

    for (int i = 0; i < ARRAY_SIZE; i++)
    {
        //Set value equal to the value stored at an array index
        value = mem[value];
    }

	//Return final value to prevent loop from being optimized out
	return value;
}

int main(int argc, char* argv[])
{
    int repeatCoreNumberOfTimes = 1;


    //Command line arguments
    if ((argc == 2) && (_stricmp(argv[1], "/?") == 0 || _stricmp(argv[1], "/h") == 0 || _stricmp(argv[1], "/help") == 0 || _stricmp(argv[1], "--help") == 0)) {
        cout << "You can use the --core-repeat <n> command to increase the time a core is tested" << endl;
        cout << "For example with \"--core-repeat 3\" it will spend three times as long on each core" << endl;
        exit(0);
    }
    else if ((argc == 3) && (_stricmp(argv[1], "--core-repeat") == 0)) {
        repeatCoreNumberOfTimes = max(atoi(argv[2]), 1); // Needs to be at least 1
        cout << "Repeating a core " << repeatCoreNumberOfTimes << " times" << endl << endl;
    }


    //Print info
	cout << "CPU Max boost tester" << endl;
	unsigned int memsize = ARRAY_SIZE / 256 / 1024;


	//One time setup
	mem = new unsigned int[ARRAY_SIZE];


    //Threads per core needs to be an array, since for Intel 13th and 14th gen, it could be either a P- or an E-Core
    //The E-Cores only have one thread, while the P-Cores have two
    CPUInfo info = getCPUInfo();
    int threadsPerCore = info.getThreadsPerCore();
    int physicalCoreCount = info.physicalCoreCount;
    int logicalCoreCount = info.logicalCoreCount;
    bool isHyperThreadingEnabled = info.isHyperThreadingEnabled();
    bool hasAsymmetricalCoreThreads = info.hasAsymmetricalCoreThreads();
    int* threadsPerCoreArray = new int[physicalCoreCount];
    int numCoresWithoutHyperThreading;
    int numCoresWithHyperThreading;

    cout << "CPU Vendor: " << info.vendor << endl;
    cout << "Physical cores found:   " << physicalCoreCount << endl;
    cout << "Logical cores found:    " << logicalCoreCount << endl;
    cout << "Hyperthreading enabled: " << isHyperThreadingEnabled << endl;

    //We assume that the cores with two threads are at the beginning of the index. Not mixed and not at the end
    if (hasAsymmetricalCoreThreads) {
        cout << "This CPU has an asymmetrical core layout" << endl;

        int numTheoreticalLogicalCores = physicalCoreCount * 2;
        numCoresWithoutHyperThreading = numTheoreticalLogicalCores - logicalCoreCount;
        numCoresWithHyperThreading = physicalCoreCount - numCoresWithoutHyperThreading;

        cout << "Cores with two threads: " << numCoresWithHyperThreading << endl;
        cout << "Cores with one thread:  " << numCoresWithoutHyperThreading << endl;

        if (numCoresWithoutHyperThreading > 0) {
            //Again, we assume that the cores with two threads only appear in the beginning
            for (int core = 0; core < physicalCoreCount; core++) {
                threadsPerCoreArray[core] = (core < numCoresWithHyperThreading) ? 2 : 1;
            }
        }
    }

    //If it's a "normal" CPU, just use 1 or 2 threads per core
    else {
        for (int core = 0; core < physicalCoreCount; core++) {
            threadsPerCoreArray[core] = (isHyperThreadingEnabled) ? 2 : 1;
        }
    }


    cout << "Memory required: " << memsize << " MB" << endl;

	//Populate memory array
	cout << "Filling memory array" << endl;
	for (unsigned int i = 0; i < HALF_ARRAY; i++)
	{
		//Fill low half of the array with values from the high half
		mem[i] = i + HALF_ARRAY;

		//Fill high half of the array with values for the low half
		mem[i + HALF_ARRAY] = i;
	}

	//Now we shuffle the high and low part of the array.
    //Doing it this way ensures that no element contains the index for itself
	cout << "Performing array shuffle (low)" << endl;
	for (unsigned int i = 0; i < HALF_ARRAY; i++) {
		int r = rand() % HALF_ARRAY;
		unsigned int temp = mem[i];
		mem[i] = mem[r];
		mem[r] = temp;
	}

	cout << "Performing array shuffle (high)" << endl;
	for (unsigned int i = HALF_ARRAY; i < ARRAY_SIZE; i++) {
		int r = (rand() % HALF_ARRAY) + HALF_ARRAY;
		unsigned int temp = mem[i];
		mem[i] = mem[r];
		mem[r] = temp;
	}

    cout << endl;

	//This value has no actual meaning, but is required to avoid runTest() being optimized out by the compiler
	unsigned long counter = 0;
	//This condition will never be false. Tricking the compiler....
	while (counter < 0xFFFFFFFFF) {
        for (int core = 0; core < physicalCoreCount; core++) {
            //We don't run on a "core", we run on a (possibly virtual) CPU
            int cpuValue = core * threadsPerCoreArray[core];
            
            //If we have reached the cores with only one thread
            if (hasAsymmetricalCoreThreads && core > numCoresWithHyperThreading - 1) {
                cpuValue = (numCoresWithHyperThreading * 2) - 1 + (core - (numCoresWithHyperThreading-1));
            }

            auto startTime = chrono::system_clock::now();

            cout << "Running on core: " << core << endl;
            for (int i = 0; i < repeatCoreNumberOfTimes; i++) {
                counter = runTest(cpuValue);

                auto currentEndTime = chrono::system_clock::now();
                chrono::duration<double> runTime = currentEndTime - startTime;
                cout << "...elapsed time: " << runTime.count() << "s" << endl;
                
                //time_t currentEndDateTime = chrono::system_clock::to_time_t(currentEndTime);
                //cout << "finished computation at " << ctime(&currentEndDateTime) << endl;
            }

            //Sleep for a bit to allow the CPU to cool down
            cout << "Waiting 3 seconds..." << endl;
            Sleep(3000);
        }
	}

	//Have to use the return from runTest() somewhere or it gets optimized out.
	return counter;
}