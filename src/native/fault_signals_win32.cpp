/* fault_signals_win32.cpp -- fatal exceptions and console stop events
 * delivered to the fault reporter.
 *
 * A vectored handler sees an exception before any frame-based handler, which
 * is what a POSIX signal handler gets and the only order that works for
 * translated code: JIT blocks carry no unwind data for the frame walk. */
#include "fault_platform.hpp"
#include "heartbeat.h"
#include "x2_log.h"
#include "x86rt_native.h"

#include "platform_posix.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

/* The stack a stack-overflow report runs on, reserved on the installing
 * thread as sigaltstack is. */
constexpr ULONG kOverflowReserve = 64 * 1024;

struct Classified {
  x2::fault::FaultKind kind;
  const char *meaning;
};

const char *access_meaning(const EXCEPTION_RECORD *record) {
  if (record->NumberParameters < 1) {
    return "a memory access fault";
  }
  switch (record->ExceptionInformation[0]) {
  case 0:
    return "a read of an address that is not mapped or not readable";
  case 1:
    return "a write to an address that is not mapped or not writable";
  case 8:
    return "an instruction fetch from memory that is not executable";
  default:
    return "a memory access fault";
  }
}

bool classify(const EXCEPTION_RECORD *record, Classified *out) {
  switch (record->ExceptionCode) {
  case EXCEPTION_ACCESS_VIOLATION:
    *out = {x2::fault::FaultKind::Segv, access_meaning(record)};
    return true;
  case EXCEPTION_STACK_OVERFLOW:
    *out = {x2::fault::FaultKind::Segv, "stack overflow"};
    return true;
  case EXCEPTION_IN_PAGE_ERROR:
    *out = {x2::fault::FaultKind::Bus, "the page could not be read in"};
    return true;
  case EXCEPTION_DATATYPE_MISALIGNMENT:
    *out = {x2::fault::FaultKind::Bus, "misaligned address"};
    return true;
  case EXCEPTION_ILLEGAL_INSTRUCTION:
    *out = {x2::fault::FaultKind::Ill,
            "illegal OPCODE -- the instruction at this address "
            "is not an instruction"};
    return true;
  case EXCEPTION_PRIV_INSTRUCTION:
    *out = {x2::fault::FaultKind::Ill, "privileged opcode"};
    return true;
  case EXCEPTION_INT_DIVIDE_BY_ZERO:
    *out = {x2::fault::FaultKind::Fpe, "integer divide by zero"};
    return true;
  case EXCEPTION_INT_OVERFLOW:
    *out = {x2::fault::FaultKind::Fpe, "integer overflow"};
    return true;
  case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    *out = {x2::fault::FaultKind::Fpe, "floating-point divide by zero"};
    return true;
  case EXCEPTION_FLT_INVALID_OPERATION:
    *out = {x2::fault::FaultKind::Fpe, "invalid floating-point operation"};
    return true;
  case EXCEPTION_FLT_DENORMAL_OPERAND:
  case EXCEPTION_FLT_INEXACT_RESULT:
  case EXCEPTION_FLT_OVERFLOW:
  case EXCEPTION_FLT_STACK_CHECK:
  case EXCEPTION_FLT_UNDERFLOW:
    *out = {x2::fault::FaultKind::Fpe, "an arithmetic fault"};
    return true;
  case EXCEPTION_BREAKPOINT:
  case EXCEPTION_SINGLE_STEP:
    *out = {x2::fault::FaultKind::Trap,
            "a trap instruction (INT3/INT1) executed with no "
            "debugger to take it"};
    return true;
  default:
    return false;
  }
}

LONG CALLBACK on_exception(EXCEPTION_POINTERS *info) {
  const EXCEPTION_RECORD *record = info->ExceptionRecord;
  Classified classified;
  if (!classify(record, &classified)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  uintptr_t address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
  int code = 0;
  if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
       record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
      record->NumberParameters >= 2) {
    code = static_cast<int>(record->ExceptionInformation[0]);
    address = static_cast<uintptr_t>(record->ExceptionInformation[1]);
  }
  const x2::fault::Fault fault = {
      classified.kind,
      classified.meaning,
      static_cast<int>(record->ExceptionCode),
      code,
      address,
      static_cast<uintptr_t>(info->ContextRecord->Rip)};
  x2::fault::fault_report(&fault);
}

