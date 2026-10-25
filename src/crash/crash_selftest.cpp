// wet-recomp - crash reporter self-test (does not load the game)
//
// Usage: crash_selftest <av|abort|terminate|purecall|overflow|cxx>
// Each mode crashes deliberately; check crashes/ next to the exe afterwards.

#include "crash/crash_handler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace {

struct Base {
  Base() { CallPure(); }
  virtual ~Base() = default;
  virtual void Pure() = 0;
  void CallPure() { Pure(); }
};
struct Derived : Base {
  void Pure() override {}
};

__declspec(noinline) void AccessViolation() {
  volatile int* p = reinterpret_cast<int*>(0x10);
  *p = 1;
}

__declspec(noinline) int Recurse(int n) {
  volatile char pad[4096];
  pad[0] = char(n);
  return Recurse(n + 1) + pad[0];
}

}  // namespace

int main(int argc, char** argv) {
  wet::crash::Install();
  const char* mode = argc > 1 ? argv[1] : "av";
  std::printf("crash_selftest: %s\n", mode);
  std::fflush(stdout);
  if (!std::strcmp(mode, "av")) AccessViolation();
  if (!std::strcmp(mode, "abort")) std::abort();
  if (!std::strcmp(mode, "terminate")) std::terminate();
  if (!std::strcmp(mode, "purecall")) Derived d;
  if (!std::strcmp(mode, "overflow")) return Recurse(0);
  if (!std::strcmp(mode, "cxx")) throw std::runtime_error("selftest");
  std::printf("unknown mode\n");
  return 1;
}
