// Test tool for finding maximum CPU boost clocks.
// This file is in the public domain.

#include <cstdlib>
#include <iostream>
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
int runTest(int core) {
	//Setup
	SetThreadAffinityMask(GetCurrentThread(), (static_cast<DWORD_PTR>(1) << core));

	//Randomly jump through the array
	//This will alternate between the high and low half
	//This is certain to run to completion because no element can contain the index for itself.
	//This process should defeat branch predictors and prefetches 
	//and result in needing data from RAM on every loop iteration.
	unsigned int value = mem[0];
	for (int i = 0; i < ARRAY_SIZE; i++)
	{
		//Set value equal to the value stored at an array index
		value = mem[value];
	}

	//Return final value to prevent loop from being optimized out
	return value;
}

int main()
{
	//Print info
	cout << "CPU Max boost tester" << endl;
	unsigned int memsize = ARRAY_SIZE / 256 / 1024;
	cout << "Memory required: " << memsize << " MB" << endl;

	//One time setup
	mem = new unsigned int[ARRAY_SIZE];

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

    CPUInfo info = getCPUInfo();
    int threadsPerCore = info.getThreadsPerCore();

	//This value has no actual meaning, but is required to avoid runTest() being optimized out by the compiler
	unsigned long counter = 0;
	//This condition will never be false. Tricking the compiler....
	while (counter < 0xFFFFFFFFF) {
		for (int i = 0; i < info.logicalCoreCount; i+=threadsPerCore) {
            cout << "Running on core: " << (i / threadsPerCore) << endl;
			counter = runTest(i);
		}
	}

	//Have to use the return from runTest() somewhere or it gets optimized out.
	return counter;
}