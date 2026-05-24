#ifndef DOOST_SAFE_MEMORY_HPP
#define DOOST_SAFE_MEMORY_HPP

#include <cstdint>
#include <optional>

namespace doost {
    [[nodiscard]] std::optional<std::uint8_t> safe_read_uint8(const std::uint8_t* p) noexcept;
} // namespace doost

#endif // DOOST_SAFE_MEMORY_HPP
