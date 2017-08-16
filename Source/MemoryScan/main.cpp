#include "utils.h"
#include <memory>
#include <vector>
#include <algorithm>
#include <assert.h>
#include <windows.h>
#include "..\..\ThirdParty\pugixml-1.8\src\pugixml.hpp"


#define RANDOM_PATTERN_LEN (32)

#define DEG_TO_RAD (0.01745329251f)
#define RAD_TO_DEG (57.2957795131f)


#define ROTATION_TYPE_UNKNOWN (0) // 00b
#define ROTATION_TYPE_YAW (1)     // 01b
#define ROTATION_TYPE_PITCH (2)   // 10b
#define ROTATION_TYPE_BOTH (3)    // 11b

struct RotationComponentSuspect
{
	uintptr_t addr;
	float val;
	float val_min;
	float val_max;
	uint32_t type;
	bool isInvalid;

	RotationComponentSuspect()
	{
		addr = 0;
		type = ROTATION_TYPE_UNKNOWN;
		isInvalid = false;
	}

	RotationComponentSuspect(uintptr_t _addr)
	{
		addr = _addr;
		val = 0.0f;
		val_min = FLT_MAX;
		val_max = -FLT_MAX;
		type = ROTATION_TYPE_UNKNOWN;
		isInvalid = false;
	}

	bool ClassifyRotation()
	{
		if ((val_min >= -180.0f && val_min < -160.0f) &&
			(val_max > 160.0f && val_max <= 180.0f))
		{
			return true;
		}

		if ((val_min >= 0.0f && val_min < 20.0f) &&
			(val_max > 340.0f && val_max <= 360.0f))
		{
			return true;
		}

		if ((val_min >= -180.0f * DEG_TO_RAD && val_min < -160.0f * DEG_TO_RAD) &&
			(val_max > 160.0f * DEG_TO_RAD && val_max <= 180.0f * DEG_TO_RAD))
		{
			return true;
		}

		if ((val_min >= 0.0f * DEG_TO_RAD && val_min < 20.0f * DEG_TO_RAD) &&
			(val_max > 340.0f * DEG_TO_RAD && val_max <= 360.0f * DEG_TO_RAD))
		{
			return true;
		}

		if ((val_min >= -90.0f && val_min < -60.0f) &&
			(val_max > 60.0f && val_max <= 90.0f))
		{
			return true;
		}

		if ((val_min >= 0.0f && val_min < 30.0f) &&
			(val_max > 150.0f && val_max <= 180.0f))
		{
			return true;
		}

		if ((val_min >= -90.0f * DEG_TO_RAD && val_min < -60.0f * DEG_TO_RAD) &&
			(val_max > 60.0f * DEG_TO_RAD && val_max <= 90.0f * DEG_TO_RAD))
		{
			return true;
		}

/*
		if ((val_min >= -1.0f && val_min < -0.95f) &&
			(val_max > 0.95f && val_max <= 1.0f))
		{
			return true;
		}
*/

		isInvalid = true;
		return false;
	}
};


struct ContiguousMemoryLayout
{
	const Process::MemoryPage* page;
	size_t contiguousOffset;
	size_t pgReadOffset;
	size_t pgReadSize;
	size_t pgValidSize;
};


std::pair<size_t, size_t> FindPageByOffset(const Process* process, size_t offset)
{
	size_t pagesCount = process->GetPagesCount();

	if (pagesCount == 0)
	{
		return std::pair<size_t, size_t>(SIZE_MAX, SIZE_MAX);
	}

	size_t currentOffset = 0;
	for (size_t pgIndex = 0; pgIndex < pagesCount; pgIndex++)
	{
		const Process::MemoryPage& page = process->GetPageByIndex(pgIndex);
		if (offset >= currentOffset && offset < currentOffset + page.size)
		{
			size_t offsetInsidePage = (offset - currentOffset);
			return std::pair<size_t, size_t>(pgIndex, offsetInsidePage);
		}

		currentOffset += page.size;
	}

	size_t lastPgIndex = pagesCount - 1;
	const Process::MemoryPage& lastPage = process->GetPageByIndex(lastPgIndex);
	return std::pair<size_t, size_t>(lastPgIndex, lastPage.size);
}