constexpr char kInterruptedMessage[] =
    "\n*** x2native was INTERRUPTED -- it did not stop on its own.\n"
    "    Nothing below is a failure the run reported; this is where it\n"
    "    HAPPENED TO BE. A run that has to be killed is usually spinning:\n"
    "    read the ring below for a repeating pair of bodies.\n";

/* Windows runs a console control handler on a thread of its own, so the
 * reports can be written from ordinary context; the sleep is the backstop
 * if the heartbeat never gets there. */
BOOL WINAPI on_console_stop(DWORD event) {
  if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT &&
      event != CTRL_CLOSE_EVENT) {
    return FALSE;
  }
  (void)write(2, kInterruptedMessage, sizeof kInterruptedMessage - 1);
  if (heartbeat_running()) {
    x2_report_now = 1;
    Sleep(10000);
    _exit(4);
  }
  x86_diag_dump();
  _exit(4);
}

} // namespace

namespace x2::fault {

bool install_fatal_handlers() {
  if (!AddVectoredExceptionHandler(1, on_exception)) {
    x2_log_error("x2native: could not install the fault reporter; a fault "
                 "will die silently\n");
    return false;
  }
  return true;
}

bool install_handlers() {
  ULONG reserve = kOverflowReserve;
  if (!SetThreadStackGuarantee(&reserve)) {
    x2_log_error("x2native: no stack reserved for an overflow report; a "
                 "stack overflow will die silently\n");
  }
  const bool installed = install_fatal_handlers();
  if (!SetConsoleCtrlHandler(on_console_stop, TRUE)) {
    x2_log_error("x2native: no console stop handler; an interrupted run "
                 "will not report where it was\n");
  }
  return installed;
}

void trigger(FaultKind kind, bool genuine) {
  if (genuine && kind == FaultKind::Ill) {
    __asm__ __volatile__("ud2");
  }
  switch (kind) {
  case FaultKind::Ill:
    RaiseException(EXCEPTION_ILLEGAL_INSTRUCTION, 0, 0, nullptr);
    break;
  case FaultKind::Fpe:
    RaiseException(EXCEPTION_INT_DIVIDE_BY_ZERO, 0, 0, nullptr);
    break;
  case FaultKind::Bus:
    RaiseException(EXCEPTION_DATATYPE_MISALIGNMENT, 0, 0, nullptr);
    break;
  case FaultKind::Trap:
    RaiseException(EXCEPTION_BREAKPOINT, 0, 0, nullptr);
    break;
  default: {
    const ULONG_PTR access[2] = {0, 0x10};
    RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 2, access);
    break;
  }
  }
}

bool run_child(int selftest_case, char *output, std::size_t capacity,
               ChildResult *result) {
  char executable[MAX_PATH];
  char command[MAX_PATH + 64];
  if (GetModuleFileNameA(nullptr, executable, sizeof executable) == 0 ||
      snprintf(command, sizeof command,
               "\"%s\" --no-window --fault-selftest-child=%d", executable,
               selftest_case) >= static_cast<int>(sizeof command)) {
    return false;
  }
  SECURITY_ATTRIBUTES inherit = {sizeof inherit, nullptr, TRUE};
  HANDLE read_end = nullptr;
  HANDLE write_end = nullptr;
  if (!CreatePipe(&read_end, &write_end, &inherit, 0)) {
    return false;
  }
  (void)SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOA startup = {};
  startup.cb = sizeof startup;
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = write_end;
  startup.hStdError = write_end;
  PROCESS_INFORMATION process = {};
  const BOOL started =
      CreateProcessA(executable, command, nullptr, nullptr, TRUE, 0, nullptr,
                     nullptr, &startup, &process);
  CloseHandle(write_end);
  if (!started) {
    CloseHandle(read_end);
    return false;
  }
  std::size_t got = 0;
  for (;;) {
    char sink[4096];
    DWORD n = 0;
    const bool into_output = got < capacity - 1;
    if (!ReadFile(read_end, into_output ? output + got : sink,
                  into_output ? static_cast<DWORD>(capacity - 1 - got)
                              : static_cast<DWORD>(sizeof sink),
                  &n, nullptr) ||
        n == 0) {
      break;
    }
    if (into_output) {
      got += n;
    }
  }
  output[got] = 0;
  CloseHandle(read_end);
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD status = 0;
  GetExitCodeProcess(process.hProcess, &status);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  result->bytes = got;
  result->exited = true;
  result->status = static_cast<int>(status);
  return true;
}

} // namespace x2::fault
