#pragma once
#include <windows.h>
#include <exception>
#include <string>
#include <mutex>

using namespace std;

// Hardcoded to 80 in dedicated.dll
#define CON_LINE_LENGTH 80

#define BUFFER_SIZE 65536

#define CCOM_WRITE_TEXT		0x2
// Param1 : Text

#define CCOM_GET_TEXT		0x3
// Param1 : Begin line
// Param2 : End line

#define CCOM_GET_SCR_LINES	0x4
// No params

#define CCOM_SET_SCR_LINES	0x5
// Param1 : Number of lines

class SRCDSControl {
public:
	SRCDSControl();
	~SRCDSControl();

	void Start(string exe, string params);

	int GetScreenBufferSize();
	bool SetScreenBufferSize(int iLines);
	bool WriteText(string input);
	string ReadText(int iBeginLine, int iEndLine);

private:
	LPVOID GetMappedBuffer();
	void ReleaseMappedBuffer(LPVOID pBuffer);
	bool WaitForResponse();
	bool WasRequestSuccessful();

private:
	HANDLE map_file_;
	HANDLE event_parent_send_;
	HANDLE event_child_send_;
	PROCESS_INFORMATION process_;
	std::mutex lock_;
};