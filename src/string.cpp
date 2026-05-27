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

    String::String(const char* value) {
        if (value != nullptr) {
            attach(make_block(value, std::strlen(value)));
        }
    }

    String::String(std::string_view value) {
        attach(make_block(value.data(), value.size()));
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
                    print_escaped(*stream, block_data(block));
                    *stream << "\"\n";
                }
            }
            auto* storage = reinterpret_cast<char*>(block);
            block->~Block();
            delete[] storage;
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

        const std::size_t bytes = sizeof(Block) + length + 1;
        auto* storage = new char[bytes];
        auto* block = new (storage) Block{nullptr};
        char* data = block_data(block);
        if (length != 0) {
            std::memcpy(data, value, length);
        }
        data[length] = '\0';
        return block;
    }

    char* String::block_data(Block* block) noexcept {
        return reinterpret_cast<char*>(block + 1);
    }

    const char* String::block_data(const Block* block) noexcept {
        return reinterpret_cast<const char*>(block + 1);
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

    std::ostream& operator<<(std::ostream& output, const String& pointer) {
        output << "String{unique=" << (pointer.unique() ? 1 : 0) << ", value=";
        if (pointer.block_.pointer() == nullptr) {
            return output << "<null>}";
        }

        output << '"';
        print_escaped(output, pointer.c_str());
        return output << "\"}";
    }
} // namespace doost
