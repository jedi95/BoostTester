// Test tool for finding maximum CPU boost clocks.
// This file is in the public domain.

#include <cstdlib>
#include <iostream>
#include "windows.h"

using namespace std;

const unsigned int HALF_ARRAY = 0x1FFFFFF + 1;
const unsigned int ARRAY_SIZE = HALF_ARRAY * 2;

unsigned int* mem;

//The goal of this function is to create a "100%" load at extremely low IPC.
//The best way I can think of to do this is by constantly stalling waiting for data from RAM.
int runTest(int core) {
	//Setup
	cout << "Running on core: " << (core / 2) << endl;
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

	//Get core count
	//Hardcoded to assume SMT2. Too lazy to look up how to grab the physical core count.
	//This works for any desktop Zen 2 except the 3500X anyway.
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	int numCPU = sysinfo.dwNumberOfProcessors;

	//This value has no actual meaning, but is required to avoid runTest() being optimized out by the compiler
	unsigned long counter = 0;
	//This condition will never be false. Tricking the compiler....
	while (counter < 0xFFFFFFFFF) {
		for (int i = 0; i < numCPU; i+=2) {
			counter = runTest(i);
		}
	}

	//Have to use the return from runTest() somewhere or it gets optimized out.
	return counter;
}