#pragma once

#include <cstddef>
#include <functional>

namespace modlock::gameinterop {

// BoundedReader and BoundedWriter reject inaccessible native memory instead of
// dereferencing it. Success means the entire requested region was copied.
using BoundedReader = std::function<bool(const void* source, void* out, size_t size)>;
using BoundedWriter = std::function<bool(void* target, const void* source, size_t size)>;

}  // namespace modlock::gameinterop
