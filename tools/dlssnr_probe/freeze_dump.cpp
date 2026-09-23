// What a frozen player is waiting for.
//
// Run it while the player hangs: it finds the process, asks Windows what each thread
// is waiting on -- a critical section, a message sent to another thread, a handle --
// and walks every stack, with the filter's own names where its pdb is beside its .ax.
// A cycle in a wait chain is a deadlock, and the chain names both ends of it.
//
//   freeze_dump.exe                 the first player it finds
//   freeze_dump.exe 1234            that process id
//   freeze_dump.exe mpc-be64.exe    that name
//
// The report goes to the screen and to freeze_dump.txt beside the program.

#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

#include "freeze_report.inl"

static DWORD FindProcess(const wchar_t* wanted)
{
	static const wchar_t* players[] = { L"mpc-be64.exe", L"mpc-be.exe", L"mpc-hc64.exe", L"mpc-hc.exe", L"playback_test.exe" };

	const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}
	DWORD found = 0;
	PROCESSENTRY32W entry = { sizeof(entry) };
	for (BOOL ok = Process32FirstW(snapshot, &entry); ok && !found; ok = Process32NextW(snapshot, &entry)) {
		if (wanted) {
			if (!_wcsicmp(entry.szExeFile, wanted)) {
				found = entry.th32ProcessID;
			}
		} else {
			for (const wchar_t* player : players) {
				if (!_wcsicmp(entry.szExeFile, player)) {
					found = entry.th32ProcessID;
					break;
				}
			}
		}
	}
	CloseHandle(snapshot);
	return found;
}

int wmain(int argc, wchar_t* argv[])
{
	setvbuf(stdout, nullptr, _IONBF, 0);

	DWORD pid = 0;
	if (argc > 1) {
		pid = _wtoi(argv[1]);
		if (!pid) {
			pid = FindProcess(argv[1]);
		}
	} else {
		pid = FindProcess(nullptr);
	}
	if (!pid) {
		printf("no player found. Give a process id or a name.\n");
		return 1;
	}

	wchar_t exe[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring dir = exe;
	dir = dir.substr(0, dir.find_last_of(L'\\'));
	_wfopen_s(&g_freezeFile, (dir + L"\\freeze_dump.txt").c_str(), L"w");

	// The filter's own pdb sits beside its .ax; the rest of the frames name their module.
	const std::wstring search = dir + L"\\..\\..\\_bin\\Filter_x64;" + dir;
	char searchAnsi[2048] = {};
	WideCharToMultiByte(CP_ACP, 0, search.c_str(), -1, searchAnsi, sizeof(searchAnsi), nullptr, nullptr);

	FreezeSay("Process %lu\n\n", pid);
	FreezeReport(pid, 0, searchAnsi);

	if (g_freezeFile) {
		fclose(g_freezeFile);
	}
	printf("\nWritten to freeze_dump.txt\n");
	return 0;
}
