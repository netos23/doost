#include "doost/string.hpp"

#include <cstddef>
#include <cstring>
#include <iostream>
#include <new>
#include <ostream>
#include <utility>

namespace doost {
namespace {
    std::ostream*& debug_trace_stream() noexcept {
        static std::ostream* stream = nullptr;
        return stream;
    }

    void print_escaped(std::ostream& output, const char* value) {
        for (const char* cursor = value; *cursor != '\0'; ++cursor) {
            switch (*cursor) {
            case '\\':
                output << "\\\\";
                break;
            case '"':
                output << "\\\"";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                output << *cursor;
                break;
            }
        }
    }
} // namespace

struct String::Block {
    String* first;
    char data[1];
};

String::String(const char* value) {
    if (value != nullptr) {
        attach(make_block(value, std::strlen(value)));
    }
}

String::String(std::string_view value) {
    attach(make_block(value.data(), value.size()));
}

String::String(const std::string& value) {
    attach(make_block(value.data(), value.size()));
}

String::String(const String& other) noexcept {
    attach(other.block_.pointer());
}

String::String(String&& other) noexcept {
    move_from(other);
}

String::~String() {
    detach();
}

String& String::operator=(const char* value) {
    if (value == nullptr) {
        reset();
        return *this;
    }

    String replacement(value);
    swap(replacement);
    return *this;
}

String& String::operator=(std::string_view value) {
    String replacement(value);
    swap(replacement);
    return *this;
}

String& String::operator=(const std::string& value) {
    String replacement(value);
    swap(replacement);
    return *this;
}

String& String::operator=(const String& other) noexcept {
    if (this == &other || block_.pointer() == other.block_.pointer()) {
        return *this;
    }

    Block* new_block = other.block_.pointer();
    detach();
    attach(new_block);
    return *this;
}

String& String::operator=(String&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    detach();
    move_from(other);
    return *this;
}

void String::reset() noexcept {
    detach();
}

void String::swap(String& other) noexcept {
    if (this == &other) {
        return;
    }

    Block* this_block = block_.pointer();
    Block* other_block = other.block_.pointer();
    if (this_block == other_block) {
        return;
    }

    const TaggedBlockPointer this_tag = block_;
    const TaggedBlockPointer other_tag = other.block_;
    String* this_previous = previous_;
    String* this_next = next_;
    String* other_previous = other.previous_;
    String* other_next = other.next_;

    replace_owner(this_block, this_previous, this_next, &other);
    replace_owner(other_block, other_previous, other_next, this);

    block_ = other_tag;
    previous_ = other_previous;
    next_ = other_next;

    other.block_ = this_tag;
    other.previous_ = this_previous;
    other.next_ = this_next;
}

bool String::has_value() const noexcept {
    return block_.pointer() != nullptr;
}

bool String::empty() const noexcept {
    return c_str()[0] == '\0';
}

std::size_t String::size() const noexcept {
    return std::strlen(c_str());
}

bool String::unique() const noexcept {
    return block_.pointer() != nullptr && block_.unique();
}

bool String::uniqueness_bit() const noexcept {
    return unique();
}

const char* String::c_str() const noexcept {
    const Block* block = block_.pointer();
    return block == nullptr ? "" : block->data;
}

std::string String::value() const {
    return std::string(c_str());
}

std::string_view String::view() const noexcept {
    const char* value = c_str();
    return std::string_view(value, std::strlen(value));
}

std::ostream* String::set_debug_trace(std::ostream* output) noexcept {
    std::ostream*& stream = debug_trace_stream();
    std::ostream* previous = stream;
    stream = output;
    return previous;
}

void String::attach(Block* block) noexcept {
    block_.set(block, false);
    previous_ = nullptr;
    next_ = nullptr;

    if (block == nullptr) {
        return;
    }

    String* old_first = block->first;
    next_ = old_first;
    if (old_first != nullptr) {
        old_first->previous_ = this;
        old_first->block_.set_unique(false);
    }

    block->first = this;
    block_.set_unique(old_first == nullptr);
}

void String::detach() noexcept {
    Block* block = block_.pointer();
    if (block == nullptr) {
        return;
    }

    if (previous_ != nullptr) {
        previous_->next_ = next_;
    }
    else {
        block->first = next_;
    }

    if (next_ != nullptr) {
        next_->previous_ = previous_;
    }

    block_.reset();
    previous_ = nullptr;
    next_ = nullptr;

    if (block->first == nullptr) {
        if constexpr (string_debug_trace_enabled) {
            if (std::ostream* stream = debug_trace_stream()) {
                *stream << "String release: \"";
                print_escaped(*stream, block->data);
                *stream << "\"\n";
            }
        }
        ::operator delete(block);
        return;
    }

    if (block->first->next_ == nullptr) {
        block->first->block_.set_unique(true);
    }
}

void String::move_from(String& other) noexcept {
    block_ = other.block_;
    other.block_.reset();
    previous_ = std::exchange(other.previous_, nullptr);
    next_ = std::exchange(other.next_, nullptr);

    Block* block = block_.pointer();
    if (block == nullptr) {
        return;
    }

    replace_owner(block, previous_, next_, this);
}

String::Block* String::make_block(const char* value, std::size_t length) {
    static_assert(alignof(Block) >= 2);
    static_assert(offsetof(Block, data) == sizeof(String*));

    const std::size_t bytes = offsetof(Block, data) + length + 1;
    auto* block = static_cast<Block*>(::operator new(bytes));
    block->first = nullptr;
    if (length != 0) {
        std::memcpy(block->data, value, length);
    }
    block->data[length] = '\0';
    return block;
}

void String::replace_owner(Block* block, String* previous, String* next,
                           String* replacement) noexcept {
    if (block == nullptr) {
        return;
    }

    if (previous != nullptr) {
        previous->next_ = replacement;
    }
    else {
        block->first = replacement;
    }

    if (next != nullptr) {
        next->previous_ = replacement;
    }
}

void swap(String& lhs, String& rhs) noexcept {
    lhs.swap(rhs);
}

std::ostream& operator<<(std::ostream& output, const String& pointer) {
    output << "String{unique=" << (pointer.unique() ? 1 : 0) << ", value=";
    if (pointer.block_.pointer() == nullptr) {
        return output << "<null>}";
    }

    output << '"';
    print_escaped(output, pointer.c_str());
    return output << "\"}";
}

bool operator==(const String& lhs, const String& rhs) noexcept {
    return std::strcmp(lhs.c_str(), rhs.c_str()) == 0;
}

bool operator==(const String& lhs, std::string_view rhs) noexcept {
    const char* value = lhs.c_str();
    const std::size_t length = std::strlen(value);
    return length == rhs.size() &&
           (length == 0 || std::memcmp(value, rhs.data(), length) == 0);
}

bool operator==(std::string_view lhs, const String& rhs) noexcept {
    return rhs == lhs;
}

bool operator<(const String& lhs, const String& rhs) noexcept {
    return std::strcmp(lhs.c_str(), rhs.c_str()) < 0;
}

} // namespace doost
