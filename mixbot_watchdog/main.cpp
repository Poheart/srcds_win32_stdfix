struct IUnknown; // Workaround for "combaseapi.h(229): error C2187: syntax error: 'identifier' was unexpected here" when using /permissive-

#include <stdio.h>
#include <windows.h>
#include <tlhelp32.h>

int get_ppid() 
{
	int pid = GetCurrentProcessId();
	int ppid = -1;
	HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 pe = { 0 };
	pe.dwSize = sizeof(PROCESSENTRY32);

	if (Process32First(h, &pe)) {
		do {
			if (pe.th32ProcessID == pid) {
				ppid = pe.th32ParentProcessID;
			}
		} while (Process32Next(h, &pe));
	}

	CloseHandle(h);

	return ppid;
}

void main_thread()
{
	int ppid = get_ppid();

	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, false, ppid);

	if (hProcess == INVALID_HANDLE_VALUE)
		return;

	DWORD exitCode = STILL_ACTIVE;

	while (exitCode == STILL_ACTIVE)
	{
		if (!GetExitCodeProcess(hProcess, &exitCode))
			break;

		Sleep(500);
	}

	if (exitCode == STILL_ACTIVE)
		exitCode = 128;

	ExitProcess(exitCode);
}

BOOL WINAPI DllMain(
	_In_ HINSTANCE hinstDLL,
	_In_ DWORD     fdwReason,
	_In_ LPVOID    lpvReserved
)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hinstDLL);
		CreateThread(NULL, NULL, reinterpret_cast<LPTHREAD_START_ROUTINE>(main_thread), NULL, NULL, NULL);
	}

	return TRUE;
}