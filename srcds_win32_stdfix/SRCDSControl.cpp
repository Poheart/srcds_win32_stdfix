#include "SRCDSControl.h"
#include <sstream>

SRCDSControl::SRCDSControl() :
	map_file_(INVALID_HANDLE_VALUE),
	event_parent_send_(INVALID_HANDLE_VALUE),
	event_child_send_(INVALID_HANDLE_VALUE)
{
	memset(&process_, 0, sizeof(PROCESS_INFORMATION));

	SECURITY_ATTRIBUTES secAttrb;
	secAttrb.nLength = sizeof(SECURITY_ATTRIBUTES);
	secAttrb.lpSecurityDescriptor = NULL;
	secAttrb.bInheritHandle = true;

	map_file_ = CreateFileMapping(INVALID_HANDLE_VALUE, &secAttrb, PAGE_READWRITE, 0, BUFFER_SIZE, NULL);

	if (map_file_ == NULL)
		throw exception("Could not create file mapping object", GetLastError());

	event_parent_send_ = CreateEvent(&secAttrb, false, false, NULL);
	if (event_parent_send_ == NULL)
		throw exception("Failed to create parent send event", GetLastError());

	event_child_send_ = CreateEvent(&secAttrb, false, false, NULL);
	if (event_child_send_ == NULL)
		throw exception("Failed to create child send event", GetLastError());
}

SRCDSControl::~SRCDSControl()
{
	if (map_file_ != INVALID_HANDLE_VALUE)
	{
		CloseHandle(map_file_);
		map_file_ = INVALID_HANDLE_VALUE;
	}

	if (event_parent_send_ != INVALID_HANDLE_VALUE)
	{
		CloseHandle(event_parent_send_);
		event_parent_send_ = INVALID_HANDLE_VALUE;
	}

	if (event_child_send_ != INVALID_HANDLE_VALUE)
	{
		CloseHandle(event_child_send_);
		event_child_send_ = INVALID_HANDLE_VALUE;
	}

	// Kill srcds as well when exiting this app.
	if (process_.dwProcessId != 0)
	{
		TerminateProcess(process_.hProcess, 1);
		WaitForSingleObject(process_.hProcess, INFINITE);
		memset(&process_, 0, sizeof(PROCESS_INFORMATION));
	}
}

