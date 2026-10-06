/*
 * KERNEL32.dll -- what this host implements of it.
 *
 * The largest surface: process, file, heap, thread, TLS, locale and
 * time. Implemented across kernel32.c and win32_sdl.c.
 *
 * ONE list, three expansions: the declarations, the table, and (for a name
 * this host spells differently from the DLL) the string the binder matches.
 * A stub renamed on one side and not the other fails to link.
 */
#include "host_imports.h"
#include "host_imports_surfaces.h"

#include "x86rt.h"

#define KERNEL32_IMPORTS(X, XN, XO)                                            \
  X(CloseHandle)                                                               \
  X(CompareStringA)                                                            \
  X(CompareStringW)                                                            \
  X(CreateDirectoryA)                                                          \
  X(CreateEventA)                                                              \
  X(CreateFileA)                                                               \
  X(CreateFileMappingA)                                                        \
  X(CreateFileW)                                                               \
  X(CreateMutexA)                                                              \
  X(CreateSemaphoreA)                                                          \
  X(DebugBreak)                                                                \
  X(DeleteCriticalSection)                                                     \
  X(DeleteFileA)                                                               \
  X(DisableThreadLibraryCalls)                                                 \
  X(DuplicateHandle)                                                           \
  X(EnterCriticalSection)                                                      \
  X(EnumSystemLocalesA)                                                        \
  X(ExitProcess)                                                               \
  X(FindClose)                                                                 \
  X(FindFirstFileA)                                                            \
  X(FindNextFileA)                                                             \
  X(FlushFileBuffers)                                                          \
  X(FormatMessageA)                                                            \
  X(FreeEnvironmentStringsA)                                                   \
  X(FreeEnvironmentStringsW)                                                   \
  X(FreeLibrary)                                                               \
  X(GetACP)                                                                    \
  X(GetCPInfo)                                                                 \
  X(GetCommandLineA)                                                           \
  X(GetCurrentProcess)                                                         \
  X(GetCurrentProcessId)                                                       \
  X(GetCurrentThread)                                                          \
  X(GetCurrentThreadId)                                                        \
  X(GetDiskFreeSpaceExA)                                                       \
  X(GetEnvironmentStrings)                                                     \
  X(GetEnvironmentStringsW)                                                    \
  X(GetFileAttributesA)                                                        \
  X(GetFileSize)                                                               \
  X(GetFileType)                                                               \
  X(GetFullPathNameA)                                                          \
  X(GetLastError)                                                              \
  X(GetLocaleInfoA)                                                            \
  X(GetLocaleInfoW)                                                            \
  X(GetModuleFileNameA)                                                        \
  X(GetModuleHandleA)                                                          \
  X(GetOEMCP)                                                                  \
  X(GetPriorityClass)                                                          \
  X(GetProcAddress)                                                            \
  X(GetProcessHeap)                                                            \
  X(GetProcessTimes)                                                           \
  X(GetStartupInfoA)                                                           \
  X(GetStdHandle)                                                              \
  X(GetStringTypeA)                                                            \
  X(GetStringTypeW)                                                            \
  X(GetSystemDirectoryA)                                                       \
  X(GetSystemInfo)                                                             \
  X(GetSystemTimeAsFileTime)                                                   \
  X(GetThreadPriority)                                                         \
  X(GetThreadTimes)                                                            \
  X(GetTickCount)                                                              \
  X(GetUserDefaultLCID)                                                        \
  X(GetVersion)                                                                \
  X(GetVersionExA)                                                             \
  X(GlobalMemoryStatus)                                                        \
  X(HeapAlloc)                                                                 \
  X(HeapCreate)                                                                \
  X(HeapDestroy)                                                               \
  X(HeapFree)                                                                  \
  X(HeapReAlloc)                                                               \
  X(HeapSize)                                                                  \
  X(InitializeCriticalSection)                                                 \
  X(InitializeCriticalSectionAndSpinCount)                                     \
  X(InterlockedDecrement)                                                      \
  X(InterlockedExchange)                                                       \
  X(InterlockedIncrement)                                                      \
  X(IsBadCodePtr)                                                              \
  X(IsBadReadPtr)                                                              \
  X(IsBadWritePtr)                                                             \
  X(IsDebuggerPresent)                                                         \
  X(IsProcessorFeaturePresent)                                                 \
  X(IsValidCodePage)                                                           \
  X(IsValidLocale)                                                             \
  X(LCMapStringA)                                                              \
  X(LCMapStringW)                                                              \
  X(LeaveCriticalSection)                                                      \
  X(LoadLibraryA)                                                              \
  X(LocalFree)                                                                 \
  X(MapViewOfFile)                                                             \
  X(MultiByteToWideChar)                                                       \
  X(OutputDebugStringA)                                                        \
  X(PulseEvent)                                                                \
  X(QueryPerformanceCounter)                                                   \
  X(QueryPerformanceFrequency)                                                 \
  X(RaiseException)                                                            \
  X(ReadFile)                                                                  \
  X(ReleaseMutex)                                                              \
  X(ReleaseSemaphore)                                                          \
  X(RemoveDirectoryA)                                                          \
  X(ResetEvent)                                                                \
  X(ResumeThread)                                                              \
  X(RtlUnwind)                                                                 \
  X(SetEndOfFile)                                                              \
  X(SetEnvironmentVariableA)                                                   \
  X(SetEvent)                                                                  \
  X(SetFilePointer)                                                            \
  X(SetHandleCount)                                                            \
  X(SetLastError)                                                              \
  X(SetPriorityClass)                                                          \
  X(SetStdHandle)                                                              \
  X(SetThreadAffinityMask)                                                     \
  X(SetThreadPriority)                                                         \
  X(SetThreadPriorityBoost)                                                    \
  X(SetUnhandledExceptionFilter)                                               \
  X(Sleep)                                                                     \
  X(SuspendThread)                                                             \
  X(TerminateProcess)                                                          \
  X(TlsAlloc)                                                                  \
  X(TlsFree)                                                                   \
  X(TlsGetValue)                                                               \
  X(TlsSetValue)                                                               \
  X(TryEnterCriticalSection)                                                   \
  X(UnmapViewOfFile)                                                           \
  X(VirtualAlloc)                                                              \
  X(VirtualFree)                                                               \
  X(VirtualQuery)                                                              \
  X(WaitForMultipleObjects)                                                    \
  X(WaitForSingleObject)                                                       \
  X(WideCharToMultiByte)                                                       \
  X(WriteFile)                                                                 \
  X(lstrlenA)

#define DECL(n) void imp_KERNEL32_##n(CPU *C);
#define DECL_N(s, n) void imp_KERNEL32_##n(CPU *C);
#define DECL_O(o, n) void imp_KERNEL32_##n(CPU *C);
KERNEL32_IMPORTS(DECL, DECL_N, DECL_O)

#define ENTRY(n) {#n, 0, imp_KERNEL32_##n},
#define ENTRY_N(s, n) {s, 0, imp_KERNEL32_##n},
#define ENTRY_O(o, n) {"#" #o, o, imp_KERNEL32_##n},
static const HostImport g_table[] = {KERNEL32_IMPORTS(ENTRY, ENTRY_N, ENTRY_O)};

void host_imports_register_kernel32(void) {
  host_imports_register("KERNEL32.dll", g_table,
                        sizeof g_table / sizeof g_table[0]);
}
