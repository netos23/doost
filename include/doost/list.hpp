#ifndef DOOST_LIST_HPP
#define DOOST_LIST_HPP

#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace doost {
    template <class T, class Allocator = std::allocator<T>>
    class List {
        struct Node {
            template <class... Args>
            explicit Node(Node* next_node, Args&&... args)
                : next(next_node), value(std::forward<Args>(args)...) {}

            Node* next;
            T value;
        };

        using NodeAllocator =
        typename std::allocator_traits<Allocator>::template rebind_alloc<Node>;
        using NodeTraits = std::allocator_traits<NodeAllocator>;

    public:
        using value_type = T;
        using allocator_type = Allocator;
        using size_type = std::size_t;
        using reference = T&;
        using const_reference = const T&;

        static constexpr std::size_t node_size = sizeof(Node);
        static constexpr std::size_t node_alignment = alignof(Node);

        class const_iterator;

        class iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = T*;
            using reference = T&;

            iterator() = default;

            reference operator*() const noexcept {
                return node_->value;
            }

            pointer operator->() const noexcept {
                return std::addressof(node_->value);
            }

            iterator& operator++() noexcept {
                node_ = node_->next;
                return *this;
            }

            iterator operator++(int) noexcept {
                iterator copy = *this;
                ++(*this);
                return copy;
            }

            friend bool operator==(iterator lhs, iterator rhs) noexcept {
                return lhs.node_ == rhs.node_;
            }

            friend bool operator!=(iterator lhs, iterator rhs) noexcept {
                return !(lhs == rhs);
            }

        private:
            friend class List;
            friend class const_iterator;

            explicit iterator(Node* node) noexcept
                : node_(node) {}

            Node* node_ = nullptr;
        };

        class const_iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = const T*;
            using reference = const T&;

            const_iterator() = default;

            const_iterator(iterator it) noexcept
                : node_(it.node_) {}

            reference operator*() const noexcept {
                return node_->value;
            }

            pointer operator->() const noexcept {
                return std::addressof(node_->value);
            }

            const_iterator& operator++() noexcept {
                node_ = node_->next;
                return *this;
            }

            const_iterator operator++(int) noexcept {
                const_iterator copy = *this;
                ++(*this);
                return copy;
            }

            friend bool operator==(const_iterator lhs, const_iterator rhs) noexcept {
                return lhs.node_ == rhs.node_;
            }

            friend bool operator!=(const_iterator lhs, const_iterator rhs) noexcept {
                return !(lhs == rhs);
            }

        private:
            friend class List;

            explicit const_iterator(const Node* node) noexcept
                : node_(node) {}

            const Node* node_ = nullptr;
        };

        List() noexcept(std::is_nothrow_default_constructible_v<NodeAllocator>) = default;

        explicit List(const Allocator& allocator)
            : allocator_(allocator) {}

        List(const List& other)
            : allocator_(NodeTraits::select_on_container_copy_construction(
                other.allocator_)) {
            copy_from(other);
        }

        List(const List& other, const Allocator& allocator)
            : allocator_(allocator) {
            copy_from(other);
        }

        List(List&& other) noexcept
            : head_(std::exchange(other.head_, nullptr)),
              size_(std::exchange(other.size_, 0)),
              allocator_(std::move(other.allocator_)) {}

        ~List() {
            clear();
        }

        List& operator=(const List& other) {
            if (this == std::addressof(other)) {
                return *this;
            }

            List copy(other, get_allocator());
            swap_nodes(copy);
            return *this;
        }

        List& operator=(List&& other) {
            if (this == std::addressof(other)) {
                return *this;
            }

            if constexpr (NodeTraits::propagate_on_container_move_assignment::value) {
                clear();
                allocator_ = std::move(other.allocator_);
                steal_nodes_from(other);
            }
            else if (allocator_ == other.allocator_) {
                clear();
                steal_nodes_from(other);
            }
            else {
                List moved(get_allocator());
                moved.move_values_from(other);
                clear();
                swap_nodes(moved);
                other.clear();
            }

            return *this;
        }

        [[nodiscard]] allocator_type get_allocator() const noexcept {
            return allocator_type(allocator_);
        }

        template <class... Args>
        reference emplace_front(Args&&... args) {
            head_ = create_node(head_, std::forward<Args>(args)...);
            ++size_;
            return head_->value;
        }

        void push_front(const T& value) {
            emplace_front(value);
        }

        void push_front(T&& value) {
            emplace_front(std::move(value));
        }

        void pop_front() {
            if (head_ == nullptr) {
                throw std::out_of_range("pop_front on an empty List");
            }

            Node* old_head = head_;
            head_ = head_->next;
            --size_;
            destroy_node(old_head);
        }

        void clear() noexcept {
            while (head_ != nullptr) {
                Node* node = head_;
                head_ = head_->next;
                destroy_node(node);
            }
            size_ = 0;
        }

        // For arena-style allocators when the whole arena is released at once.
        void release_nodes() noexcept {
            head_ = nullptr;
            size_ = 0;
        }

        [[nodiscard]] reference front() {
            return head_->value;
        }

        [[nodiscard]] const_reference front() const {
            return head_->value;
        }

        [[nodiscard]] bool empty() const noexcept {
            return head_ == nullptr;
        }

        [[nodiscard]] size_type size() const noexcept {
            return size_;
        }

        [[nodiscard]] iterator begin() noexcept {
            return iterator(head_);
        }

        [[nodiscard]] iterator end() noexcept {
            return iterator();
        }

        [[nodiscard]] const_iterator begin() const noexcept {
            return const_iterator(head_);
        }

        [[nodiscard]] const_iterator end() const noexcept {
            return const_iterator();
        }

        [[nodiscard]] const_iterator cbegin() const noexcept {
            return begin();
        }

        [[nodiscard]] const_iterator cend() const noexcept {
            return end();
        }

        [[nodiscard]] static std::size_t required_storage(size_type node_count) {
            if (node_count > std::numeric_limits<std::size_t>::max() / node_size) {
                throw std::length_error("List node storage size overflows size_t");
            }
            return node_count * node_size;
        }

    private:
        template <class... Args>
        Node* create_node(Node* next, Args&&... args) {
            Node* node = NodeTraits::allocate(allocator_, 1);
            try {
                NodeTraits::construct(allocator_, node, next,
                                      std::forward<Args>(args)...);
            }
            catch (...) {
                NodeTraits::deallocate(allocator_, node, 1);
                throw;
            }
            return node;
        }

        void destroy_node(Node* node) noexcept {
            NodeTraits::destroy(allocator_, node);
            NodeTraits::deallocate(allocator_, node, 1);
        }

        void copy_from(const List& other) {
            Node** tail = &head_;
            try {
                for (Node* source = other.head_; source != nullptr;
                     source = source->next) {
                    Node* copy = create_node(nullptr, source->value);
                    *tail = copy;
                    tail = &copy->next;
                    ++size_;
                }
            }
            catch (...) {
                clear();
                throw;
            }
        }

        void move_values_from(List& other) {
            Node** tail = &head_;
            try {
                for (Node* source = other.head_; source != nullptr;
                     source = source->next) {
                    Node* copy = create_node(nullptr, std::move(source->value));
                    *tail = copy;
                    tail = &copy->next;
                    ++size_;
                }
            }
            catch (...) {
                clear();
                throw;
            }
        }

        void steal_nodes_from(List& other) noexcept {
            head_ = std::exchange(other.head_, nullptr);
            size_ = std::exchange(other.size_, 0);
        }

        void swap_nodes(List& other) noexcept {
            using std::swap;
            swap(head_, other.head_);
            swap(size_, other.size_);
        }

        Node* head_ = nullptr;
        size_type size_ = 0;
        [[no_unique_address]] NodeAllocator allocator_;
    };
} // namespace doost

#endif // DOOST_LIST_HPP
