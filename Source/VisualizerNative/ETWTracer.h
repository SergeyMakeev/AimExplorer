#pragma once

#include "ETW.h"
#include <memory>
#include <vector>
#include <algorithm>


struct CapturePacket
{
	int64_t eventTimeStamp;
	int64_t captureTimeStamp;
	int64_t freq;

	uint32_t frameIndex;

	float gamepadX;
	float gamepadY;

	float val01;
	float val02;

	CapturePacket()
	{
		eventTimeStamp = 0;
		captureTimeStamp = 0;
		freq = 0;
		frameIndex = 0;
		gamepadX = 0.0f;
		gamepadY = 0.0f;
		val01 = 0.0f;
		val02 = 0.0f;
	}
};



class HistoryBuffer
{
	// this spinlock never gives a time slice to the operating system
	class SpinLock
	{
		LONG volatile state;

	public:
		SpinLock()
		{
			state = 0;
		}

		void Enter()
		{
			//atomic exchange from 0 to 1
			while (InterlockedCompareExchange(&state, 1, 0) != 0)
			{
				YieldProcessor();
			}
		}

		void Leave()
		{
			state = 0;
		}

	};


	std::vector<CapturePacket> producer;
	std::vector<CapturePacket> producerToConsumer;
	size_t maxElementsCount;
	SpinLock spinLock;

	std::vector<CapturePacket> consumer;

public:

	HistoryBuffer(size_t _maxElementsCount)
		: maxElementsCount(_maxElementsCount)
	{
		producer.reserve(maxElementsCount);
		producerToConsumer.reserve(maxElementsCount);
		consumer.reserve(maxElementsCount * 2);
	}

	~HistoryBuffer()
	{
	}

	void Clear()
	{
		spinLock.Enter();
		{
			producer.clear();
			producerToConsumer.clear();
			consumer.clear();
		}
		spinLock.Leave();
	}

	void Put(const CapturePacket & item)
	{
		spinLock.Enter();
		{
			producer.push_back(item);
		}
		spinLock.Leave();
	}

	bool Find(int64_t desiredTimestamp, CapturePacket * result)
	{
		//this lock must be as small as posible!
		spinLock.Enter();
		{
			producer.swap(producerToConsumer);
		}
		spinLock.Leave();

		//move data from producer to consumer
		for (const CapturePacket & p : producerToConsumer)
		{
			consumer.push_back(p);
		}
		producerToConsumer.clear();
	
		auto upperBound = std::upper_bound(consumer.begin(), consumer.end(), desiredTimestamp, [](int64_t desiredTime, const CapturePacket &packet)
		{
			return desiredTime < packet.captureTimeStamp;
		});

		size_t dbgIndex = upperBound - consumer.begin();

		bool isFound = false;

		if (upperBound != consumer.end())
		{
			if (upperBound == consumer.begin())
			{
				isFound = true;
				*result = *upperBound;
			} else
			{
				const CapturePacket & packetBefore = *upperBound;
				upperBound--;
				const CapturePacket & packetAfter = *upperBound;
				if (packetBefore.captureTimeStamp > desiredTimestamp && packetAfter.captureTimeStamp <= desiredTimestamp)
				{
					isFound = true;
					*result = packetAfter;
				} else
				{
					__debugbreak();
				}
			}
		}

		//trim from start
		if (consumer.size() > maxElementsCount)
		{
			size_t removeCount = consumer.size() - maxElementsCount;
			consumer.erase(consumer.begin(), consumer.begin() + removeCount);
		}

		return isFound;
	}



};



class EtwTracer
{
	struct TracePropertiesDeleter
	{
		void operator()(EVENT_TRACE_PROPERTIES* p)
		{
			if (p)
			{
				free(p);
			}
		}
	};

	std::unique_ptr<EVENT_TRACE_PROPERTIES, TracePropertiesDeleter> traceProperties;
	TRACEHANDLE traceSessionHandle;
	TRACEHANDLE etwLoggerHandle;
	HANDLE parserThreadHandle;
	HANDLE captureThreadHandle;
	HANDLE targetProcessHandle;
	uint64_t targetAddr01;
	uint64_t targetAddr02;
	uint32_t targetProcessId;
	uint32_t frameIndex;
	volatile uint32_t captureThreadShouldQuit;
	bool dxgiEnabled;
	bool dx9Enabled;

	static DWORD WINAPI EtwProcessingThread(LPVOID parameter);
	static DWORD WINAPI EtwCaptureThread(LPVOID parameter);


	CRITICAL_SECTION cs;
	std::vector<CapturePacket> packets;

	HistoryBuffer capturedData;

	void CaptureThreadEntry();

	void DoCaptureData();

public:

	EtwTracer();
	~EtwTracer();

	uint32_t StartSession(uint32_t _targetProcess, uint64_t addr01, uint64_t addr02);
	uint32_t StopSession();

	void OnPresentEvent(uint32_t targetProcessId, USHORT eventId, LONGLONG eventTimeStamp);

	uint32_t TryGetCapturedData(CapturePacket* buffer, uint32_t bufferSizeInElements);
};