#include "host_memory.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gufo::models::fish_drama3 {

void TrimResidentHostPages() noexcept {
#if defined(_WIN32)
  // (SIZE_T)-1 for both limits removes as many pages as possible from the
  // working set. Clean file-backed pages move to the standby list and are
  // re-read from the file if touched again, so this is safe at any point.
  (void)SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1),
                                 static_cast<SIZE_T>(-1));
#endif
}

}  // namespace gufo::models::fish_drama3
