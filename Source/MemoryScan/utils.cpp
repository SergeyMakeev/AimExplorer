#include <windows.h>
#include <psapi.h>
#include <vector>
#include <assert.h>
#include "utils.h"


#pragma comment( lib, "psapi" )

namespace utils
{
	bool ActivateProcessMainWindow(Process* process)
	{
		for (int i = 0; i < 128; i++)
		{
			if (process->IsForegroundWindow())
			{
				return true;
			}
			process->SetAsForegroundWindow();
			Sleep(20);
		}

		Sleep(500);
		return process->IsForegroundWindow();
	}

	void MoveMouse(int dx, int dy)
	{
		mouse_event(MOUSEEVENTF_MOVE, dx, dy, 0, 0);
	}

	bool GenRandom(uint8_t* buffer, uint32_t bufferSize)
	{
		HCRYPTPROV hCryptProv;
		if (!CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, 0))
		{
			return false;
		}

		if (!CryptGenRandom(hCryptProv, bufferSize, buffer))
		{
			return false;
		}

		if (!CryptReleaseContext(hCryptProv, 0))
		{
			return false;
		}

		return true;
	}

	bool GenSymmetricDeltas(int* deltasBuffer, uint32_t bufferSize, int val, int val_min, int val_max)
	{
		assert(((bufferSize & 1) == 0) && "Invalid buffer size. Buffer size must be event number.");

		std::vector<uint8_t> rnd;
		rnd.resize(bufferSize);
		utils::GenRandom(&rnd[0], (uint32_t)rnd.size());

		for (size_t i = 0; i < bufferSize; i++)
		{
			if (rnd[i] < 128)
				deltasBuffer[i] = -val;
			else
				deltasBuffer[i] = val;
		}

		int currentRotation = 0;
		for (size_t i = 0; i < bufferSize; i++)
		{
			int delta = deltasBuffer[i];
			int newRotation = currentRotation + delta;
			if (newRotation < val_min || newRotation > val_max)
			{
				deltasBuffer[i] = -deltasBuffer[i];
			}
			currentRotation += deltasBuffer[i];
		}

		//make symmetric
		size_t offset = (bufferSize / 2);
		for (size_t i = offset; i < bufferSize; i++)
		{
			deltasBuffer[i] = -deltasBuffer[i - offset];
		}

		return true;
	}

	void DebugMessage(const char* msg, ...)
	{
		static char strBuffer[4096];

		va_list args;
		va_start(args, msg);
		vsprintf_s(strBuffer, 512, msg, args);
		strBuffer[4095] = '\0';
		va_end(args);

		printf("%s", strBuffer);
		OutputDebugStringA(strBuffer);
	}

}


class ProcessImpl : public Process
{
	size_t executableBase;
	HANDLE handle;
	HWND hWnd;
	size_t largestPageSizeInBytes;
	size_t readableMemoryInBytes;
	std::vector<MemoryPage> pages;
	uint32_t pid;
	bool isX86;
	char executablePath[MAX_PATH];


