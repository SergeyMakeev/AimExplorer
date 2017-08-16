#pragma once
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <memory>
#include <xmmintrin.h>

class Process;

namespace utils
{
	bool ActivateProcessMainWindow(Process* process);

	void MoveMouse(int dx, int dy);

	bool GenRandom(uint8_t* buffer, uint32_t bufferSize);

	bool GenSymmetricDeltas(int* deltasBuffer, uint32_t bufferSize, int val, int val_min, int val_max);

	void DebugMessage(const char* msg, ...);
}


struct ScanMode
{
	enum Type
	{
		SCAN_FOR_CHANGES = 0,
		SCAN_FOR_CONSTANTS = 1,
	};
};

class bit_array
{
	uint8_t* data;
	size_t bytesCount;

public:

	bit_array(size_t bitsCount)
	{
		assert((bitsCount & 3) == 0 && "Bits count must be multiple of 8");
		bytesCount = bitsCount / 8;
		data = (uint8_t*)_mm_malloc(bytesCount, 16);
		memset(data, 0xFF, bytesCount);
	}

	~bit_array()
	{
		_mm_free(data);
		data = nullptr;
	}

	size_t size() const
	{
		return bytesCount * 8;
	}

	void clearToZero()
	{
		memset(data, 0x0, bytesCount);
	}

	void clearToOne()
	{
		memset(data, 0xFF, bytesCount);
	}

	void enable(size_t index)
	{
		size_t byteOffset = index / 8;
		size_t bitIndex = index - (byteOffset * 8);
		size_t mask = (uint8_t)(1 << bitIndex);
		data[byteOffset] |= mask;
	}

	void disable(size_t index)
	{
		size_t byteOffset = index / 8;
		size_t bitIndex = index - (byteOffset * 8);
		size_t mask = (uint8_t)(1 << bitIndex);
		data[byteOffset] &= ~mask;
	}

	bool get(size_t index) const
	{
		size_t byteOffset = index / 8;
		size_t bitIndex = index - (byteOffset * 8);
		size_t mask = (uint8_t)(1 << bitIndex);
		return ((data[byteOffset] & mask) != 0);
	}

	const uint8_t* getData() const
	{
		return data;
	}

};
class Process
{

public:

	struct MemoryPage
	{
		uintptr_t baseAddr;
		size_t size;
		std::unique_ptr<bit_array> suspectsMask;

		MemoryPage()
		{
			baseAddr = 0;
			size = 0;
			suspectsMask = nullptr;
		}

		MemoryPage(MemoryPage && other)
		{
			std::swap(baseAddr, other.baseAddr);
			std::swap(size, other.size);
			std::swap(suspectsMask, other.suspectsMask);

		}
	};

	Process() {};
	virtual ~Process() {};
	virtual bool Open(uint32_t _pid) = 0;

	virtual size_t GetLargestMemoryPageSizeInBytes() const = 0;
	virtual bool SetAsForegroundWindow() const = 0;
	virtual bool IsForegroundWindow() const = 0;

	virtual size_t ReadMemory(void* pBuffer, uintptr_t baseAddr, size_t size) const = 0;

	virtual size_t GetPagesCount() const = 0;
	virtual const MemoryPage& GetPageByIndex(size_t index) const = 0;

	virtual const char* GetExecutablePath() const = 0;
	virtual size_t GetExecutableBase() const = 0;
	virtual size_t GetReadableBytesCount() const = 0;

	static Process* Create();
};


