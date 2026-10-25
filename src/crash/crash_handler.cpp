// wet-recomp - in-process crash reporter (see crash_handler.h)

#include "crash/crash_handler.h"

#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>

#include <atomic>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

namespace wet::crash {
namespace {

constexpr DWORD kAbortExceptionCode = 0xE0000ABE;  // synthetic: abort/terminate/CRT errors
constexpr int kMaxHostFrames = 64;
constexpr int kMaxGuestFrames = 32;
constexpr int kKeepCrashFiles = 20;  // .txt + .dmp pairs kept is half of this

std::atomic<bool> g_installed{false};
std::atomic<LONG> g_in_crash{0};
wchar_t g_crash_dir[MAX_PATH];
const char* g_abort_reason = "abort()";

// The report is built in a static buffer: the heap may be what crashed.
char g_report[64 * 1024];
size_t g_report_len = 0;

void Out(const char* fmt, ...) {
  if (g_report_len >= sizeof(g_report) - 1) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(g_report + g_report_len, sizeof(g_report) - g_report_len, fmt, ap);
  va_end(ap);
  if (n > 0) g_report_len = std::min(g_report_len + size_t(n), sizeof(g_report) - 1);
}

const char* ExceptionName(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
    case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
    case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
    case 0xC0000409: return "STACK_BUFFER_OVERRUN / fail-fast";
    case 0xE06D7363: return "C++ exception";
    case kAbortExceptionCode: return g_abort_reason;
    default: return "unknown";
  }
}

bool Readable(const void* p, size_t n) {
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
  if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
  auto end = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
  return static_cast<const uint8_t*>(p) + n <= end;
}

uint8_t* GuestBase() {
  auto* ks = rex::system::kernel_state();
  return ks && ks->memory() ? ks->memory()->virtual_membase() : nullptr;
}

bool ReadGuest32(uint8_t* base, uint32_t addr, uint32_t* out) {
  if (!base || !addr) return false;
  const uint8_t* p = base + addr;
  if (!Readable(p, 4)) return false;
  *out = _byteswap_ulong(*reinterpret_cast<const uint32_t*>(p));
  return true;
}

void ReportHostStack(const CONTEXT* ctx_in) {
  Out("\n== Host stack ==\n");
  HANDLE process = GetCurrentProcess();
  CONTEXT ctx = *ctx_in;
  STACKFRAME64 frame{};
  frame.AddrPC.Offset = ctx.Rip;
  frame.AddrFrame.Offset = ctx.Rbp;
  frame.AddrStack.Offset = ctx.Rsp;
  frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;

  alignas(SYMBOL_INFO) char sym_buf[sizeof(SYMBOL_INFO) + 512];
  for (int i = 0; i < kMaxHostFrames; ++i) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &ctx,
                     nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
      break;
    DWORD64 pc = frame.AddrPC.Offset;
    if (!pc) break;

    char module[MAX_PATH] = "?";
    DWORD64 mod_base = SymGetModuleBase64(process, pc);
    if (mod_base) {
      GetModuleFileNameA(reinterpret_cast<HMODULE>(mod_base), module, sizeof(module));
      if (const char* slash = strrchr(module, '\\')) memmove(module, slash + 1, strlen(slash));
    }

    auto* sym = reinterpret_cast<SYMBOL_INFO*>(sym_buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 511;
    DWORD64 disp = 0;
    IMAGEHLP_LINE64 line{sizeof(line)};
    DWORD line_disp = 0;
    if (SymFromAddr(process, pc, &disp, sym)) {
      Out("  #%02d %016llX %s!%s+0x%llX", i, pc, module, sym->Name, disp);
      if (SymGetLineFromAddr64(process, pc, &line_disp, &line))
        Out("  (%s:%lu)", line.FileName, line.LineNumber);
      Out("\n");
    } else {
      Out("  #%02d %016llX %s+0x%llX\n", i, pc, module, pc - mod_base);
    }
  }
}

void ReportGuest() {
  auto* ts = rex::runtime::ThreadState::Get();
  if (!ts || !ts->context()) {
    Out("\n== Guest ==\n  (crashing thread is not a guest thread)\n");
    return;
  }
  const PPCContext& c = *ts->context();
  Out("\n== Guest registers (guest thread id %u) ==\n", ts->thread_id());
  const PPCRegister* gpr[32] = {&c.r0,  &c.r1,  &c.r2,  &c.r3,  &c.r4,  &c.r5,  &c.r6,  &c.r7,
                                &c.r8,  &c.r9,  &c.r10, &c.r11, &c.r12, &c.r13, &c.r14, &c.r15,
                                &c.r16, &c.r17, &c.r18, &c.r19, &c.r20, &c.r21, &c.r22, &c.r23,
                                &c.r24, &c.r25, &c.r26, &c.r27, &c.r28, &c.r29, &c.r30, &c.r31};
  for (int i = 0; i < 32; ++i)
    Out("  r%-2d %016llX%s", i, (unsigned long long)gpr[i]->u64, (i % 4 == 3) ? "\n" : "");
  Out("  lr  %016llX  ctr %016llX  msr %08X  last_indirect_target %08X\n",
      (unsigned long long)c.lr, (unsigned long long)c.ctr.u64, c.msr, c.last_indirect_target);
  Out("  xer so=%u ov=%u ca=%u  fpscr %08X\n", c.xer.so, c.xer.ov, c.xer.ca, c.fpscr.csr);
  const PPCCRRegister* cr[8] = {&c.cr0, &c.cr1, &c.cr2, &c.cr3, &c.cr4, &c.cr5, &c.cr6, &c.cr7};
  Out("  cr ");
  for (int i = 0; i < 8; ++i)
    Out(" cr%d[%c%c%c%c]", i, cr[i]->lt ? 'L' : '-', cr[i]->gt ? 'G' : '-',
        cr[i]->eq ? 'E' : '-', cr[i]->so ? 'S' : '-');
  Out("\n");
  const PPCRegister* fpr[32] = {&c.f0,  &c.f1,  &c.f2,  &c.f3,  &c.f4,  &c.f5,  &c.f6,  &c.f7,
                                &c.f8,  &c.f9,  &c.f10, &c.f11, &c.f12, &c.f13, &c.f14, &c.f15,
                                &c.f16, &c.f17, &c.f18, &c.f19, &c.f20, &c.f21, &c.f22, &c.f23,
                                &c.f24, &c.f25, &c.f26, &c.f27, &c.f28, &c.f29, &c.f30, &c.f31};
  for (int i = 0; i < 32; ++i)
    Out("  f%-2d %-14g%s", i, fpr[i]->f64, (i % 4 == 3) ? "\n" : "");

  // Guest back-chain: [sp] = caller sp; saved LR lives at caller_sp - 8 (Xbox 360 ABI).
  Out("\n== Guest stack (back-chain from r1) ==\n");
  uint8_t* base = GuestBase();
  uint32_t sp = c.r1.u32;
  Out("  #00 lr %08X  sp %08X\n", uint32_t(c.lr), sp);
  for (int i = 1; i < kMaxGuestFrames; ++i) {
    uint32_t caller_sp = 0, saved_lr = 0;
    if (!ReadGuest32(base, sp, &caller_sp) || caller_sp <= sp) break;
    if (!ReadGuest32(base, caller_sp - 8, &saved_lr)) break;
    Out("  #%02d lr %08X  sp %08X\n", i, saved_lr, caller_sp);
    sp = caller_sp;
  }
}

void TrimOldCrashes() {
  wchar_t pattern[MAX_PATH];
  swprintf(pattern, MAX_PATH, L"%s\\crash-*", g_crash_dir);
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  struct Entry { FILETIME t; wchar_t name[MAX_PATH]; };
  static Entry entries[512];
  int n = 0;
  do {
    if (n < 512) { entries[n].t = fd.ftLastWriteTime; wcscpy_s(entries[n].name, fd.cFileName); ++n; }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  // Oldest first, then delete until kKeepCrashFiles remain.
  for (int i = 0; i < n; ++i)
    for (int j = i + 1; j < n; ++j)
      if (CompareFileTime(&entries[j].t, &entries[i].t) < 0) std::swap(entries[i], entries[j]);
  for (int i = 0; i < n - kKeepCrashFiles; ++i) {
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s\\%s", g_crash_dir, entries[i].name);
    DeleteFileW(path);
  }
}

struct DumpJob {
  const wchar_t* path;
  EXCEPTION_POINTERS* ep;
  DWORD thread_id;
};

DWORD WINAPI DumpThread(void* param) {
  auto* job = static_cast<DumpJob*>(param);
  HANDLE file = CreateFileW(job->path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return 1;
  MINIDUMP_EXCEPTION_INFORMATION mei{job->thread_id, job->ep, FALSE};
  MINIDUMP_USER_STREAM stream{CommentStreamA, ULONG(g_report_len), g_report};
  MINIDUMP_USER_STREAM_INFORMATION streams{1, &stream};
  // Small dump: thread stacks + small windows of memory they point at; no full heap,
  // so the reserved guest address space does not end up in the file.
  auto type = MINIDUMP_TYPE(MiniDumpNormal | MiniDumpWithThreadInfo |
                            MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory);
  BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &mei,
                              &streams, nullptr);
  CloseHandle(file);
  return ok ? 0 : 2;
}

void WriteFileAll(const wchar_t* path, const char* data, size_t len) {
  HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD written;
  WriteFile(f, data, DWORD(len), &written, nullptr);
  CloseHandle(f);
}

void HandleCrash(EXCEPTION_POINTERS* ep) {
  if (g_in_crash.exchange(1)) {
    // Crashed while reporting (or a second thread crashed): just stop.
    TerminateProcess(GetCurrentProcess(), 0xDEAD);
  }

  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t stem[MAX_PATH], txt_path[MAX_PATH], dmp_path[MAX_PATH];
  swprintf(stem, MAX_PATH, L"%s\\crash-%04u%02u%02u-%02u%02u%02u-%lu", g_crash_dir, st.wYear,
           st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());
  swprintf(txt_path, MAX_PATH, L"%s.txt", stem);
  swprintf(dmp_path, MAX_PATH, L"%s.dmp", stem);

  const EXCEPTION_RECORD* er = ep->ExceptionRecord;
  Out("wet-recomp crash report\n");
  Out("time %04u-%02u-%02u %02u:%02u:%02u  pid %lu  tid %lu\n", st.wYear, st.wMonth, st.wDay,
      st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId(), GetCurrentThreadId());
  Out("exception 0x%08lX (%s) at %016llX\n", er->ExceptionCode, ExceptionName(er->ExceptionCode),
      (unsigned long long)(uintptr_t)er->ExceptionAddress);
  if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
    uint64_t addr = er->ExceptionInformation[1];
    const char* op = er->ExceptionInformation[0] == 0 ? "read" : er->ExceptionInformation[0] == 1
                                                               ? "write" : "execute";
    Out("  %s of %016llX", op, (unsigned long long)addr);
    if (uint8_t* base = GuestBase(); base && addr >= uint64_t(uintptr_t(base)) &&
                                     addr < uint64_t(uintptr_t(base)) + 0x100000000ull)
      Out("  (guest address %08X)", uint32_t(addr - uint64_t(uintptr_t(base))));
    Out("\n");
  }
  const CONTEXT* hc = ep->ContextRecord;
  Out("\n== Host registers ==\n");
  Out("  rax %016llX rbx %016llX rcx %016llX rdx %016llX\n", hc->Rax, hc->Rbx, hc->Rcx, hc->Rdx);
  Out("  rsi %016llX rdi %016llX rbp %016llX rsp %016llX\n", hc->Rsi, hc->Rdi, hc->Rbp, hc->Rsp);
  Out("  r8  %016llX r9  %016llX r10 %016llX r11 %016llX\n", hc->R8, hc->R9, hc->R10, hc->R11);
  Out("  r12 %016llX r13 %016llX r14 %016llX r15 %016llX\n", hc->R12, hc->R13, hc->R14, hc->R15);
  Out("  rip %016llX eflags %08lX\n", hc->Rip, hc->EFlags);

  ReportHostStack(hc);
  ReportGuest();

  WriteFileAll(txt_path, g_report, g_report_len);

  DumpJob job{dmp_path, ep, GetCurrentThreadId()};
  if (HANDLE t = CreateThread(nullptr, 0, DumpThread, &job, 0, nullptr)) {
    WaitForSingleObject(t, 60000);
    CloseHandle(t);
  }
  TrimOldCrashes();

  if (auto logger = rex::GetLogger()) {
    REXLOG_CRITICAL("Crash: exception 0x{:08X}; report written to crashes/", er->ExceptionCode);
    logger->flush();
  }

  if (GetEnvironmentVariableW(L"WET_CRASH_NO_DIALOG", nullptr, 0)) return;
  wchar_t msg[1024];
  swprintf(msg, 1024, L"Wet crashed (0x%08lX).\n\nReport:\n%s\n\nMinidump:\n%s", er->ExceptionCode,
           txt_path, dmp_path);
  MessageBoxW(nullptr, msg, L"wet-recomp crash", MB_OK | MB_ICONERROR | MB_TOPMOST);
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ep) {
  HandleCrash(ep);
  return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void CrashFromHere(const char* reason) {
  g_abort_reason = reason;
  CONTEXT ctx{};
  RtlCaptureContext(&ctx);
  EXCEPTION_RECORD er{};
  er.ExceptionCode = kAbortExceptionCode;
  er.ExceptionAddress = reinterpret_cast<void*>(ctx.Rip);
  EXCEPTION_POINTERS ep{&er, &ctx};
  HandleCrash(&ep);
  TerminateProcess(GetCurrentProcess(), 3);
  __assume(0);
}

void OnSigAbrt(int) { CrashFromHere("abort()"); }
void OnTerminate() { CrashFromHere("std::terminate"); }
void OnPureCall() { CrashFromHere("pure virtual call"); }
void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
  CrashFromHere("CRT invalid parameter");
}

}  // namespace

void Install() {
  if (g_installed.exchange(true)) return;

  // crashes/ next to the executable.
  GetModuleFileNameW(nullptr, g_crash_dir, MAX_PATH);
  if (wchar_t* slash = wcsrchr(g_crash_dir, L'\\')) *slash = 0;
  wcscat_s(g_crash_dir, L"\\crashes");
  CreateDirectoryW(g_crash_dir, nullptr);

  // Load symbols now; doing it inside the crash path is slow and allocates.
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
  SymInitialize(GetCurrentProcess(), nullptr, TRUE);

  // Keep a little stack in reserve so a stack overflow can still be reported.
  ULONG guarantee = 64 * 1024;
  SetThreadStackGuarantee(&guarantee);

  SetUnhandledExceptionFilter(UnhandledFilter);
  std::signal(SIGABRT, OnSigAbrt);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  std::set_terminate(OnTerminate);
  _set_purecall_handler(OnPureCall);
  _set_invalid_parameter_handler(OnInvalidParameter);
}

}  // namespace wet::crash
