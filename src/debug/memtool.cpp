// wet-recomp - guest memory debug tool (off unless --wet_memtool=true)
//
// A background thread watches --wet_memtool_dir for request files:
//   dump.req          -> writes dump-<n>.bin (all committed guest pages, concatenated) and
//                        dump-<n>.idx (one "guest_start size file_offset" line per region)
//   poke.req          -> each line "<hex guest addr> f32|u32 <value>" is written to guest
//                        memory (big-endian, as the game sees it), once
//   profile.req       -> "<seconds> [thread name substring]": sampling profile of a host
//                        thread (default "GPU Commands") to profile-<n>.txt.
//   watch.req         -> "<hex guest addr> <hex size> <seconds>": data watchpoint. The host page
//                        is guarded; every access inside the range logs the reading/writing
//                        host function once (generated code is named sub_<guest address>)
// The request file is deleted when handled. Used to locate game variables by diffing.

#include "debug/memtool.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#include <algorithm>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <vector>

REXCVAR_DEFINE_BOOL(wet_memtool, false, "Wet/Debug", "Enable the guest memory dump/poke tool");
REXCVAR_DEFINE_STRING(wet_memtool_dir, "memtool", "Wet/Debug",
                      "Directory watched by the memory tool for dump.req / poke.req");

namespace wet::memtool {
namespace {

std::atomic<bool> g_started{false};

uint8_t* GuestBase() {
  auto* ks = rex::system::kernel_state();
  return ks && ks->memory() ? ks->memory()->virtual_membase() : nullptr;
}

void Dump(const std::filesystem::path& dir, int n) {
  uint8_t* base = GuestBase();
  if (!base) return;
  auto bin = dir / ("dump-" + std::to_string(n) + ".bin");
  auto idx = dir / ("dump-" + std::to_string(n) + ".idx");
  FILE* fb = _wfopen(bin.c_str(), L"wb");
  FILE* fi = _wfopen(idx.c_str(), L"w");
  if (!fb || !fi) return;
  uint64_t offset = 0, guest = 0;
  while (guest < 0x100000000ull) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(base + guest, &mbi, sizeof(mbi))) break;
    uint64_t region_end = uint64_t(static_cast<uint8_t*>(mbi.BaseAddress) - base) + mbi.RegionSize;
    if (region_end > 0x100000000ull) region_end = 0x100000000ull;
    bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
    if (readable) {
      uint64_t size = region_end - guest;
      std::fwrite(base + guest, 1, size, fb);
      std::fprintf(fi, "%08llX %llX %llX\n", (unsigned long long)guest, (unsigned long long)size,
                   (unsigned long long)offset);
      offset += size;
    }
    guest = region_end;
  }
  std::fclose(fb);
  std::fclose(fi);
  REXLOG_INFO("wet memtool: dump {} written ({} MB)", n, offset >> 20);
}

void Poke(const std::filesystem::path& req) {
  uint8_t* base = GuestBase();
  if (!base) return;
  std::ifstream in(req);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string addr_s, type, value;
    if (!(ss >> addr_s >> type >> value)) continue;
    uint32_t addr = uint32_t(std::stoul(addr_s, nullptr, 16));
    uint32_t bits = 0;
    if (type == "f32") {
      float f = std::stof(value);
      std::memcpy(&bits, &f, 4);
    } else {
      bits = uint32_t(std::stoul(value, nullptr, 0));
    }
    uint32_t be = _byteswap_ulong(bits);
    std::memcpy(base + addr, &be, 4);
    REXLOG_INFO("wet memtool: poke {:08X} = {} {}", addr, type, value);
  }
}

// --- data watchpoint (PAGE_GUARD + single-step re-arm) ---
std::atomic<uintptr_t> g_watch_lo{0}, g_watch_hi{0}, g_watch_page{0};
std::atomic<bool> g_watch_on{false};
thread_local bool t_rearm = false;
std::mutex g_watch_mutex;
std::set<DWORD64> g_watch_seen;

void ArmGuard() {
  DWORD old;
  VirtualProtect(reinterpret_cast<void*>(g_watch_page.load()), 4096, PAGE_READWRITE | PAGE_GUARD, &old);
}

LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* ep) {
  DWORD code = ep->ExceptionRecord->ExceptionCode;
  if (code == STATUS_GUARD_PAGE_VIOLATION && g_watch_on.load()) {
    uintptr_t addr = ep->ExceptionRecord->ExceptionInformation[1];
    if (addr >= g_watch_lo.load() && addr < g_watch_hi.load()) {
      DWORD64 rip = ep->ContextRecord->Rip;
      std::lock_guard lock(g_watch_mutex);
      if (g_watch_seen.insert(rip).second && g_watch_seen.size() < 200) {
        alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        const char* name = SymFromAddr(GetCurrentProcess(), rip, &disp, sym) ? sym->Name : "?";
        REXLOG_INFO("wet memtool: watch {} at +{:X} rip={:X} ({}+0x{:X})",
                    ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                    addr - g_watch_lo.load(), rip, name, disp);
      }
    }
    // Guard is consumed; single-step one instruction, then re-arm.
    t_rearm = true;
    ep->ContextRecord->EFlags |= 0x100;
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  if (code == STATUS_SINGLE_STEP && t_rearm) {
    t_rearm = false;
    if (g_watch_on.load()) ArmGuard();
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

void Watch(const std::filesystem::path& req) {
  uint8_t* base = GuestBase();
  if (!base) return;
  std::ifstream in(req);
  std::string a, n, secs;
  if (!(in >> a >> n >> secs)) return;
  uintptr_t lo = uintptr_t(base) + std::stoul(a, nullptr, 16);
  g_watch_lo = lo;
  g_watch_hi = lo + std::stoul(n, nullptr, 16);
  g_watch_page = lo & ~uintptr_t(4095);
  static void* handler = AddVectoredExceptionHandler(1, WatchHandler);
  (void)handler;
  REXLOG_INFO("wet memtool: watching guest {} size {} for {} s", a, n, secs);
  g_watch_on = true;
  ArmGuard();
  std::this_thread::sleep_for(std::chrono::seconds(std::stoi(secs)));
  g_watch_on = false;
  DWORD old;
  VirtualProtect(reinterpret_cast<void*>(g_watch_page.load()), 4096, PAGE_READWRITE, &old);
  REXLOG_INFO("wet memtool: watch ended");
}


// --- sampling profiler: profile.req "<seconds> [thread-name-substring]" ---
// Samples one host thread's stack (default: the SDK's "GPU Commands" thread) with
// SuspendThread + StackWalk64 about 1000 times per second and writes profile-<n>.txt with
// the hottest functions by self and by inclusive sample count.
typedef HRESULT(WINAPI* GetThreadDescriptionFn)(HANDLE, PWSTR*);

HANDLE FindThreadByName(const std::string& needle, std::string& found_name) {
  auto get_desc = reinterpret_cast<GetThreadDescriptionFn>(
      GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription"));
  if (!get_desc) return nullptr;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return nullptr;
  THREADENTRY32 te{sizeof(te)};
  HANDLE result = nullptr;
  const DWORD pid = GetCurrentProcessId();
  for (BOOL ok = Thread32First(snap, &te); ok && !result; ok = Thread32Next(snap, &te)) {
    if (te.th32OwnerProcessID != pid) continue;
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                          FALSE, te.th32ThreadID);
    if (!h) continue;
    PWSTR desc = nullptr;
    if (SUCCEEDED(get_desc(h, &desc)) && desc) {
      std::string name;
      for (PWSTR c = desc; *c; ++c) name.push_back(char(*c));
      LocalFree(desc);
      if (name.find(needle) != std::string::npos) {
        found_name = name;
        result = h;
        break;
      }
    }
    CloseHandle(h);
  }
  CloseHandle(snap);
  return result;
}

void Profile(const std::filesystem::path& dir, const std::filesystem::path& req, int n) {
  std::ifstream in(req);
  int seconds = 10;
  std::string needle = "GPU Commands";
  in >> seconds;
  std::string rest;
  std::getline(in, rest);
  if (!rest.empty()) {
    size_t a = rest.find_first_not_of(" 	");
    if (a != std::string::npos) needle = rest.substr(a);
  }
  std::string thread_name;
  HANDLE thread = FindThreadByName(needle, thread_name);
  if (!thread) {
    REXLOG_WARN("wet memtool: profile: no thread named like '{}'", needle);
    return;
  }
  REXLOG_INFO("wet memtool: profiling thread '{}' for {} s", thread_name, seconds);
  constexpr int kMaxFrames = 32;
  std::map<DWORD64, uint32_t> self_pc, incl_pc;
  uint64_t samples = 0, failed = 0;
  HANDLE process = GetCurrentProcess();
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  while (std::chrono::steady_clock::now() < end) {
    DWORD64 frames[kMaxFrames];
    int count = 0;
    if (SuspendThread(thread) != DWORD(-1)) {
      CONTEXT ctx{};
      ctx.ContextFlags = CONTEXT_FULL;
      if (GetThreadContext(thread, &ctx)) {
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = ctx.Rip;
        frame.AddrFrame.Offset = ctx.Rbp;
        frame.AddrStack.Offset = ctx.Rsp;
        frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
        while (count < kMaxFrames &&
               StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, nullptr,
                           SymFunctionTableAccess64, SymGetModuleBase64, nullptr) &&
               frame.AddrPC.Offset) {
          frames[count++] = frame.AddrPC.Offset;
        }
      }
      ResumeThread(thread);
    }
    if (count) {
      ++samples;
      ++self_pc[frames[0]];
      std::set<DWORD64> seen;
      for (int i = 0; i < count; ++i) {
        if (seen.insert(frames[i]).second) ++incl_pc[frames[i]];
      }
    } else {
      ++failed;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(900));
  }
  CloseHandle(thread);

  // The SDK DLL PDBs are not installed next to the DLLs; point dbghelp at the SDK build tree.
  {
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, sizeof(exe));
    std::filesystem::path exe_dir = std::filesystem::path(exe).parent_path();
    std::filesystem::path sdk_pdb = exe_dir / ".." / ".." / ".." / "third_party" / "rexglue-sdk" /
                                    "out" / "win-amd64" / "RelWithDebInfo";
    std::error_code ec;
    std::string path = exe_dir.string() + ";" + std::filesystem::canonical(sdk_pdb, ec).string();
    SymSetSearchPath(process, path.c_str());
    SymRefreshModuleList(process);
  }
  auto symbolize = [&](DWORD64 pc) {
    alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 512];
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 511;
    DWORD64 disp = 0;
    char module[MAX_PATH] = "?";
    DWORD64 mod_base = SymGetModuleBase64(process, pc);
    if (mod_base) {
      GetModuleFileNameA(reinterpret_cast<HMODULE>(mod_base), module, sizeof(module));
      if (const char* slash = strrchr(module, '\\')) memmove(module, slash + 1, strlen(slash));
    }
    std::string name = SymFromAddr(process, pc, &disp, sym) ? sym->Name : "?";
    return std::string(module) + "!" + name;
  };
  // Merge per-pc counts by symbol so a function's samples are not split across offsets,
  // but keep the inclusive count once per symbol per sample (approximated: inclusive per pc
  // summed can overcount recursion; acceptable for hot-spot ranking).
  std::map<std::string, uint32_t> self_sym, incl_sym;
  for (auto& [pc, c] : self_pc) self_sym[symbolize(pc)] += c;
  for (auto& [pc, c] : incl_pc) incl_sym[symbolize(pc)] += c;
  auto top = [&](std::map<std::string, uint32_t>& m, size_t limit) {
    std::vector<std::pair<uint32_t, std::string>> v;
    for (auto& [k, c] : m) v.emplace_back(c, k);
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    if (v.size() > limit) v.resize(limit);
    return v;
  };
  std::filesystem::path out_path = dir / ("profile-" + std::to_string(n) + ".txt");
  std::ofstream out(out_path);
  auto line = [&](uint32_t c, const std::string& k) {
    out << std::fixed << std::setprecision(1) << std::setw(6)
        << 100.0 * c / std::max<uint64_t>(1, samples) << "%  " << c << "  " << k << "\n";
  };
  out << "thread: " << thread_name << "  samples: " << samples << "  failed: " << failed
      << "  seconds: " << seconds << "\n\n== self (top of stack) ==\n";
  for (auto& [c, k] : top(self_sym, 60)) line(c, k);
  out << "\n== inclusive (anywhere on stack) ==\n";
  for (auto& [c, k] : top(incl_sym, 80)) line(c, k);
  REXLOG_INFO("wet memtool: profile written to {} ({} samples)", out_path.string(), samples);
}

