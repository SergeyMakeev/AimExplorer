
#include "ETWTracer.h"


#define DLL_EXPORT_API extern "C" __declspec(dllexport)



EtwTracer* GetEtwTracer()
{
	static EtwTracer tracer;
	return &tracer;
}


DLL_EXPORT_API uint32_t StartEtwSession(uint32_t targetProcessId, uint64_t addr01, uint64_t addr02)
{
	uint32_t err = GetEtwTracer()->StartSession(targetProcessId, addr01, addr02);
	return err;
}


DLL_EXPORT_API uint32_t StopEtwSession()
{
	uint32_t err = GetEtwTracer()->StopSession();
	return err;
}


DLL_EXPORT_API uint32_t GetCapturedData(CapturePacket* buffer, uint32_t bufferSizeInElements)
{
	uint32_t elementsCount = GetEtwTracer()->TryGetCapturedData(buffer, bufferSizeInElements);
	return elementsCount;
}