std::vector<ContiguousMemoryLayout> ReadProcessMemoryAsContiguousMemoryBlock(const Process* process, uint8_t* pDestShadowBuffer, size_t offset, size_t count)
{
	std::vector<ContiguousMemoryLayout> r;

	std::pair<size_t, size_t> pageFrom = FindPageByOffset(process, offset);
	std::pair<size_t, size_t> pageTo = FindPageByOffset(process, offset + count);

	size_t pagesCount = process->GetPagesCount();

	assert(pageFrom.first < pagesCount && "Bad logic!");
	assert(pageTo.first < pagesCount && "Bad logic!");
	if (pageFrom.first >= pagesCount || pageTo.first >= pagesCount)
	{
		printf("Something went wrong!\n");
		return r;
	}

	size_t rdPagesCount = pageTo.first - pageFrom.first + 1;
	r.reserve(rdPagesCount);

	size_t shadowBufferOffset = 0;
	for (size_t pgIndex = pageFrom.first; pgIndex <= pageTo.first; pgIndex++)
	{
		const Process::MemoryPage& page = process->GetPageByIndex(pgIndex);

		size_t pgOffset = 0;
		size_t pgSize = page.size;

		if (pgIndex == pageFrom.first)
		{
			pgOffset = pageFrom.second;
			pgSize = (page.size - pgOffset);
		}

		if (pgIndex == pageTo.first)
		{
			pgSize = (pageTo.second - pgOffset);
		}

		size_t readedBytes = process->ReadMemory((pDestShadowBuffer + shadowBufferOffset), page.baseAddr + pgOffset, pgSize);

		assert((pgSize & 3) == 0 && "pgSize must be multiple of 4=sizeof(float)");
		assert((offset & 3) == 0 && "offsetStart must be multiple of 4=sizeof(float)");

		size_t firstBadIndex = (pgOffset + readedBytes) / sizeof(float);
		size_t lastBadIndex = (pgOffset + pgSize) / sizeof(float);
		for (size_t i = firstBadIndex; i < lastBadIndex; i++)
		{
			page.suspectsMask->disable(i);
		}


		ContiguousMemoryLayout readedBlock;
		readedBlock.page = &page;
		readedBlock.contiguousOffset = shadowBufferOffset;
		readedBlock.pgReadOffset = pgOffset;
		readedBlock.pgReadSize = pgSize;
		readedBlock.pgValidSize = readedBytes;
		r.push_back(readedBlock);

		shadowBufferOffset += pgSize;
	}

	return r;
}



