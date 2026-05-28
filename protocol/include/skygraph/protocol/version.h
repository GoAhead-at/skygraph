#pragma once

#include <cstdint>

namespace skygraph::protocol {

// Bump on every wire-incompatible change. The viewer refuses to render data
// from a plugin whose major differs; on minor mismatch the viewer warns but
// continues (additive-only changes are allowed to bump minor).
inline constexpr std::uint32_t kProtocolMajor = 1;
inline constexpr std::uint32_t kProtocolMinor = 0;

inline constexpr const char* kProductName = "skygraph";
inline constexpr std::uint32_t kProductVersionMajor = 0;
inline constexpr std::uint32_t kProductVersionMinor = 1;
inline constexpr std::uint32_t kProductVersionPatch = 0;

}  // namespace skygraph::protocol
