#pragma once
#ifdef _WIN32

#include <windows.h>

#define INITGUID  // Causes definition of SystemTraceControlGuid in evntrace.h.
#include <strsafe.h>
#include <wmistr.h>
#include <evntrace.h>
#include <evntcons.h>

#if _MSC_VER <= 1600
#define EVENT_DESCRIPTOR_DEF
#define EVENT_HEADER_DEF
#define EVENT_HEADER_EXTENDED_DATA_ITEM_DEF
#define EVENT_RECORD_DEF
#endif

///////////////////////////////////////////////////////////////////////////////
#define PROCESS_TRACE_MODE_REAL_TIME                0x00000100
#define PROCESS_TRACE_MODE_RAW_TIMESTAMP            0x00001000
#define PROCESS_TRACE_MODE_EVENT_RECORD             0x10000000
///////////////////////////////////////////////////////////////////////////////
#ifndef EVENT_DESCRIPTOR_DEF
#define EVENT_DESCRIPTOR_DEF
typedef struct _EVENT_DESCRIPTOR {

	USHORT      Id;
	UCHAR       Version;
	UCHAR       Channel;
	UCHAR       Level;
	UCHAR       Opcode;
	USHORT      Task;
	ULONGLONG   Keyword;

} EVENT_DESCRIPTOR, *PEVENT_DESCRIPTOR;
typedef const EVENT_DESCRIPTOR *PCEVENT_DESCRIPTOR;
#endif
///////////////////////////////////////////////////////////////////////////////
#ifndef EVENT_HEADER_DEF
#define EVENT_HEADER_DEF
typedef struct _EVENT_HEADER {

	USHORT              Size;                   
	USHORT              HeaderType;             
	USHORT              Flags;                  
	USHORT              EventProperty;          
	ULONG               ThreadId;               
	ULONG               ProcessId;              
	LARGE_INTEGER       TimeStamp;              
	GUID                ProviderId;             
	EVENT_DESCRIPTOR    EventDescriptor;        
	union {
		struct {
			ULONG       KernelTime;             
			ULONG       UserTime;               
		} DUMMYSTRUCTNAME;
		ULONG64         ProcessorTime;          
												
	} DUMMYUNIONNAME;
	GUID                ActivityId;             

} EVENT_HEADER, *PEVENT_HEADER;
#endif
///////////////////////////////////////////////////////////////////////////////
#ifndef EVENT_HEADER_EXTENDED_DATA_ITEM_DEF
#define EVENT_HEADER_EXTENDED_DATA_ITEM_DEF
typedef struct _EVENT_HEADER_EXTENDED_DATA_ITEM {

	USHORT      Reserved1;                      // Reserved for internal use
	USHORT      ExtType;                        // Extended info type 
	struct {
		USHORT  Linkage : 1;       // Indicates additional extended 
								   // data item
		USHORT  Reserved2 : 15;
	};
	USHORT      DataSize;                       // Size of extended info data
	ULONGLONG   DataPtr;                        // Pointer to extended info data

} EVENT_HEADER_EXTENDED_DATA_ITEM, *PEVENT_HEADER_EXTENDED_DATA_ITEM;
#endif
///////////////////////////////////////////////////////////////////////////////
#ifndef EVENT_RECORD_DEF
#define EVENT_RECORD_DEF
typedef struct _EVENT_RECORD {
	EVENT_HEADER        EventHeader;          
	ETW_BUFFER_CONTEXT  BufferContext;        
	USHORT              ExtendedDataCount;    
												
	USHORT              UserDataLength;       
	PEVENT_HEADER_EXTENDED_DATA_ITEM ExtendedData;           
	PVOID               UserData;             
	PVOID               UserContext;          
} EVENT_RECORD, *PEVENT_RECORD;
#endif
///////////////////////////////////////////////////////////////////////////////



// from Intel PresentMon
// https://github.com/GameTechDev/PresentMon

struct __declspec(uuid("{CA11C036-0102-4A2D-A6AD-F03CFED5D3C9}")) DXGI_PROVIDER_GUID_HOLDER;
struct __declspec(uuid("{802ec45a-1e99-4b83-9920-87c98277ba9d}")) DXGKRNL_PROVIDER_GUID_HOLDER;
struct __declspec(uuid("{783ACA0A-790E-4d7f-8451-AA850511C6B9}")) D3D9_PROVIDER_GUID_HOLDER;

static const auto DXGI_PROVIDER_GUID = __uuidof(DXGI_PROVIDER_GUID_HOLDER);
static const auto DXGKRNL_PROVIDER_GUID = __uuidof(DXGKRNL_PROVIDER_GUID_HOLDER);
static const auto D3D9_PROVIDER_GUID = __uuidof(D3D9_PROVIDER_GUID_HOLDER);



#endif
