#include "doost/downward_pool.hpp"

#include <cerrno>
#include <limits>
#include <stdexcept>
#include <system_error>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace doost {
    namespace {
#if defined(MAP_ANONYMOUS)
        constexpr int kAnonymousMap = MAP_ANONYMOUS;
#elif defined(MAP_ANON)
        constexpr int kAnonymousMap = MAP_ANON;
#else
        constexpr int kAnonymousMap = 0;
#endif

        constexpr int kPoolProtection = PROT_READ | PROT_WRITE;
        constexpr int kPoolMapFlags = MAP_PRIVATE;
        constexpr int kGuardProtection = PROT_NONE;

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
            const std::size_t mask = multiple - 1;
            return (value + mask) & ~mask;
        }

    } // namespace

    DownwardPool::DownwardPool(std::size_t usable_bytes)
        : requested_bytes_(usable_bytes), page_size_(system_page_size()) {
        if (usable_bytes == 0) {
            usable_bytes = 1;
        }

        const std::size_t rounded_usable_bytes =
            round_up(usable_bytes, page_size_);

        if (rounded_usable_bytes >
            std::numeric_limits<std::size_t>::max() - page_size_) {
            throw std::length_error("pool mapping size overflows size_t");
        }
        mapping_bytes_ = rounded_usable_bytes + page_size_;

        void* mapping = mmap_anonymous(mapping_bytes_);
        if (mapping == MAP_FAILED) {
            throw std::system_error(errno, std::generic_category(), "mmap failed");
        }

        mapping_begin_ = static_cast<std::byte*>(mapping);
        cursor_ = mapping_begin_ + mapping_bytes_;

        if (mprotect(mapping_begin_, page_size_, kGuardProtection) != 0) {
            const int error = errno;
            munmap(mapping_begin_, mapping_bytes_);
            mapping_begin_ = nullptr;
            cursor_ = nullptr;
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

    void DownwardPool::release() noexcept {
        if (mapping_begin_ != nullptr) {
            munmap(mapping_begin_, mapping_bytes_);
        }

        mapping_begin_ = nullptr;
        cursor_ = nullptr;
        requested_bytes_ = 0;
        mapping_bytes_ = 0;
        page_size_ = 0;
    }

    void DownwardPool::move_from(DownwardPool& other) noexcept {
        mapping_begin_ = other.mapping_begin_;
        cursor_ = other.cursor_;
        requested_bytes_ = other.requested_bytes_;
        mapping_bytes_ = other.mapping_bytes_;
        page_size_ = other.page_size_;

        other.mapping_begin_ = nullptr;
        other.cursor_ = nullptr;
        other.requested_bytes_ = 0;
        other.mapping_bytes_ = 0;
        other.page_size_ = 0;
    }
} // namespace doost
