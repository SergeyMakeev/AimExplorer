#include "ETWTracer.h"

#include <Xinput.h>
#pragma comment(lib, "xinput.lib")



#define CAPTURE_THREAD_CIRCULAR_BUFFER_SIZE (1048576) /* 1 000 000 values ~ 50 sec of data */
#define VISUALIZER_LOGGER_NAME L"FpsLatency"

static const USHORT DXGIPresent_Start = 42;
static const USHORT DXGIPresent_Stop = 43;
static const USHORT DXGIPresentMPO_Start = 55;
static const USHORT DXGIPresentMPO_Stop = 56;

static const USHORT D3D9PresentStart = 1;
static const USHORT D3D9PresentStop = 2;


void WINAPI OnRecordEvent(PEVENT_RECORD eventRecord)
{
	EtwTracer* tracer = (EtwTracer*)eventRecord->UserContext;

	ULONG processId = eventRecord->EventHeader.ProcessId;
	LONGLONG timeStamp = eventRecord->EventHeader.TimeStamp.QuadPart;

	if (eventRecord->EventHeader.ProviderId == DXGI_PROVIDER_GUID)
	{

		switch (eventRecord->EventHeader.EventDescriptor.Id)
		{
		case DXGIPresent_Start:
		case DXGIPresentMPO_Start:
		case DXGIPresent_Stop:
		case DXGIPresentMPO_Stop:
			tracer->OnPresentEvent(processId, eventRecord->EventHeader.EventDescriptor.Id, timeStamp);
			break;
		}
	} else if (eventRecord->EventHeader.ProviderId == D3D9_PROVIDER_GUID)
	{
		switch (eventRecord->EventHeader.EventDescriptor.Id)
		{
		case D3D9PresentStart:
		case D3D9PresentStop:
			tracer->OnPresentEvent(processId, eventRecord->EventHeader.EventDescriptor.Id, timeStamp);
			break;
		}
	}
}

static ULONG WINAPI OnBufferRecord(_In_ PEVENT_TRACE_LOGFILE Buffer)
{
	return 1;
}

EtwTracer::EtwTracer()
: traceProperties((EVENT_TRACE_PROPERTIES*)malloc(sizeof(EVENT_TRACE_PROPERTIES) + sizeof(VISUALIZER_LOGGER_NAME)))
, traceSessionHandle(INVALID_PROCESSTRACE_HANDLE)
, etwLoggerHandle(INVALID_PROCESSTRACE_HANDLE)
, parserThreadHandle(NULL)
, captureThreadHandle(NULL)
, targetProcessHandle(NULL)
, targetAddr01(0)
, targetAddr02(0)
, targetProcessId(0)
, frameIndex(0)
, captureThreadShouldQuit(0)
, dxgiEnabled(false)
, dx9Enabled(false)
, capturedData(CAPTURE_THREAD_CIRCULAR_BUFFER_SIZE)
{
	InitializeCriticalSectionAndSpinCount(&cs, 16);
	packets.reserve(1024 * 1024);
}

EtwTracer::~EtwTracer()
{
	StopSession();
	DeleteCriticalSection(&cs);
}

uint32_t EtwTracer::StopSession()
{
	if (traceSessionHandle == INVALID_PROCESSTRACE_HANDLE)
	{
		return 1;
	}
	ULONG status = 0;
	status = ControlTrace(etwLoggerHandle, VISUALIZER_LOGGER_NAME, traceProperties.get(), EVENT_TRACE_CONTROL_STOP);

	if (parserThreadHandle != NULL)
	{
		WaitForSingleObject(parserThreadHandle, INFINITE);
		CloseHandle(parserThreadHandle);
	}

	captureThreadShouldQuit = 1;
	if (captureThreadHandle != NULL)
	{
		WaitForSingleObject(captureThreadHandle, INFINITE);
		CloseHandle(captureThreadHandle);
	}

	if (targetProcessHandle != NULL)
	{
		CloseHandle(targetProcessHandle);
		targetProcessHandle = NULL;
	}


	// stop DXGI provider
	if (dxgiEnabled)
	{
		status = EnableTraceEx2(traceSessionHandle, &D3D9_PROVIDER_GUID, EVENT_CONTROL_CODE_DISABLE_PROVIDER, 0, 0, 0, 0, nullptr);
		dxgiEnabled = false;
	}

	// stop DXGI provider
	if (dx9Enabled)
	{
		status = EnableTraceEx2(traceSessionHandle, &DXGI_PROVIDER_GUID, EVENT_CONTROL_CODE_DISABLE_PROVIDER, 0, 0, 0, 0, nullptr);
		dx9Enabled = false;
	}

	// stop session
	status = ControlTrace(traceSessionHandle, nullptr, traceProperties.get(), EVENT_TRACE_CONTROL_STOP);

	traceSessionHandle = INVALID_PROCESSTRACE_HANDLE;

	EnterCriticalSection(&cs);
	{
		packets.clear();
		capturedData.Clear();
	}
	LeaveCriticalSection(&cs);


	return 0;
}