void SRCDSControl::Start(string exe, string params)
{
	ostringstream commandline;
	// Build the basic params for console redirection.
	commandline << exe << " -HFILE " << (int)map_file_ << " -HPARENT " << (int)event_parent_send_ << " -HCHILD " << (int)event_child_send_ << " ";

	// Add other params
	commandline << params;

	STARTUPINFO startupInfo;
	memset(&startupInfo, 0, sizeof(startupInfo));
	startupInfo.cb = sizeof(STARTUPINFO);
	//startupInfo.lpTitle = &sTitle;

	BOOL success = CreateProcess(NULL, (LPSTR)commandline.str().c_str(), NULL, NULL, true, CREATE_NEW_CONSOLE, NULL, NULL, &startupInfo, &process_);
	if (!success)
		throw exception("Failed to create child process", GetLastError());

	// inject mixbot_watchdog
	string dll =
#ifdef _DEBUG
		"mixbot_watchdog_d.dll"
#else
		"mixbot_watchdog.dll"
#endif
		;

	LPVOID lpParam = VirtualAllocEx(process_.hProcess, NULL, dll.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	if (!lpParam)
		return;

	if (!WriteProcessMemory(process_.hProcess, lpParam, dll.c_str(), dll.size(), NULL))
		return;

	CreateRemoteThread(process_.hProcess, NULL, NULL, reinterpret_cast<LPTHREAD_START_ROUTINE>(LoadLibraryA), lpParam, NULL, NULL);
}

string SRCDSControl::ReadText(int iBeginLine, int iEndLine)
{
	lock_.lock();
	try
	{
		int *pBuf = (int *)GetMappedBuffer();
		pBuf[0] = CCOM_GET_TEXT;
		pBuf[1] = iBeginLine;
		pBuf[2] = iEndLine;
		ReleaseMappedBuffer(pBuf);
		SetEvent(event_parent_send_);

		// Wait for a response
		if (!WaitForResponse())
			throw exception("ReadText: WaitForMultipleObject failed", GetLastError());

		string output;
		pBuf = (int *)GetMappedBuffer();
		if (pBuf[0] == 1)
		{
			output = string((char *)(pBuf + 1));
		}
		ReleaseMappedBuffer(pBuf);
		lock_.unlock();
		return output;
	}
	catch (exception& ex)
	{
		lock_.unlock();
		throw ex;
	}
}

bool SRCDSControl::WriteText(string input)
{
	lock_.lock();
	try
	{
		// Setup command
		int *pBuf = (int *)GetMappedBuffer();
		pBuf[0] = CCOM_WRITE_TEXT;

		char *text = (char *)(pBuf + 1);
		strncpy_s(text, BUFFER_SIZE - sizeof(int), input.c_str(), input.length() + 1);

		ReleaseMappedBuffer(pBuf);
		SetEvent(event_parent_send_);

		// Wait for a response
		if (!WaitForResponse())
			throw exception("WriteText: WaitForMultipleObject failed", GetLastError());

		bool success = WasRequestSuccessful();
		lock_.unlock();
		return success;
	}
	catch (exception& ex)
	{
		lock_.unlock();
		throw ex;
	}
}

int SRCDSControl::GetScreenBufferSize()
{
	lock_.lock();
	try
	{
		// Request the screen line buffer size
		int *pBuf = (int *)GetMappedBuffer();
		pBuf[0] = CCOM_GET_SCR_LINES;
		ReleaseMappedBuffer(pBuf);
		SetEvent(event_parent_send_);

		// Wait for a response
		if (!WaitForResponse())
			throw exception("GetScreenBufferSize: WaitForMultipleObject failed", GetLastError());

		// Read the buffer size
		pBuf = (int *)GetMappedBuffer();
		int bufferSize = -1;
		if (pBuf[0] == 1)
			bufferSize = pBuf[1];

		ReleaseMappedBuffer(pBuf);
		lock_.unlock();
		return bufferSize;
	}
	catch (exception& ex)
	{
		lock_.unlock();
		throw ex;
	}
}

bool SRCDSControl::SetScreenBufferSize(int iLines)
{
	lock_.lock();
	try
	{
		// Set the screen buffer size
		int *pBuf = (int *)GetMappedBuffer();
		pBuf[0] = CCOM_SET_SCR_LINES;
		pBuf[1] = iLines;
		ReleaseMappedBuffer(pBuf);
		SetEvent(event_parent_send_);

		// Wait for a response
		if (!WaitForResponse())
			throw exception("SetScreenBufferSize: WaitForMultipleObject failed", GetLastError());

		bool success = WasRequestSuccessful();
		lock_.unlock();
		return success;
	}
	catch (exception& ex)
	{
		lock_.unlock();
		throw ex;
	}
}

bool SRCDSControl::WaitForResponse()
{
	HANDLE waitForEvents[2];
	waitForEvents[0] = event_child_send_;
	waitForEvents[1] = process_.hProcess;

	DWORD waitResult = WaitForMultipleObjects(2, waitForEvents, false, INFINITE);
	if (waitResult == (WAIT_OBJECT_0 + 1))
		throw exception("SRCDS process ended.");

	return waitResult == WAIT_OBJECT_0;
}

LPVOID SRCDSControl::GetMappedBuffer()
{
	LPVOID pBuf = (LPVOID)MapViewOfFile(map_file_, FILE_MAP_READ | FILE_MAP_WRITE, NULL, NULL, 0);
	if (!pBuf)
		throw exception("Failed to get view to our file mapping", GetLastError());
	return pBuf;
}

void SRCDSControl::ReleaseMappedBuffer(LPVOID pBuffer)
{
	UnmapViewOfFile(pBuffer);
}

bool SRCDSControl::WasRequestSuccessful()
{
	int *pBuf = (int *)GetMappedBuffer();
	bool success = pBuf[0] == 1;
	ReleaseMappedBuffer(pBuf);
	return success;
}