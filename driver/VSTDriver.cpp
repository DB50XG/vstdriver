/* Copyright (C) 2011 Chris Moeller, Brad Miller
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string>
#include "VSTDriver.h"
#include <assert.h>
#include <filesystem>

using std::vector;

enum Command : uint32_t
{
	Exit = 0,
	GetChunkData = 1,
	SetChunkData = 2,
	VstiHasEditor = 3,
	DisplayEditorModal = 4,
	SetSampleRate = 5,
	Reset = 6,
	SendMidiEvent = 7,
	SendMidiSystemExclusiveEvent = 8,
	RenderAudioSamples = 9,
};

VSTDriver::VSTDriver()
{
	isInitialized = false;
	isTerminating = false;
	hProcess = NULL;
	hThread = NULL;
	hReadEvent = NULL;
	hChildStd_IN_Rd = NULL;
	/// <summary>
	/// The MIDI input of the VSTi DLL
	/// </summary>
	hChildStd_IN_Wr = NULL;
	/// <summary>
	/// The Audio output of the VSTi DLL
	/// </summary>
	hChildStd_OUT_Rd = NULL;
	hChildStd_OUT_Wr = NULL;
	audioOutputs = 0;
	effectName = NULL;
	vendor = NULL;
	product = NULL;
}

VSTDriver::~VSTDriver()
{
	CloseVSTDriver();
	delete[] effectName;
	delete[] vendor;
	delete[] product;
}

static WORD getwordle(BYTE* pData)
{
	return (WORD)(pData[0] | (((WORD)pData[1]) << 8));
}

static DWORD getdwordle(BYTE* pData)
{
	return pData[0] | (((DWORD)pData[1]) << 8) | (((DWORD)pData[2]) << 16) | (((DWORD)pData[3]) << 24);
}

unsigned VSTDriver::test_plugin_platform() {
#define iMZHeaderSize (0x40)
#define iPEHeaderSize (4 + 20 + 224)

	BYTE peheader[iPEHeaderSize];
	DWORD dwOffsetPE;

	FILE* f = _tfopen(szPluginPath, _T("rb"));
	if (!f) goto error;
	if (fread(peheader, 1, iMZHeaderSize, f) < iMZHeaderSize) goto error;
	if (getwordle(peheader) != 0x5A4D) goto error;
	dwOffsetPE = getdwordle(peheader + 0x3c);
	if (fseek(f, dwOffsetPE, SEEK_SET) != 0) goto error;
	if (fread(peheader, 1, iPEHeaderSize, f) < iPEHeaderSize) goto error;
	fclose(f); f = NULL;
	if (getdwordle(peheader) != 0x00004550) goto error;
	switch (getwordle(peheader + 4)) {
		case 0x014C: return 32;
		case 0x8664: return 64;
	}

error:
	if (f) fclose(f);
	return 0;
}

/// <summary>
/// Initialize the path to the VSTi plugin
/// </summary>
/// <param name="effect">The path to the VSTi</param>
void VSTDriver::InitializeVstiPath(TCHAR* szPath)
{
	if (szPath)
	{
		DWORD pluginPathLength = 0;
		pluginPathLength = _tcslen(szPath) * sizeof(TCHAR);
		szPluginPath = (TCHAR*)calloc(pluginPathLength + sizeof(TCHAR), 1);
		_tcscpy(szPluginPath, szPath);
	}
	else
	{
		HKEY hKey;
		long result = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\VSTi Driver", 0, KEY_READ | KEY_WOW64_32KEY, &hKey);

		if (result != NO_ERROR)
		{
			return;
		}

		ULONG size;
		DWORD registryType = REG_NONE;

		/// Get the current VSTi plugin path. Example: C:\Program Files (x86)\yamaha_syxg50_vsti\syxg50.dll
		result = RegQueryValueEx(hKey, L"plugin", NULL, &registryType, NULL, &size);

		if (result != NO_ERROR || size == 0 || (registryType != REG_SZ && registryType != REG_EXPAND_SZ))
		{
			return;
		}

		szPluginPath = (TCHAR*)calloc(size, sizeof(TCHAR));
		result = RegQueryValueEx(hKey, L"plugin", NULL, &registryType, (LPBYTE)szPluginPath, &size);

		if (result != NO_ERROR || size == 0 || (registryType != REG_SZ && registryType != REG_EXPAND_SZ))
		{
			return;
		}

		RegCloseKey(hKey);
	}

	uPluginPlatform = test_plugin_platform();
}

/// <summary>
/// Load the settings of the VSTi plugin
/// </summary>
void VSTDriver::LoadVstiSettings()
{
	HKEY hKey;
	long result = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\VSTi Driver\\Persistence", 0, KEY_READ | KEY_WOW64_32KEY, &hKey);

	if (result != NO_ERROR)
	{
		return;
	}

	ULONG chunkSize;
	vector<uint8_t> chunk;
	DWORD registryType = REG_NONE;

	/// Get the VSTi plugin settings
	result = RegQueryValueEx(hKey, std::filesystem::path(szPluginPath).stem().c_str(), NULL, &registryType, NULL, &chunkSize);

	if (result != NO_ERROR || chunkSize == 0 || registryType != REG_BINARY)
	{
		RegCloseKey(hKey);
		return;
	}

	chunk.resize(chunkSize);

	result = RegQueryValueEx(hKey, std::filesystem::path(szPluginPath).stem().c_str(), NULL, &registryType, (LPBYTE)chunk.data(), &chunkSize);

	if (result != NO_ERROR)
	{
		RegCloseKey(hKey);
		return;
	}

	/// Set the VSTi plugin settings
	SetChunk(chunk.data(), chunkSize);

	RegCloseKey(hKey);
}

/// <summary>
/// Save the settings of the VSTi plugin
/// </summary>
void VSTDriver::SaveVstiSettings()
{
	if (!szPluginPath)
	{
		return;
	}

	HKEY hKey;

	/// Create the Persistence registry subkey
	long result = RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\VSTi Driver\\Persistence", 0, 0, 0, KEY_WRITE | KEY_WOW64_32KEY, NULL, &hKey, NULL);

	if (result != NO_ERROR)
	{
		return;
	}

	/// Get the VSTi plugin settings
	vector<uint8_t> chunk;
	GetChunk(chunk);

	if (chunk.size() > 0)
	{
		/// Save the VSTi plugin settings
		RegSetValueEx(hKey, std::filesystem::path(szPluginPath).stem().c_str(), 0, REG_BINARY, (LPBYTE)chunk.data(), chunk.size());
	}

	RegCloseKey(hKey);
}

static inline char print_hex_digit(unsigned val)
{
	static const char table[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
	assert((val & ~0xF) == 0);
	return table[val];
}

static void print_hex(unsigned val, std::wstring& out, unsigned bytes)
{
	unsigned n;
	for (n = 0; n < bytes; n++)
	{
		unsigned char c = (unsigned char)((val >> ((bytes - 1 - n) << 3)) & 0xFF);
		out += print_hex_digit(c >> 4);
		out += print_hex_digit(c & 0xF);
	}
}

static void print_guid(const GUID& p_guid, std::wstring& out)
{
	print_hex(p_guid.Data1, out, 4);
	out += '-';
	print_hex(p_guid.Data2, out, 2);
	out += '-';
	print_hex(p_guid.Data3, out, 2);
	out += '-';
	print_hex(p_guid.Data4[0], out, 1);
	print_hex(p_guid.Data4[1], out, 1);
	out += '-';
	print_hex(p_guid.Data4[2], out, 1);
	print_hex(p_guid.Data4[3], out, 1);
	print_hex(p_guid.Data4[4], out, 1);
	print_hex(p_guid.Data4[5], out, 1);
	print_hex(p_guid.Data4[6], out, 1);
	print_hex(p_guid.Data4[7], out, 1);
}

static bool GeneratePipeName(std::wstring& pipeName)
{
	GUID guid;
	if (FAILED(CoCreateGuid(&guid)))
	{
		return false;
	}

	pipeName = L"\\\\.\\pipe\\";
	print_guid(guid, pipeName);

	return true;
}

bool VSTDriver::connect_pipe(HANDLE hPipe)
{
	OVERLAPPED ol = {};
	ol.hEvent = hReadEvent;
	ResetEvent(hReadEvent);
	if (!ConnectNamedPipe(hPipe, &ol))
	{
		DWORD error = GetLastError();
		if (error == ERROR_PIPE_CONNECTED)
		{
			return true;
		}

		if (error != ERROR_IO_PENDING)
		{
			return false;
		}

		if (WaitForSingleObject(hReadEvent, 10000) == WAIT_TIMEOUT)
		{
			return false;
		}
	}
	return true;
}

extern "C" { extern HINSTANCE hinst_vst_driver; };

bool VSTDriver::process_create(uint32_t** error)
{
	isTerminating = false;

	if (uPluginPlatform != 32 && uPluginPlatform != 64)
	{
		return false;
	}

	SECURITY_ATTRIBUTES saAttr{};
	saAttr.nLength = sizeof(saAttr);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;

	if (!isInitialized)
	{
		if (FAILED(CoInitialize(NULL)))
		{
			return false;
		}
		isInitialized = true;
	}

	hReadEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	std::wstring pipeNameIn, pipeNameOut;
	if (!GeneratePipeName(pipeNameIn) || !GeneratePipeName(pipeNameOut))
	{
		process_terminate();
		return false;
	}

	HANDLE hPipe = CreateNamedPipe(pipeNameIn.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, &saAttr);
	if (hPipe == INVALID_HANDLE_VALUE)
	{
		process_terminate();
		return false;
	}
	hChildStd_IN_Rd = CreateFile(pipeNameIn.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &saAttr, OPEN_EXISTING, 0, NULL);
	DuplicateHandle(GetCurrentProcess(), hPipe, GetCurrentProcess(), &hChildStd_IN_Wr, 0, FALSE, DUPLICATE_SAME_ACCESS);
	CloseHandle(hPipe);

	hPipe = CreateNamedPipe(pipeNameOut.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED, PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, &saAttr);
	if (hPipe == INVALID_HANDLE_VALUE)
	{
		process_terminate();
		return false;
	}
	hChildStd_OUT_Wr = CreateFile(pipeNameOut.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &saAttr, OPEN_EXISTING, 0, NULL);
	DuplicateHandle(GetCurrentProcess(), hPipe, GetCurrentProcess(), &hChildStd_OUT_Rd, 0, FALSE, DUPLICATE_SAME_ACCESS);
	CloseHandle(hPipe);

	std::wstring szCmdLine = L"\"";

	TCHAR my_path[MAX_PATH];
	GetModuleFileName(hinst_vst_driver, my_path, _countof(my_path));

	szCmdLine += my_path;
	szCmdLine.resize(szCmdLine.find_last_of('\\') + 1);
	if (szCmdLine.find(L"vstmididrv\\") == std::string::npos)
	{
		szCmdLine += L"vstmididrv\\";
	}
	szCmdLine += (uPluginPlatform == 64) ? L"vsthost64.exe" : L"vsthost32.exe";
	szCmdLine += L"\" \"";
	szCmdLine += szPluginPath;
	szCmdLine += L"\" ";

	unsigned sum = 0;

	const TCHAR* ch = szPluginPath;
	while (*ch)
	{
		sum += (TCHAR)(*ch++ * 820109);
	}

	print_hex(sum, szCmdLine, 4);

	PROCESS_INFORMATION piProcInfo;
	STARTUPINFO siStartInfo = { 0 };

	siStartInfo.cb = sizeof(siStartInfo);
	siStartInfo.hStdInput = hChildStd_IN_Rd;
	siStartInfo.hStdOutput = hChildStd_OUT_Wr;
	siStartInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

	TCHAR CmdLine[MAX_PATH];
	_tcscpy_s(CmdLine, _countof(CmdLine), szCmdLine.c_str());

	/// Start the VST Host process with the VSTi
	if (!CreateProcess(NULL, CmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo))
	{
		process_terminate();
		return false;
	}

	// Close remote handles so pipes will break when process terminates
	CloseHandle(hChildStd_OUT_Wr);
	CloseHandle(hChildStd_IN_Rd);
	hChildStd_OUT_Wr = NULL;
	hChildStd_IN_Rd = NULL;

	hProcess = piProcInfo.hProcess;
	hThread = piProcInfo.hThread;

#ifdef NDEBUG
	SetPriorityClass(hProcess, GetPriorityClass(GetCurrentProcess()));
	SetThreadPriority(hThread, GetThreadPriority(GetCurrentThread()));
#endif

	uint32_t status = ReceiveData();

	if (status != 0)
	{
		process_terminate();
		*error = new uint32_t(status);
		return false;
	}

	uint32_t effectNameLength = ReceiveData();
	uint32_t vendorLength = ReceiveData();
	uint32_t productLength = ReceiveData();
	vendorVersion = ReceiveData();
	uniqueId = ReceiveData();
	audioOutputs = ReceiveData();

	delete[] effectName;
	delete[] vendor;
	delete[] product;

	effectName = new char[effectNameLength + 1];
	vendor = new char[vendorLength + 1];
	product = new char[productLength + 1];

	ReceiveData(effectName, effectNameLength);
	ReceiveData(vendor, vendorLength);
	ReceiveData(product, productLength);

	effectName[effectNameLength] = 0;
	vendor[vendorLength] = 0;
	product[productLength] = 0;

	return true;
}

void VSTDriver::process_terminate()
{
	if (isTerminating)
	{
		return;
	}

	isTerminating = true;

	if (hProcess)
	{
		SendData(Command::Exit);
		// TerminateProcess is asynchronous; it initiates termination and returns immediately.
		TerminateProcess(hProcess, 0);
		// If you need to be sure the process has terminated, call the WaitForSingleObject function with a handle to the process.
		WaitForSingleObject(hProcess, 5000);

		CloseHandle(hThread);
		hThread = NULL;

		CloseHandle(hProcess);
		hProcess = NULL;
	}
	if (hChildStd_IN_Rd)
	{
		CloseHandle(hChildStd_IN_Rd);
		hChildStd_IN_Rd = NULL;
	}
	if (hChildStd_IN_Wr)
	{
		CloseHandle(hChildStd_IN_Wr);
		hChildStd_IN_Wr = NULL;
	}
	if (hChildStd_OUT_Rd)
	{
		CloseHandle(hChildStd_OUT_Rd);
		hChildStd_OUT_Rd = NULL;
	}
	if (hChildStd_OUT_Wr)
	{
		CloseHandle(hChildStd_OUT_Wr);
		hChildStd_OUT_Wr = NULL;
	}
	if (hReadEvent)
	{
		CloseHandle(hReadEvent);
		hReadEvent = NULL;
	}
	if (isInitialized)
	{
		CoUninitialize();
		isInitialized = false;
	}
}

bool VSTDriver::process_running()
{
	return hProcess && WaitForSingleObject(hProcess, 0) == WAIT_TIMEOUT;
}

static void ProcessPendingMessages()
{
	MSG msg = {};
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		DispatchMessage(&msg);
	}
}

uint32_t VSTDriver::ReceivePipeData(void* out, uint32_t size)
{
	OVERLAPPED ol = {};
	ol.hEvent = hReadEvent;
	ResetEvent(hReadEvent);
	SetLastError(NO_ERROR);
	DWORD received;
	if (ReadFile(hChildStd_OUT_Rd, out, size, &received, &ol))
	{
		return received;
	}

	if (GetLastError() != ERROR_IO_PENDING)
	{
		return 0;
	}

	const HANDLE handles[1] = { hReadEvent };
	SetLastError(NO_ERROR);
	DWORD state;
	for (;;)
	{
		state = MsgWaitForMultipleObjects(_countof(handles), handles, FALSE, INFINITE, QS_ALLEVENTS);
		if (state == WAIT_OBJECT_0 + _countof(handles))
		{
			ProcessPendingMessages();
		}
		else
		{
			break;
		}
	}

	if (state == WAIT_OBJECT_0 && GetOverlappedResult(hChildStd_OUT_Rd, &ol, &received, TRUE))
	{
		return received;
	}

#if 0 && _WIN32_WINNT >= 0x600
	CancelIoEx(hChildStd_OUT_Rd, &ol);
#else
	CancelIo(hChildStd_OUT_Rd);
#endif

	return 0;
}

void VSTDriver::ReceiveData(void* out, uint32_t size)
{
	if (size && process_running())
	{
		uint8_t* ptr = (uint8_t*)out;
		uint32_t done = 0;
		while (done < size)
		{
			uint32_t received = ReceivePipeData(ptr + done, size - done);
			if (received == 0)
			{
				memset(out, 0xFF, size);
				break;
			}
			done += received;
		}
	}
	else
	{
		memset(out, 0xFF, size);
	}
}

uint32_t VSTDriver::ReceiveData()
{
	uint32_t code;
	ReceiveData(&code, sizeof(code));
	return code;
}

void VSTDriver::SendData(const void* in, uint32_t size)
{
	if (size && process_running())
	{
		DWORD bytesWritten;
		if (!WriteFile(hChildStd_IN_Wr, in, size, &bytesWritten, NULL) || bytesWritten < size)
		{
			process_terminate();
		}
	}
}

void VSTDriver::SendData(uint32_t code)
{
	SendData(&code, sizeof(code));
}

void VSTDriver::GetEffectName(std::string& out)
{
	out = effectName;
}

void VSTDriver::GetVendorString(std::string& out)
{
	out = vendor;
}

void VSTDriver::GetProductString(std::string& out)
{
	out = product;
}

/// <summary>
/// Get the VSTi vendor-specific version
/// </summary>
/// <returns>The VSTi vendor-specific version</returns>
long VSTDriver::GetVendorVersion()
{
	return vendorVersion;
}

/// <summary>
/// Get the unique id of the VSTi
/// </summary>
/// <returns>The unique id of the VSTi</returns>
long VSTDriver::GetUniqueID()
{
	return uniqueId;
}

void VSTDriver::CloseVSTDriver()
{
	SaveVstiSettings();
	process_terminate();

	if (szPluginPath)
	{
		free(szPluginPath);

		szPluginPath = NULL;
	}
}

bool VSTDriver::OpenVSTDriver(TCHAR* szPath, uint32_t** error, unsigned int sampleRate)
{
	CloseVSTDriver();

	InitializeVstiPath(szPath);

	if (!process_create(error))
	{
		return false;
	}

	if (!SetSampleRate(sampleRate))
	{
		return false;
	}

	if (!SetChunk(blChunk))
	{
		return false;
	}

	LoadVstiSettings();

	DisplayEditorModal();

	//timeSetEvent(1000, 10, (LPTIMECALLBACK)TimeProc, (DWORD)this, TIME_ONESHOT);

	return true;
}

void VSTDriver::GetChunk(vector<uint8_t>& out)
{
	SendData(Command::GetChunkData);

	if (ReceiveData())
	{
		process_terminate();
	}
	else
	{
		uint32_t size = ReceiveData();

		out.resize(size);

		ReceiveData(&out[0], size);
	}
}

bool VSTDriver::SetChunk(const void* in, unsigned size)
{
	SendData(Command::SetChunkData);
	SendData(size);
	SendData(in, size);
	if (ReceiveData())
	{
		process_terminate();
		return false;
	}
	return true;
}

bool VSTDriver::SetChunk(std::vector<std::uint8_t> blChunk)
{
	return SetChunk(blChunk.data(), blChunk.size());
}

bool VSTDriver::HasEditor()
{
	SendData(Command::VstiHasEditor);

	if (ReceiveData())
	{
		process_terminate();
		return false;
	}
	return ReceiveData();
}

void VSTDriver::DisplayEditorModal()
{
	SendData(Command::DisplayEditorModal);

	if (ReceiveData())
	{
		process_terminate();
	}
}

bool VSTDriver::SetSampleRate(uint32_t sampleRate)
{
	SendData(Command::SetSampleRate);
	SendData(sizeof(uint32_t));
	SendData(sampleRate);

	if (ReceiveData())
	{
		process_terminate();
		return false;
	}
	return true;
}

void VSTDriver::ResetDriver()
{
	SaveVstiSettings();

	SendData(Command::Reset);

	if (ReceiveData())
	{
		process_terminate();
	}
}

void VSTDriver::ProcessMIDIMessage(DWORD dwPort, DWORD dwParam1)
{
	dwParam1 = (dwParam1 & 0xFFFFFF) | (dwPort << 24);
	SendData(Command::SendMidiEvent);
	SendData(dwParam1);

	if (ReceiveData())
	{
		process_terminate();
	}
}

void VSTDriver::ProcessSysEx(DWORD dwPort, const unsigned char* sysexbuffer, int exlen)
{
	dwPort = (dwPort << 24) | (exlen & 0xFFFFFF);
	SendData(Command::SendMidiSystemExclusiveEvent);
	SendData(dwPort);
	SendData(sysexbuffer, exlen);

	if (ReceiveData())
	{
		process_terminate();
	}
}

void VSTDriver::RenderFloat(float* samples, int len, float volume)
{
	SendData(Command::RenderAudioSamples);
	SendData(len);

	if (ReceiveData())
	{
		process_terminate();
		memset(samples, 0, sizeof(*samples) * len * audioOutputs);
		return;
	}

	while (len)
	{
		unsigned len_to_do = len;
		if (len_to_do > 4096)
		{
			len_to_do = 4096;
		}
		ReceiveData(samples, sizeof(*samples) * len_to_do * audioOutputs);
		for (unsigned i = 0; i < len_to_do * audioOutputs; ++i)
		{
			samples[i] *= volume;
		}
		samples += len_to_do * audioOutputs;
		len -= len_to_do;
	}
}

void VSTDriver::Render(short* samples, int len, float volume)
{
	float* float_out = (float*)_alloca(512 * audioOutputs * sizeof(*float_out));
	while (len > 0)
	{
		int len_todo = len > 512 ? 512 : len;
		RenderFloat(float_out, len_todo, volume);
		for (unsigned i = 0; i < len_todo * audioOutputs; ++i)
		{
			int sample = (float_out[i] * 32768.f);
			if ((sample + 0x8000) & 0xFFFF0000)
			{
				sample = 0x7FFF ^ (sample >> 31);
			}
			samples[0] = sample;
			++samples;
		}
		len -= len_todo;
	}
}