uint32_t EtwTracer::StartSession(uint32_t _targetProcessId, uint64_t addr01, uint64_t addr02)
{
	if (traceSessionHandle != INVALID_PROCESSTRACE_HANDLE)
	{
		// session is already started
		return 1;
	}

	targetProcessHandle = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, _targetProcessId);
	if (targetProcessHandle == NULL)
	{
		return 99;
	}

	targetProcessId = _targetProcessId;
	targetAddr01 = addr01;
	targetAddr02 = addr02;

	// Enable realtime priority for our process
	///////////////////////////////////////////////////////////////////////////////////////////////
	SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);

	// Start ETW Session
	///////////////////////////////////////////////////////////////////////////////////////////////
	ULONG bufferSize = sizeof(EVENT_TRACE_PROPERTIES) + sizeof(VISUALIZER_LOGGER_NAME);
	memset(traceProperties.get(), 0, bufferSize);

	traceProperties->Wnode.BufferSize = bufferSize;//
	traceProperties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
	traceProperties->EnableFlags = 0;
	traceProperties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
	traceProperties->Wnode.Flags = 0;
	traceProperties->MinimumBuffers = 200;
	//
	// https://msdn.microsoft.com/en-us/library/windows/desktop/aa364160(v=vs.85).aspx
	// Clock resolution = QPC
	traceProperties->Wnode.ClientContext = 1;

	int retryCount = 4;
	ULONG status = 0;
	while (--retryCount >= 0)
	{
		status = StartTrace(&traceSessionHandle, VISUALIZER_LOGGER_NAME, traceProperties.get());

		switch (status)
		{
		case ERROR_NO_SUCH_PRIVILEGE:
		{
			HANDLE token = 0;
			if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
			{
				TOKEN_PRIVILEGES tokenPrivileges;
				memset(&tokenPrivileges, 0, sizeof(tokenPrivileges));
				tokenPrivileges.PrivilegeCount = 1;
				tokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
				LookupPrivilegeValue(NULL, SE_SYSTEM_PROFILE_NAME, &tokenPrivileges.Privileges[0].Luid);

				AdjustTokenPrivileges(token, FALSE, &tokenPrivileges, 0, (PTOKEN_PRIVILEGES)NULL, 0);
				CloseHandle(token);
			}
		}
		break;

		case ERROR_ALREADY_EXISTS:
			ControlTrace(0, VISUALIZER_LOGGER_NAME, traceProperties.get(), EVENT_TRACE_CONTROL_STOP);
			break;

		case ERROR_ACCESS_DENIED:
			StopSession();
			return 2;

		case ERROR_SUCCESS:
			retryCount = 0;
			break;

		default:
			StopSession();
			return 3;
		}
	}


	if (traceSessionHandle == INVALID_PROCESSTRACE_HANDLE)
	{
		return 4;
	}

	// Enable DX providers
	///////////////////////////////////////////////////////////////////////////////////////////////
	
	status = EnableTraceEx2(traceSessionHandle, &DXGI_PROVIDER_GUID, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 0, 0, 0, 0, nullptr);
	if (status != ERROR_SUCCESS)
	{
		StopSession();
		return 5;
	}
	dxgiEnabled = true;

	status = EnableTraceEx2(traceSessionHandle, &D3D9_PROVIDER_GUID, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 0, 0, 0, 0, nullptr);
	if (status != ERROR_SUCCESS)
	{
		StopSession();
		return 6;
	}
	dx9Enabled = true;



	// Enable ETW logger
	///////////////////////////////////////////////////////////////////////////////////////////////

	EVENT_TRACE_LOGFILE logFile;
	memset(&logFile, 0, sizeof(EVENT_TRACE_LOGFILE));
	logFile.LoggerName = VISUALIZER_LOGGER_NAME;
	logFile.ProcessTraceMode = (PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP);
	logFile.EventRecordCallback = OnRecordEvent;
	logFile.BufferCallback = OnBufferRecord;
	logFile.Context = this;
	etwLoggerHandle = OpenTrace(&logFile);

	if (etwLoggerHandle == INVALID_PROCESSTRACE_HANDLE)
	{
		StopSession();
		return 7;
	}

	// Create high priority thread to parse etw logger data
	///////////////////////////////////////////////////////////////////////////////////////////////
	DWORD threadID;
	parserThreadHandle = CreateThread(0, 0, EtwProcessingThread, this, 0, &threadID);


	// Create high priority thread to capture data from target process
	///////////////////////////////////////////////////////////////////////////////////////////////
	captureThreadShouldQuit = 0;
	captureThreadHandle = CreateThread(0, 0, EtwCaptureThread, this, 0, &threadID);

	frameIndex = 0;

	return 0;
}

