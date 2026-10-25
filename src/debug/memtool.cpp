// wet-recomp - guest memory debug tool (off unless --wet_memtool=true)
//
// A background thread watches --wet_memtool_dir for request files:
//   dump.req          -> writes dump-<n>.bin (all committed guest pages, concatenated) and
//                        dump-<n>.idx (one "guest_start size file_offset" line per region)
//   poke.req          -> each line "<hex guest addr> f32|u32 <value>" is written to guest
//                        memory (big-endian, as the game sees it), once
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

#include <mutex>
#include <set>

REXCVAR_DEFINE_BOOL(wet_memtool, false, "Wet/Debug", "Enable the guest memory dump/poke tool");
REXCVAR_DEFINE_STRING(wet_memtool_dir, "E:/WET/work/memtool", "Wet/Debug",
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
