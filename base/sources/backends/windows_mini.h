#pragma once

typedef __int64          INT_PTR;
typedef unsigned __int64 UINT_PTR;
typedef __int64          LONG_PTR;
typedef unsigned __int64 ULONG_PTR;

typedef unsigned long  DWORD;
typedef unsigned short WORD;
typedef DWORD         *LPDWORD;
#define STD_OUTPUT_HANDLE ((DWORD) - 11)
#define STD_ERROR_HANDLE  ((DWORD) - 12)
#define WINAPI            __stdcall
typedef void        *HWND;
typedef void        *HANDLE;
typedef unsigned int UINT;
#define WINBASEAPI
typedef int BOOL;
#define CONST const
#define VOID  void
typedef void        *LPVOID;
typedef char         CHAR;
typedef const CHAR  *LPCSTR;
typedef wchar_t      WCHAR;
typedef const WCHAR *LPCWSTR;
typedef CONST CHAR  *LPCCH, *PCCH;
#define CP_UTF8        65001
#define FILE_TYPE_CHAR 0x0002
typedef wchar_t     WCHAR;
typedef WCHAR      *LPWSTR;
typedef void       *PVOID;
typedef long        LONG;
typedef LONG       *PLONG;
typedef CONST void *LPCVOID;

#define GENERIC_READ  (0x80000000L)
#define GENERIC_WRITE (0x40000000L)

#define FILE_SHARE_READ 0x00000001

#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3

#define FILE_ATTRIBUTE_HIDDEN    0x00000002
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_NORMAL    0x00000080
#define INVALID_FILE_ATTRIBUTES  ((DWORD) - 1)
#define INVALID_HANDLE_VALUE     ((HANDLE)(LONG_PTR) - 1)
#define FILE_BEGIN               0
#define FILE_CURRENT             1
#define MAX_PATH                 260

typedef struct _SECURITY_ATTRIBUTES {
	DWORD  nLength;
	LPVOID lpSecurityDescriptor;
	BOOL   bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct _OVERLAPPED {
	ULONG_PTR Internal;
	ULONG_PTR InternalHigh;
	union {
		struct {
			DWORD Offset;
			DWORD OffsetHigh;
		} DUMMYSTRUCTNAME;
		PVOID Pointer;
	} DUMMYUNIONNAME;

	HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

WINBASEAPI BOOL WINAPI   WriteConsoleA(HANDLE hConsoleOutput, CONST VOID *lpBuffer, DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten,
                                       LPVOID lpReserved);
WINBASEAPI BOOL WINAPI   WriteConsoleW(HANDLE hConsoleOutput, CONST VOID *lpBuffer, DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten,
                                       LPVOID lpReserved);
WINBASEAPI VOID WINAPI   OutputDebugStringA(LPCSTR lpOutputString);
WINBASEAPI VOID WINAPI   OutputDebugStringW(LPCWSTR lpOutputString);
WINBASEAPI HANDLE WINAPI GetStdHandle(DWORD nStdHandle);
int WINAPI               MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar);
int WINAPI               WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar, CHAR *lpMultiByteStr, int cbMultiByte,
                                             LPCCH lpDefaultChar, BOOL *lpUsedDefaultChar);
WINBASEAPI DWORD WINAPI  GetFileType(HANDLE hFile);
WINBASEAPI HANDLE WINAPI CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                                     DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
WINBASEAPI DWORD WINAPI  GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
WINBASEAPI DWORD WINAPI  GetFileAttributesA(LPCSTR lpFileName);
WINBASEAPI BOOL WINAPI   ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
WINBASEAPI DWORD WINAPI  SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod);
WINBASEAPI BOOL WINAPI   CloseHandle(HANDLE hObject);
WINBASEAPI BOOL WINAPI   WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
int WINAPI               MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType);

typedef unsigned char BYTE;
typedef BYTE         *LPBYTE;

#define INFINITE             0xFFFFFFFF
#define SW_HIDE              0
#define STARTF_USESHOWWINDOW 0x00000001
#define CREATE_NO_WINDOW     0x08000000

typedef struct _STARTUPINFOW {
	DWORD  cb;
	LPWSTR lpReserved;
	LPWSTR lpDesktop;
	LPWSTR lpTitle;
	DWORD  dwX;
	DWORD  dwY;
	DWORD  dwXSize;
	DWORD  dwYSize;
	DWORD  dwXCountChars;
	DWORD  dwYCountChars;
	DWORD  dwFillAttribute;
	DWORD  dwFlags;
	WORD   wShowWindow;
	WORD   cbReserved2;
	LPBYTE lpReserved2;
	HANDLE hStdInput;
	HANDLE hStdOutput;
	HANDLE hStdError;
} STARTUPINFOW, *LPSTARTUPINFOW;

typedef STARTUPINFOW   STARTUPINFO;
typedef LPSTARTUPINFOW LPSTARTUPINFO;

typedef struct _PROCESS_INFORMATION {
	HANDLE hProcess;
	HANDLE hThread;
	DWORD  dwProcessId;
	DWORD  dwThreadId;
} PROCESS_INFORMATION, *PPROCESS_INFORMATION, *LPPROCESS_INFORMATION;

WINBASEAPI DWORD WINAPI GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize);

WINBASEAPI BOOL WINAPI CreateProcessW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                      LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment,
                                      LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);

WINBASEAPI DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);

WINBASEAPI BOOL WINAPI GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode);