	static size_t GetModuleBase(HANDLE hProcess, const char* szModulePath)
	{
		char szModName[MAX_PATH];
		HMODULE hMods[1024];
		DWORD cbNeeded;

		if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
		{
			for (uint32_t i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
			{
				// Get the full path to the module's file.
				if (GetModuleFileNameExA(hProcess, hMods[i], szModName, sizeof(szModName) / sizeof(TCHAR)))
				{
					if (_stricmp(szModulePath, szModName) == 0)
					{
						return (size_t)hMods[i];
					}
				}
			}
		}

		return 0;
	}


	static BOOL CALLBACK FindProcessMainWindowCallback(HWND hWnd, LPARAM lParam)
	{
		ProcessImpl* self = (ProcessImpl*)lParam;

		DWORD pid = 0;
		GetWindowThreadProcessId(hWnd, &pid);
		if (pid != self->pid)
		{
			return TRUE;
		}

		self->hWnd = hWnd;
		if (GetWindow(hWnd, GW_OWNER) == (HWND)NULL && IsWindowVisible(hWnd))
		{
			return FALSE;
		}
		return TRUE;
	}

	bool QueryMemoryPages()
	{
		largestPageSizeInBytes = 0;
		readableMemoryInBytes = 0;
		pages.clear();

		if (handle == NULL || pid == 0)
		{
			utils::DebugMessage("You should Open() process before calling QueryMemoryPages()\n");
			return false;
		}

		uintptr_t memoryStart = 0x0;
		uintptr_t memoryStop = (uintptr_t)0x7fffffffffffffff;
		if (isX86)
		{
			memoryStop = 0x7fffffff;
		}

		MEMORY_BASIC_INFORMATION m;
		uintptr_t address = memoryStart;
		while (address < memoryStop)
		{
			size_t ret = VirtualQueryEx(handle, (LPCVOID)address, &m, sizeof(MEMORY_BASIC_INFORMATION));
			if (ret != sizeof(MEMORY_BASIC_INFORMATION))
			{
				break;
			}

			if (((uintptr_t)m.BaseAddress + (uintptr_t)m.RegionSize) == address)
			{
				break;
			}

			if (m.State & MEM_COMMIT)
			{
				if ((m.Protect & PAGE_NOACCESS) == 0 &&
					(m.Protect & PAGE_GUARD) == 0 &&
					(m.Protect & PAGE_NOCACHE) == 0 &&
					(m.Protect & PAGE_EXECUTE) == 0 &&
					(m.Protect & PAGE_EXECUTE_READWRITE) == 0 &&
					(m.Protect & PAGE_EXECUTE_WRITECOPY) == 0 &&
					(m.Protect & PAGE_EXECUTE_READ) == 0)
				{
					MemoryPage page;
					page.baseAddr = (uintptr_t)m.BaseAddress;
					page.size = m.RegionSize;
					page.suspectsMask = std::make_unique<bit_array>(page.size / sizeof(float));
					pages.push_back(std::move(page));
					assert(page.size & 3 && "Must be multiple of 4");
					largestPageSizeInBytes = max(largestPageSizeInBytes, m.RegionSize);
					readableMemoryInBytes += m.RegionSize;
				}
			}

			address = (uintptr_t)m.BaseAddress + (uintptr_t)m.RegionSize;
		}

		return true;
	}



public:

	ProcessImpl()
	{
		pid = 0;
		handle = nullptr;
		hWnd = nullptr;
		isX86 = false;
		largestPageSizeInBytes = 0;
		readableMemoryInBytes = 0;
		pages.reserve(1024);
		executablePath[0] = '\0';
	}

	virtual ~ProcessImpl()
	{
		hWnd = NULL;
		pid = 0;
		if (handle != NULL)
		{
			CloseHandle(handle);
		}
	}

	virtual bool Open(uint32_t _pid) override
	{
		pid = _pid;

		handle = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
		if (handle == NULL)
		{
			DWORD lastErr = GetLastError();
			utils::DebugMessage("Can't open process pid:%d, GetLastError = 0x%08X\n", pid, lastErr);
			return false;
		}

		executablePath[0] = '\0';
		DWORD res = GetModuleFileNameExA(handle, NULL, executablePath, MAX_PATH);
		executablePath[MAX_PATH - 1] = '\0';
		if (res == 0)
		{
			DWORD lastErr = GetLastError();
			utils::DebugMessage("GetModuleFileNameExA failed, GetLastError = 0x%08X\n", lastErr);
			return false;
		}

		executableBase = GetModuleBase(handle, executablePath);

		// detect process type x86/x64
		BOOL isX86Process = (executableBase < UINT_MAX);
		if (!IsWow64Process(handle, &isX86Process))
		{
			DWORD lastErr = GetLastError();
			utils::DebugMessage("IsWow64Process failed, GetLastError = 0x%08X\n", lastErr);
			return false;
		}
		isX86 = (isX86Process != 0);

		// try to find process main window
		EnumWindows(FindProcessMainWindowCallback, (LPARAM)this);

		bool r = QueryMemoryPages();
		if (r == false)
		{
			utils::DebugMessage("Failed to query memory pages.\n");
			return false;
		}

		return true;
	}


	virtual size_t GetLargestMemoryPageSizeInBytes() const override
	{
		return largestPageSizeInBytes;
	}


	virtual bool SetAsForegroundWindow() const override
	{
		if (hWnd == nullptr)
		{
			return false;
		}
		BOOL r = SetForegroundWindow(hWnd);
		return (r != 0);
	}

	virtual bool IsForegroundWindow() const override
	{
		if (hWnd == nullptr)
		{
			return false;
		}

		return GetForegroundWindow() == hWnd;
	}


	virtual size_t ReadMemory(void* pBuffer, uintptr_t baseAddr, size_t size) const override
	{
		if (handle == nullptr)
		{
			return 0;
		}

		SIZE_T pageReadedBytes = 0;
		BOOL res = ::ReadProcessMemory(handle, (LPCVOID)baseAddr, pBuffer, size, &pageReadedBytes);
		if (res == 0)
		{
			DWORD lastErr = GetLastError();
			if (lastErr != ERROR_PARTIAL_COPY)
			{
				utils::DebugMessage("ReadProcessMemory failed, GetLastError = 0x%08X\n", lastErr);
				return 0;
			}
		}

		return pageReadedBytes;
	}

	virtual size_t GetPagesCount() const override
	{
		return pages.size();
	}

	virtual const MemoryPage& GetPageByIndex(size_t index) const override
	{
		return pages[index];
	}

	virtual const char* GetExecutablePath() const override
	{
		return &executablePath[0];
	}

	virtual size_t GetExecutableBase() const override
	{
		return executableBase;
	}

	virtual size_t GetReadableBytesCount() const override
	{
		return readableMemoryInBytes;
	}

	


};


Process* Process::Create()
{
	return new ProcessImpl();
}