int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		printf("Usage: BroadPhase.exe pid broad_results.xml\n");
		return -1;
	}


	uint32_t pid = atoi(argv[1]);
	const char* resultXml = argv[2];
	printf("Process id : %d\n", pid);
	printf("Result XML : %s\n", resultXml);
	std::unique_ptr<Process> process(Process::Create());
	if (!process->Open(pid))
	{
		printf("ERROR: Can't open process, pid %d\n", pid);
		return -2;
	}

	const char* exePath = process->GetExecutablePath();
	printf("Path : %s\n", exePath);

	size_t exeBaseAddr = process->GetExecutableBase();
	printf("Base addr : 0x%zX\n", exeBaseAddr);

	size_t readableBytesCount = process->GetReadableBytesCount();
	printf("Memory bytes : %3.2f Mb\n", (double)readableBytesCount / 1024.0 / 1024.0);
	if (readableBytesCount < 1)
	{
		printf("ERROR: No readable memory regions found!\n");
		return -77;
	}

	size_t shadowMemoryBytesCount = 768 * 1024 * 1024; //768Mb of shadow memory
	//shadowMemoryBytesCount = 512;

	assert((shadowMemoryBytesCount & 15) == 0 && "Must be multiple of 16=sizeof(__m128i)");
	

	uint8_t* buffers[2];
	buffers[0] = (uint8_t*)VirtualAlloc(NULL, shadowMemoryBytesCount, MEM_COMMIT, PAGE_READWRITE);
	buffers[1] = (uint8_t*)VirtualAlloc(NULL, shadowMemoryBytesCount, MEM_COMMIT, PAGE_READWRITE);

	if (buffers[0] == nullptr || buffers[1] == nullptr)
	{
		printf("ERROR: Can't allocate shadow memory, %3.2fMb bytes\n", (double)shadowMemoryBytesCount * 2.0 / 1024.0 / 1024.0);
		return -3;
	}

	size_t stepsCount = readableBytesCount / shadowMemoryBytesCount;
	size_t tailSize = readableBytesCount - (stepsCount * shadowMemoryBytesCount);
	if (tailSize > 0)
	{
		stepsCount++;
	}


	if (!utils::ActivateProcessMainWindow(process.get()))
	{
		printf("ERROR: Can't activate process main window\n");
		return -6;
	}

	printf("Prepare your game\n");
	Sleep(3000);
	printf("Scan memory. Do not TOUCH ANY INPUT!\n");


	const int firstPassYawSpeed = 3;
	const int firstPassPitchSpeed = 3;
	const int mouseEventsCount = 12;

	std::vector<RotationComponentSuspect> yawSuspects;
	yawSuspects.reserve(2 * 1024 * 1024);

	std::vector<RotationComponentSuspect> pitchSuspects;
	pitchSuspects.reserve(2 * 1024 * 1024);

	//Constant removal pass
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	for (int rotType = 0; rotType < 2; rotType++)
	{

		//mark all as suspects
		size_t pgCount = process->GetPagesCount();
		for (size_t pgIndex = 0; pgIndex < pgCount; pgIndex++)
		{
			process->GetPageByIndex(pgIndex).suspectsMask->clearToOne();
		}

		//0 = yaw
		//1 = pitch

		int yawDelta = 0;
		int pitchDelta = 0;
		if (rotType == 0)
		{
			yawDelta = firstPassYawSpeed;
			printf("Mode: yaw\n");
		} else
		{
			pitchDelta = firstPassPitchSpeed;
			printf("Mode: pitch\n");
		}


		//read whole memory...
		size_t bytesProcessed = 0;
		size_t totalCount = 0;
		size_t removedConstCount = 0;
		size_t blockStart = 0;
		for (size_t step = 0; step < stepsCount; step++)
		{
			size_t blockEnd = min(blockStart + shadowMemoryBytesCount, readableBytesCount);
			size_t blockSize = blockEnd - blockStart;

			uint8_t* shadowBuffer = buffers[0];
			std::vector<ContiguousMemoryLayout> blocksPrev = ReadProcessMemoryAsContiguousMemoryBlock(process.get(), shadowBuffer, blockStart, blockSize);

			// send multiple mouse events (some games sometimes misses some events)
			for (int i = 0; i < mouseEventsCount; i++)
			{
				utils::MoveMouse(yawDelta, pitchDelta);
			}

			//wait until game response mouse events and update internal state
			Sleep(400);

			uint8_t* currentBuffer = buffers[1];
			std::vector<ContiguousMemoryLayout> blocksNow = ReadProcessMemoryAsContiguousMemoryBlock(process.get(), currentBuffer, blockStart, blockSize);

			assert(blocksNow.size() == blocksPrev.size() && "Something went wrong!");

			for (const ContiguousMemoryLayout & block : blocksNow)
			{
				printf("%3.2f of %3.2f Mb                     \r", (double)bytesProcessed / 1024.0 / 1024.0, (double)readableBytesCount / 1024.0 / 1024.0);

				size_t firstValidIndex = (block.pgReadOffset) / sizeof(float);
				size_t lastValidIndex = (block.pgReadOffset + block.pgValidSize) / sizeof(float);

				uint32_t* prev = (uint32_t*)(shadowBuffer + block.contiguousOffset);
				uint32_t* now = (uint32_t*)(currentBuffer + block.contiguousOffset);

				for (size_t vIndex = firstValidIndex; vIndex < lastValidIndex; vIndex++, now++, prev++)
				{
						if (*prev == *now)
						{
							block.page->suspectsMask->disable(vIndex);
							removedConstCount++;
						}

						totalCount++;
				}

				bytesProcessed += block.pgReadSize;
			}
			blockStart += blockSize;
		}

		printf("%3.2f of %3.2f Mb                     \n", (double)bytesProcessed / 1024.0 / 1024.0, (double)readableBytesCount / 1024.0 / 1024.0);

		printf("Removed %zd bytes, %3.2f %%\n", removedConstCount*4, ((double)removedConstCount / (double)totalCount) * 100.0f);

		//generate suspects list for each mode
		std::vector<RotationComponentSuspect> & res = ((rotType == 0) ? yawSuspects : pitchSuspects);

		printf("Analyze\n");


		__m128i zero = _mm_setzero_si128();

		size_t pagesCount = process->GetPagesCount();
		for (size_t pgIndex = 0; pgIndex < pagesCount; pgIndex++)
		{
			const Process::MemoryPage& page = process->GetPageByIndex(pgIndex);

			const bit_array& suspectsMask = *page.suspectsMask;

			const __m128i* pBits = (const __m128i*)suspectsMask.getData();

			size_t suspectsCount = suspectsMask.size();
			size_t stepsCount = suspectsCount / 128;
			size_t tailSize = suspectsCount - (stepsCount * 128);

			size_t bitIndexStart = 0;
			size_t offset = 0;
			for (size_t s = 0; s < stepsCount; s++, pBits++, bitIndexStart += 128)
			{
				//check 128 bits at once
				__m128i bits = _mm_load_si128(pBits);
				if (_mm_movemask_epi8(_mm_cmpeq_epi32(bits, zero)) == 0xFFFF)
				{
					offset += sizeof(float) * 128;
					continue;
				}

				//precise check for every bit
				for (size_t bitIndex = bitIndexStart; bitIndex < (bitIndexStart + 128); bitIndex++)
				{
					if (suspectsMask.get(bitIndex))
					{
						res.push_back(RotationComponentSuspect(page.baseAddr + offset));
					}
					offset += sizeof(float);
				}

			}

		}



		


	} //rot type


	printf("Yaw suspects count: %zd\n", yawSuspects.size());
	printf("Pitch suspects count: %zd\n", pitchSuspects.size());

	std::sort(yawSuspects.begin(), yawSuspects.end(), [](const RotationComponentSuspect & a, const RotationComponentSuspect & b) {
		return a.addr < b.addr;
	});

	std::sort(pitchSuspects.begin(), pitchSuspects.end(), [](const RotationComponentSuspect & a, const RotationComponentSuspect & b) {
		return a.addr < b.addr;
	});

	//extract suspects addrs
	std::vector<RotationComponentSuspect> suspects;
	suspects.reserve(2 * 1024 * 1024);

	std::set_symmetric_difference(yawSuspects.begin(), yawSuspects.end(),
		                          pitchSuspects.begin(), pitchSuspects.end(),
		                          std::back_inserter(suspects),
		                          [](const RotationComponentSuspect & a, const RotationComponentSuspect & b) {
		return a.addr < b.addr;
	});

	printf("Suspects count: %zd\n", suspects.size());

	// Return mouse back to original position
	for (size_t i = 0; i < stepsCount; i++)
	{
		int yawDelta = -firstPassYawSpeed;
		int pitchDelta = -firstPassPitchSpeed;
		for (int i = 0; i < mouseEventsCount; i++)
		{
			utils::MoveMouse(yawDelta, pitchDelta);
		}
	}

	// Free memory
	VirtualFree(buffers[0], shadowMemoryBytesCount, MEM_DECOMMIT);
	buffers[0] = nullptr;
	VirtualFree(buffers[1], shadowMemoryBytesCount, MEM_DECOMMIT);
	buffers[1] = nullptr;

	Sleep(200);


	int yawSpeed = 40;
	int pitchSpeed = 30;

	int frameDelay = 16;
	printf("Frame delay : %d ms\n", frameDelay);


	for (int rotType = 0; rotType < 2; rotType++)
	{
		//0 = yaw
		//1 = pitch

		uint32_t typeMask = (rotType == 0) ? ROTATION_TYPE_YAW : ROTATION_TYPE_PITCH;

		//read initial suspect values (to determine type)
		for (size_t i = 0; i < suspects.size(); i++)
		{
			RotationComponentSuspect & suspect = suspects[i];
			if (suspect.isInvalid)
			{
				continue;
			}

			if (!process->ReadMemory(&suspect.val, suspect.addr, sizeof(float)))
			{
				suspect.isInvalid = true;
				continue;
			}
		}

		suspects.erase(std::remove_if(suspects.begin(), suspects.end(), [](const RotationComponentSuspect & suspect) {return suspect.isInvalid; }), suspects.end());


		for (int step = 0; step < 2; step++)
		{
			// step defines direction
			printf("Detect ranges (step #%d)\n", step);

			int maxIterationCount = (step == 0) ? 256 : 512;
			if (rotType == 1)
			{
				maxIterationCount = maxIterationCount / 2;
			}

			for (int iter = 0; iter < maxIterationCount; iter++)
			{
				if (!process->IsForegroundWindow())
				{
					printf("ERROR: Process main window is not foreground window!\n");
					return -7;
				}

				printf("step : %d / %d, count : %zd                        \r", (iter+1), maxIterationCount, suspects.size());

				int ySpeed = (rotType == 0) ? yawSpeed : 0;
				int pSpeed = (rotType == 0) ? 0 : pitchSpeed;

				int yawDelta = (step == 0) ? ySpeed : -ySpeed;
				int pitchDelta = (step == 0) ? pSpeed : -pSpeed;
				utils::MoveMouse(yawDelta, pitchDelta);
				Sleep(frameDelay);

				//check suspects
				for (size_t i = 0; i < suspects.size(); i++)
				{
					RotationComponentSuspect & suspect = suspects[i];
					if (suspect.isInvalid)
					{
						continue;
					}

					float val = 0.0f;
					if (!process->ReadMemory(&val, suspect.addr, sizeof(float)))
					{
						suspect.isInvalid = true;
						continue;
					}

					switch (std::fpclassify(val)) {
					case FP_INFINITE:
					case FP_NAN:
					case FP_SUBNORMAL:
						suspect.isInvalid = true;
						continue;
						//case FP_NORMAL:
						//case FP_ZERO:
					}

					if (val < -360.0f || val > 360.0f)
					{
						suspect.isInvalid = true;
						continue;
					}

					suspect.val_min = min(suspect.val_min, val);
					suspect.val_max = max(suspect.val_max, val);
					if (val != suspect.val)
					{
						suspect.type |= typeMask;
					}

				} // suspects
			}

			printf("\n");
			suspects.erase(std::remove_if(suspects.begin(), suspects.end(), [](const RotationComponentSuspect & suspect) {return suspect.isInvalid; }), suspects.end());
		}
	}


	//Classify suspects
	//////////////////////////////////////////////////////////////////////////////
	for (size_t i = 0; i < suspects.size(); i++)
	{
		RotationComponentSuspect & suspect = suspects[i];
		if (suspect.isInvalid)
		{
			continue;
		}
		suspect.ClassifyRotation();
	}

	suspects.erase(std::remove_if(suspects.begin(), suspects.end(), [](const RotationComponentSuspect & suspect) {return suspect.isInvalid; }), suspects.end());


	//Final check 
	//////////////////////////////////////////////////////////////////////////////

	printf("Validation....\n");

	for (int validationPass = 0; validationPass < 8; validationPass++)
	{
		int speed = 30;
		int mouseDeltas[RANDOM_PATTERN_LEN];
		utils::GenSymmetricDeltas(mouseDeltas, RANDOM_PATTERN_LEN, speed, -1024, 1024);

		//read current values and seta all suspects to false
		for (size_t i = 0; i < suspects.size(); i++)
		{
			RotationComponentSuspect & suspect = suspects[i];
			process->ReadMemory(&suspect.val, suspect.addr, sizeof(float));
			suspect.isInvalid = true;
		}

		for (int i = 0; i < RANDOM_PATTERN_LEN; i++)
		{
			printf("%d of %d                      \r", i + 1, RANDOM_PATTERN_LEN);

			int dt = mouseDeltas[i];

			utils::MoveMouse(dt, dt);
			Sleep(frameDelay);

			for (size_t i = 0; i < suspects.size(); i++)
			{
				RotationComponentSuspect & suspect = suspects[i];
				float val = 0.0f;
				process->ReadMemory(&val, suspect.addr, sizeof(float));
				if (val != suspect.val)
				{
					suspect.isInvalid = false;
				}
			}
		}

		printf("\n");

		Sleep(1000);

		suspects.erase(std::remove_if(suspects.begin(), suspects.end(), [](const RotationComponentSuspect & suspect) {return suspect.isInvalid; }), suspects.end());

		for (size_t i = 0; i < suspects.size(); i++)
		{
			RotationComponentSuspect & suspect = suspects[i];
			process->ReadMemory(&suspect.val, suspect.addr, sizeof(float));
		}

		const int maxStepsCount = 512;
		for (int i = 0; i < maxStepsCount; i++)
		{
			printf("%d of %d                      \r", i + 1, maxStepsCount);

			for (size_t i = 0; i < suspects.size(); i++)
			{
				RotationComponentSuspect & suspect = suspects[i];
				float val = 0.0f;
				process->ReadMemory(&val, suspect.addr, sizeof(float));
				if (fabs(val - suspect.val) > 0.0001f)
				{
					suspect.isInvalid = true;
				}
			}
		}

		printf("\n");

		suspects.erase(std::remove_if(suspects.begin(), suspects.end(), [](const RotationComponentSuspect & suspect) {return suspect.isInvalid; }), suspects.end());
	}


	std::sort(suspects.begin(), suspects.end(), [](const RotationComponentSuspect & a, const RotationComponentSuspect & b) {
		return a.addr < b.addr;
	});


	printf("Final Suspects count: %zd\n", suspects.size());

	{
		pugi::xml_document doc;

		pugi::xml_node processNode = doc.append_child("process");
		pugi::xml_attribute pidAttr = processNode.append_attribute("pid");
		pidAttr.set_value(pid);
		pugi::xml_attribute pathAttr = processNode.append_attribute("path");
		pathAttr.set_value(exePath);
		pugi::xml_attribute baseAttr = processNode.append_attribute("base");
		baseAttr.set_value(exeBaseAddr);

		pugi::xml_node values = doc.append_child("suspects");
		for (size_t i = 0; i < suspects.size(); i++)
		{
			const RotationComponentSuspect & candidate = suspects[i];

			pugi::xml_node item = values.append_child("item");
			pugi::xml_attribute addrAttr = item.append_attribute("addr");
			addrAttr.set_value(candidate.addr);

			pugi::xml_attribute valAttr = item.append_attribute("val");
			valAttr.set_value(candidate.val);

			pugi::xml_attribute minAttr = item.append_attribute("min");
			minAttr.set_value(candidate.val_min);

			pugi::xml_attribute maxAttr = item.append_attribute("max");
			maxAttr.set_value(candidate.val_max);

			pugi::xml_attribute typeAttr = item.append_attribute("type");
			typeAttr.set_value(candidate.type);
		}

		doc.save_file(resultXml);
	}
	printf("DONE. File saved.\n");
	return 0;
}