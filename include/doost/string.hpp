#ifndef DOOST_STRING_HPP
#define DOOST_STRING_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <string>
#include <string_view>

namespace doost {
#if defined(NDEBUG)
    inline constexpr bool string_debug_trace_enabled = false;
#else
    inline constexpr bool string_debug_trace_enabled = true;
#endif

    class String {
    public:
        String() noexcept = default;
        String(const char* value);
        String(std::string_view value);
        String(const std::string& value);
        String(const String& other) noexcept;
        String(String&& other) noexcept;

        ~String();

        String& operator=(const char* value);
        String& operator=(std::string_view value);
        String& operator=(const std::string& value);
        String& operator=(const String& other) noexcept;
        String& operator=(String&& other) noexcept;

        void reset() noexcept;
        void swap(String& other) noexcept;

        [[nodiscard]] bool has_value() const noexcept;
        [[nodiscard]] bool empty() const noexcept;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] bool unique() const noexcept;
        [[nodiscard]] bool uniqueness_bit() const noexcept;
        [[nodiscard]] const char* c_str() const noexcept;
        [[nodiscard]] std::string value() const;
        [[nodiscard]] std::string_view view() const noexcept;

        static std::ostream* set_debug_trace(std::ostream* output) noexcept;

        friend void swap(String& lhs, String& rhs) noexcept;
        friend std::ostream& operator<<(std::ostream& output,
                                        const String& pointer);
        friend bool operator==(const String& lhs,
                               const String& rhs) noexcept;
        friend bool operator==(const String& lhs,
                               std::string_view rhs) noexcept;
        friend bool operator==(std::string_view lhs,
                               const String& rhs) noexcept;
        friend bool operator<(const String& lhs,
                              const String& rhs) noexcept;

    private:
        struct Block {
            String* first;
        };

        struct TaggedBlockPointer {
            [[nodiscard]] Block* pointer() const noexcept {
                return reinterpret_cast<Block*>(tagged_address & kAddressMask);
            }

            [[nodiscard]] bool unique() const noexcept {
                return (tagged_address & kUniqueMask) != 0;
            }

            void set(Block* block, bool unique_block) noexcept {
                tagged_address = reinterpret_cast<std::uintptr_t>(block);
                set_unique(unique_block);
            }

            void set_unique(bool unique_block) noexcept {
                if (unique_block) {
                    tagged_address |= kUniqueMask;
                } else {
                    tagged_address &= kAddressMask;
                }
            }

            void reset() noexcept {
                tagged_address = 0;
            }

            static constexpr std::uintptr_t kUniqueMask = 1U;
            static constexpr std::uintptr_t kAddressMask = ~kUniqueMask;

            std::uintptr_t tagged_address = 0;
        };

        static_assert(sizeof(TaggedBlockPointer) == sizeof(std::uintptr_t));

        void attach(Block* block) noexcept;
        void detach() noexcept;
        void move_from(String& other) noexcept;

        static Block* make_block(const char* value, std::size_t length);
        static char* block_data(Block* block) noexcept;
        static const char* block_data(const Block* block) noexcept;
        static void replace_owner(Block* block, String* previous, String* next,
                                  String* replacement) noexcept;

        TaggedBlockPointer block_;
        String* previous_ = nullptr;
        String* next_ = nullptr;
    };

    static_assert(sizeof(String) == 3 * sizeof(void*));

    inline String::String(const std::string& value) {
        attach(make_block(value.data(), value.size()));
    }

    inline String::String(const String& other) noexcept {
        attach(other.block_.pointer());
    }

    inline String::String(String&& other) noexcept {
        move_from(other);
    }

    inline String& String::operator=(const std::string& value) {
        return *this = std::string_view(value.data(), value.size());
    }

    inline void String::reset() noexcept {
        detach();
    }

    [[nodiscard]] inline bool String::has_value() const noexcept {
        return block_.pointer() != nullptr;
    }

    [[nodiscard]] inline bool String::empty() const noexcept {
        return c_str()[0] == '\0';
    }

    [[nodiscard]] inline std::size_t String::size() const noexcept {
        return std::strlen(c_str());
    }

    [[nodiscard]] inline bool String::unique() const noexcept {
        return block_.pointer() != nullptr && block_.unique();
    }

    [[nodiscard]] inline bool String::uniqueness_bit() const noexcept {
        return unique();
    }

    [[nodiscard]] inline const char* String::c_str() const noexcept {
        const Block* block = block_.pointer();
        return block == nullptr ? "" : block_data(block);
    }

    [[nodiscard]] inline std::string String::value() const {
        return {c_str()};
    }

    [[nodiscard]] inline std::string_view String::view() const noexcept {
        const char* value = c_str();
        return std::string_view(value, std::strlen(value));
    }

    inline void swap(String& lhs, String& rhs) noexcept {
        lhs.swap(rhs);
    }

    [[nodiscard]] inline bool operator==(const String& lhs,
                                         const String& rhs) noexcept {
        return std::strcmp(lhs.c_str(), rhs.c_str()) == 0;
    }

    [[nodiscard]] inline bool operator==(const String& lhs,
                                         std::string_view rhs) noexcept {
        const char* value = lhs.c_str();
        const std::size_t length = std::strlen(value);
        return length == rhs.size() &&
            (length == 0 || std::memcmp(value, rhs.data(), length) == 0);
    }

    [[nodiscard]] inline bool operator==(std::string_view lhs,
                                         const String& rhs) noexcept {
        return rhs == lhs;
    }

    [[nodiscard]] inline bool operator<(const String& lhs,
                                        const String& rhs) noexcept {
        return std::strcmp(lhs.c_str(), rhs.c_str()) < 0;
    }
} // namespace doost

#endif // DOOST_STRING_HPP