inline int64_t GetHighFrequencyTime()
{
	LARGE_INTEGER largeInteger;
	QueryPerformanceCounter(&largeInteger);
	return largeInteger.QuadPart;
}

inline int64_t GetFrequency()
{
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	return frequency.QuadPart;
}

inline int64_t GetTimeMicroSeconds()
{
	LARGE_INTEGER largeInteger;
	QueryPerformanceCounter(&largeInteger);
	return (largeInteger.QuadPart * int64_t(1000000)) / GetFrequency();
}

inline void SpinSleepMicroSeconds(uint32_t microseconds)
{
	int64_t desiredTime = GetTimeMicroSeconds() + microseconds;
	for (;;)
	{
		int64_t timeNow = GetTimeMicroSeconds();
		if (timeNow > desiredTime)
		{
			break;
		}
		YieldProcessor();
	}
}


void EtwTracer::CaptureThreadEntry()
{
	while (captureThreadShouldQuit == 0)
	{
		DoCaptureData();

		//20000 captures per second
		SpinSleepMicroSeconds(50);
	}
}

void EtwTracer::DoCaptureData()
{
	LARGE_INTEGER largeInteger;
	QueryPerformanceCounter(&largeInteger);
	LONGLONG captureTimeStamp = largeInteger.QuadPart;

	LARGE_INTEGER largeIntegerFreq;
	QueryPerformanceFrequency(&largeIntegerFreq);

	CapturePacket packet;
	packet.frameIndex = 0; // unknown at this moment
	packet.eventTimeStamp = 0; // unknown at this moment
	packet.captureTimeStamp = captureTimeStamp;
	packet.freq = largeIntegerFreq.QuadPart;

	XINPUT_STATE state;
	ZeroMemory(&state, sizeof(XINPUT_STATE));
	DWORD dwResult = XInputGetState(0, &state);
	if (dwResult == ERROR_SUCCESS)
	{
		// sThumbR are in range -32768 to +32767
		packet.gamepadX = (state.Gamepad.sThumbRX / 32768.0f);
		packet.gamepadY = (state.Gamepad.sThumbRY / 32768.0f);
	}
	else
	{
		packet.gamepadX = -999.0f;
		packet.gamepadY = -999.0f;
	}

	size_t pageReadedBytes = 0;
	if (ReadProcessMemory(targetProcessHandle, (LPCVOID)targetAddr01, &packet.val01, sizeof(float), &pageReadedBytes) == 0)
	{
		packet.val01 = -999.0f;
	}

	if (ReadProcessMemory(targetProcessHandle, (LPCVOID)targetAddr02, &packet.val02, sizeof(float), &pageReadedBytes) == 0)
	{
		packet.val02 = -999.0f;
	}

	capturedData.Put(packet);
}

void EtwTracer::OnPresentEvent(uint32_t processId, USHORT eventId, LONGLONG eventTimeStamp)
{
	if (processId != targetProcessId)
	{
		return;
	}

	if (eventId != DXGIPresent_Start && eventId != D3D9PresentStart)
	{
		return;
	}

	DoCaptureData();

	// get captured data
	CapturePacket packet;
	if (capturedData.Find(eventTimeStamp, &packet))
	{
		packet.eventTimeStamp = eventTimeStamp;
		packet.frameIndex = frameIndex;

		EnterCriticalSection(&cs);
		{
			packets.push_back(packet);
		}
		LeaveCriticalSection(&cs);
	}

	frameIndex++;
}

uint32_t EtwTracer::TryGetCapturedData(CapturePacket* buffer, uint32_t bufferSizeInElements)
{
	uint32_t count = 0;

	EnterCriticalSection(&cs);
	{
		uint32_t resCount = (uint32_t)packets.size();
		count = min(resCount, bufferSizeInElements);
		for (size_t i = 0; i < count; i++)
		{
			buffer[i] = packets[i];
		}
		packets.clear();
	}
	LeaveCriticalSection(&cs);

	return count;

}


DWORD WINAPI EtwTracer::EtwProcessingThread(LPVOID parameter)
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	EtwTracer* tracer = (EtwTracer*)parameter;
	ULONG status = ProcessTrace(&tracer->etwLoggerHandle, 1, 0, 0);
	return 0;
}

DWORD WINAPI EtwTracer::EtwCaptureThread(LPVOID parameter)
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	EtwTracer* tracer = (EtwTracer*)parameter;
	tracer->CaptureThreadEntry();
	return 0;
}



