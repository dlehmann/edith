#pragma once
// Shared type aliases used throughout the editor.

#include <chrono>
#include <string>

using u32 = std::u32string;  // text is stored as UTF-32 code points
using Clock = std::chrono::steady_clock;
