#include "doost/downward_pool.hpp"

#include <cerrno>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace doost {
    namespace {
#if defined(MAP_GROWSDOWN)
        constexpr int kMapGrowDown = MAP_GROWSDOWN;
#else
        constexpr int kMapGrowDown = 0;
#endif

#if defined(PROT_GROWSDOWN)
        constexpr int kProtGrowDown = PROT_GROWSDOWN;
#else
        constexpr int kProtGrowDown = 0;
#endif

#if defined(MAP_ANONYMOUS)
        constexpr int kAnonymousMap = MAP_ANONYMOUS;
#elif defined(MAP_ANON)
        constexpr int kAnonymousMap = MAP_ANON;
#else
        constexpr int kAnonymousMap = 0;
#endif

        constexpr int kPoolProtection = PROT_READ | PROT_WRITE;
        constexpr int kPoolMapFlags = MAP_PRIVATE | kMapGrowDown;
        constexpr int kGuardProtection = PROT_NONE | kProtGrowDown;

        void* mmap_anonymous(std::size_t bytes) {
#if defined(MAP_ANONYMOUS) || defined(MAP_ANON)
            void* mapping = mmap(nullptr, bytes, kPoolProtection,
                                 kPoolMapFlags | kAnonymousMap, -1, 0);
#else
            const int fd = open("/dev/zero", O_RDWR);
            if (fd < 0) {
                throw std::system_error(errno, std::generic_category(),
                                        "open(/dev/zero) failed");
            }
            void* mapping =
                mmap(nullptr, bytes, kPoolProtection, kPoolMapFlags, fd, 0);
            const int saved_errno = errno;
            close(fd);
            errno = saved_errno;
#endif

            return mapping;
        }

        std::size_t system_page_size() {
            errno = 0;
            const long value = sysconf(_SC_PAGESIZE);
            if (value <= 0) {
                const int error = errno == 0 ? EINVAL : errno;
                throw std::system_error(error, std::generic_category(),
                                        "sysconf(_SC_PAGESIZE) failed");
            }
            return static_cast<std::size_t>(value);
        }

        std::size_t round_up(std::size_t value, std::size_t multiple) {
            const std::size_t remainder = value % multiple;
            if (remainder == 0) {
                return value;
            }

            const std::size_t increment = multiple - remainder;
            if (value > std::numeric_limits<std::size_t>::max() - increment) {
                throw std::length_error("pool size overflows size_t");
            }
            return value + increment;
        }

        bool is_power_of_two(std::size_t value) {
            return value != 0 && (value & (value - 1)) == 0;
        }
    } // namespace

    DownwardPool::DownwardPool(std::size_t usable_bytes)
        : requested_bytes_(usable_bytes), page_size_(system_page_size()) {
        if (usable_bytes == 0) {
            usable_bytes = 1;
        }

        usable_bytes_ = round_up(usable_bytes, page_size_);
        guard_bytes_ = page_size_;

        if (usable_bytes_ >
            std::numeric_limits<std::size_t>::max() - guard_bytes_) {
            throw std::length_error("pool mapping size overflows size_t");
        }
        mapping_bytes_ = usable_bytes_ + guard_bytes_;

        void* mapping = mmap_anonymous(mapping_bytes_);
        if (mapping == MAP_FAILED) {
            throw std::system_error(errno, std::generic_category(), "mmap failed");
        }

        mapping_begin_ = static_cast<std::byte*>(mapping);
        lower_bound_ = mapping_begin_ + guard_bytes_;
        upper_bound_ = mapping_begin_ + mapping_bytes_;
        cursor_ = upper_bound_;

        if (mprotect(mapping_begin_, guard_bytes_, kGuardProtection) != 0) {
            const int error = errno;
            munmap(mapping_begin_, mapping_bytes_);
            mapping_begin_ = nullptr;
            lower_bound_ = nullptr;
            cursor_ = nullptr;
            upper_bound_ = nullptr;
            throw std::system_error(error, std::generic_category(),
                                    "mprotect failed");
        }
    }

    DownwardPool::~DownwardPool() {
        release();
    }

    DownwardPool::DownwardPool(DownwardPool&& other) noexcept {
        move_from(other);
    }

    DownwardPool& DownwardPool::operator=(DownwardPool&& other) noexcept {
        if (this != &other) {
            release();
            move_from(other);
        }
        return *this;
    }

    void* DownwardPool::allocate(std::size_t bytes, std::size_t alignment) {
        if (mapping_begin_ == nullptr) {
            throw std::bad_alloc();
        }
        if (!is_power_of_two(alignment)) {
            throw std::invalid_argument("allocation alignment must be a power of two");
        }
        if (bytes == 0) {
            bytes = 1;
        }

        const auto current = reinterpret_cast<std::uintptr_t>(cursor_);
        const std::uintptr_t raw = current - bytes;
        const std::uintptr_t aligned =
            raw & ~(static_cast<std::uintptr_t>(alignment) - 1U);

        cursor_ = reinterpret_cast<std::byte*>(aligned);
        return cursor_;
    }

    void DownwardPool::deallocate(void*, std::size_t, std::size_t) noexcept {}

    void DownwardPool::reset() noexcept {
        cursor_ = upper_bound_;
    }

    void DownwardPool::release() noexcept {
        if (mapping_begin_ != nullptr) {
            munmap(mapping_begin_, mapping_bytes_);
        }

        mapping_begin_ = nullptr;
        lower_bound_ = nullptr;
        cursor_ = nullptr;
        upper_bound_ = nullptr;
        requested_bytes_ = 0;
        usable_bytes_ = 0;
        guard_bytes_ = 0;
        mapping_bytes_ = 0;
        page_size_ = 0;
    }

    std::size_t DownwardPool::requested_bytes() const noexcept {
        return requested_bytes_;
    }

    std::size_t DownwardPool::usable_bytes() const noexcept {
        return usable_bytes_;
    }

    std::size_t DownwardPool::used_bytes() const noexcept {
        if (cursor_ == nullptr) {
            return 0;
        }
        return static_cast<std::size_t>(upper_bound_ - cursor_);
    }

    std::size_t DownwardPool::remaining_bytes() const noexcept {
        if (cursor_ == nullptr) {
            return 0;
        }
        return static_cast<std::size_t>(cursor_ - lower_bound_);
    }

    std::size_t DownwardPool::guard_bytes() const noexcept {
        return guard_bytes_;
    }

    std::size_t DownwardPool::page_size() const noexcept {
        return page_size_;
    }

    void DownwardPool::move_from(DownwardPool& other) noexcept {
        mapping_begin_ = other.mapping_begin_;
        lower_bound_ = other.lower_bound_;
        cursor_ = other.cursor_;
        upper_bound_ = other.upper_bound_;
        requested_bytes_ = other.requested_bytes_;
        usable_bytes_ = other.usable_bytes_;
        guard_bytes_ = other.guard_bytes_;
        mapping_bytes_ = other.mapping_bytes_;
        page_size_ = other.page_size_;

        other.mapping_begin_ = nullptr;
        other.lower_bound_ = nullptr;
        other.cursor_ = nullptr;
        other.upper_bound_ = nullptr;
        other.requested_bytes_ = 0;
        other.usable_bytes_ = 0;
        other.guard_bytes_ = 0;
        other.mapping_bytes_ = 0;
        other.page_size_ = 0;
    }
} // namespace doost