void Worker(std::filesystem::path dir) {
  std::filesystem::create_directories(dir);
  int n = 0;
  while (true) {
    std::error_code ec;
    if (std::filesystem::exists(dir / "dump.req", ec)) {
      std::filesystem::remove(dir / "dump.req", ec);
      Dump(dir, n++);
    }
    if (std::filesystem::exists(dir / "watch.req", ec)) {
      std::filesystem::path req = dir / "watch.req.active";
      std::filesystem::rename(dir / "watch.req", req, ec);
      Watch(req);
      std::filesystem::remove(req, ec);
    }
    if (std::filesystem::exists(dir / "poke.req", ec)) {
      Poke(dir / "poke.req");
      std::filesystem::remove(dir / "poke.req", ec);
    }
    if (std::filesystem::exists(dir / "profile.req", ec)) {
      std::filesystem::path req = dir / "profile.req.active";
      std::filesystem::rename(dir / "profile.req", req, ec);
      Profile(dir, req, n++);
      std::filesystem::remove(req, ec);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

}  // namespace

void StartIfEnabled() {
  if (!REXCVAR_GET(wet_memtool) || g_started.exchange(true)) return;
  std::thread(Worker, std::filesystem::path(REXCVAR_GET(wet_memtool_dir))).detach();
  REXLOG_INFO("wet memtool: watching {}", REXCVAR_GET(wet_memtool_dir));
}

}  // namespace wet::memtool
