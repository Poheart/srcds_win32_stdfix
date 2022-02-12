#include "SRCDSControl.h"
#include <iostream>
#include <vector>
#include <sstream>
#include <process.h>
#include <conio.h>

SRCDSControl *srcdsControl = nullptr;
HANDLE hReadThread = 0;

unsigned _stdcall HandleStdIn(void *arg)
{
	try
	{
		char chr[2];
		chr[1] = 0;

		while (cin.read(chr, 1))
		{

			if (chr[0] == '\r')
				cout << "\r";
			srcdsControl->WriteText(string(chr));

		}
	}
	catch (...)
	{
		// Ignore exception and just stop the thread.
	}

	_endthreadex(0);
	return 0;
}

BOOL CtrlHandler(DWORD fdwCtrlType)
{
	delete srcdsControl;
	if (hReadThread > 0)
		CloseHandle(hReadThread);
	return FALSE;
}

void HandleCommandLineDisplay(int screenSize)
{
	static string oldCmdline;
	string cmdline = srcdsControl->ReadText(screenSize - 1, screenSize - 1);
	if (!cmdline.empty() && cmdline.find_first_not_of(" ", 0) != string::npos && cmdline.compare(oldCmdline) != 0)
	{
		/*CONSOLE_SCREEN_BUFFER_INFO info;
		HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
		if (GetConsoleScreenBufferInfo(hout, &info))
		{
		COORD coord;
		coord.X = 0;
		coord.Y = info.dwSize.Y;
		DWORD lpNumberOfCharsWritten;
		WriteConsoleOutputCharacter(hout, cmdline.c_str(), cmdline.length(), coord, &lpNumberOfCharsWritten);
		}*/
		string realcmd = cmdline.substr(0, cmdline.find_last_not_of(" ") + 1);
		//cout << "\r" << realcmd << string(CON_LINE_LENGTH - realcmd.length(), ' ');
		cout << "\r" << realcmd;
	}

	oldCmdline = cmdline;
}

int main(int argc, char* argv[])
{
	// Forward all arguments to this program to srcds.
	ostringstream commandline;
	for (int i = 1; i < argc; i++)
	{
		commandline << argv[i] << " ";
	}

	// Stop srcds when stopping this program.
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);

	try
	{
		srcdsControl = new SRCDSControl();

		// Start the SRCDS
		srcdsControl->Start("srcds.exe", commandline.str());

		// Setup seperate input handling thread
		unsigned threadAddr;
		hReadThread = (HANDLE)_beginthreadex(NULL, 0, HandleStdIn, NULL, 0, &threadAddr);
		if (!hReadThread)
		{
			cerr << "Failed to create input thread. Won't process stdin." << endl;
		}

		vector<string> oldOutput;
		vector<string> outputBuffer;
		bool bJustStarted = true;

		while (1)
		{
			int screenSize = srcdsControl->GetScreenBufferSize();
			if (screenSize == -1)
				continue;

			// Make sure the window has 80 chars width again.
			if (!srcdsControl->SetScreenBufferSize(screenSize))
				cout << "Failed to set screen size to " << screenSize << "\n";

			// Skip the status line, so start at line 1 instead of 0.
			// Skip the command line as well.
			string output = srcdsControl->ReadText(1, screenSize - 2);

			outputBuffer.clear();
			// Read in the current output screen.
			string line;
			int lastNotEmptyIndex = -1;
			for (int i = 0; i < screenSize - 2; i++)
			{
				if (i * CON_LINE_LENGTH >= (int)output.length())
					break;

				line = output.substr(i * CON_LINE_LENGTH, CON_LINE_LENGTH);
				if (line.find_first_not_of(" ", 0) != string::npos)
					lastNotEmptyIndex = outputBuffer.size();

				outputBuffer.push_back(line);
			}

			// Remove all empty lines after the last non-empty one.
			if (lastNotEmptyIndex >= 0 && (int)outputBuffer.size() > lastNotEmptyIndex)
			{
				outputBuffer.resize(lastNotEmptyIndex + 1);
			}

			// Start tracking output.
			if (lastNotEmptyIndex != -1)
			{
				bJustStarted = false;
			}

			// Search for new lines.
			if (!oldOutput.empty())
			{
				int lastLine = oldOutput.size() - 1;
				int firstNewLine = outputBuffer.size() - 1;
				vector<string>::reverse_iterator oldIter;
				bool bCheckHistory = false;
				// Start from the newest line in the new buffer.
				for (auto iter = outputBuffer.rbegin(); iter != outputBuffer.rend(); iter++)
				{
					// Still searching for the last line we already printed.
					if (!bCheckHistory)
					{
						// This line is the last line in the old output.
						if (*iter == oldOutput[lastLine])
						{
							bCheckHistory = true;
							oldIter = iter;
							firstNewLine++;
						}
						else
						{
							firstNewLine--;
						}
					}
					else
					{
						// All the other previous lines have to match now.
						if (*iter != oldOutput[--lastLine])
						{
							// Try to find the last output line again.
							lastLine = oldOutput.size() - 1;
							// Start from the next line that we did before.
							iter = oldIter;
							firstNewLine -= 2;
							// Find the last line of the old output again.
							bCheckHistory = false;
						}
					}
				}


				if (firstNewLine < 0)
				{
					// No new line since last call.
					if (bCheckHistory)
					{
						HandleCommandLineDisplay(screenSize);
						continue;
					}
					// Console moved too fast and we missed something in between. Just startover now.
					else
					{
						firstNewLine = 0;
					}
				}

				// Print the newly added lines.
				for (unsigned int i = firstNewLine; i < outputBuffer.size(); i++)
				{
					oldOutput.push_back(outputBuffer[i]);
					cout << outputBuffer[i] << endl;
				}

				// Delete the first few elements, that are too much in the vector
				int sizeDiff = oldOutput.size() - screenSize;
				if (sizeDiff > 0)
				{
					oldOutput.erase(oldOutput.begin(), oldOutput.begin() + sizeDiff);
				}
			}
			else if (!bJustStarted)
			{
				for (auto iter = outputBuffer.begin(); iter != outputBuffer.end(); iter++)
				{
					oldOutput.push_back(*iter);
					cout << *iter << endl;
				}
			}

			// Get input command line
			HandleCommandLineDisplay(screenSize);
		}
	}
	catch (exception const& ex)
	{
		cerr << "Error: " << ex.what();
	}

	delete srcdsControl;

	// kill me
	TerminateProcess(GetCurrentProcess(), 0);
	return 0;
}