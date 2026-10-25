// wet-recomp - in-process crash reporter
//
// On an unhandled exception, abort(), std::terminate, pure virtual call or CRT
// invalid-parameter error, writes next to the executable:
//   crashes/crash-<time>-<pid>.txt  symbolized host stack + guest PPC registers
//                                   + guest back-chain walk
//   crashes/crash-<time>-<pid>.dmp  small minidump (stacks, thread info, the
//                                   text report as a comment stream; no full heap)

#pragma once

namespace wet::crash {

// Install all hooks. Safe to call more than once; only the first call acts.
void Install();

}  // namespace wet::crash
