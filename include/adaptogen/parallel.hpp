#pragma once
#include <cstddef>
#include <functional>

namespace adaptogen {

/// Run fn(i) for i in [0, n) on up to `threads` threads (0 = hardware concurrency).
void parallel_for(std::size_t n, int threads, const std::function<void(std::size_t)>& fn);

}  // namespace adaptogen
