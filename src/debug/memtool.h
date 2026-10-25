// wet-recomp - guest memory debug tool (see memtool.cpp)

#pragma once

namespace wet::memtool {

// Start the request-file watcher thread when --wet_memtool is set.
void StartIfEnabled();

}  // namespace wet::memtool
