#ifndef SBMR_DETAIL_LIST_HPP
#define SBMR_DETAIL_LIST_HPP


#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>

#include <sbmr/_detail/assert.hpp>


namespace sbmr::_detail {


    // union with single uninitialized field
    // which must be manually constructed and destroyed

    template <class T>
        requires std::is_object_v<T>
    union _list_uninit
    {
        // default constructible and destructible
        constexpr _list_uninit()  noexcept = default;
        constexpr ~_list_uninit() noexcept = default;

        // holder is not copyable or movable
        _list_uninit(const _list_uninit&) = delete;
        _list_uninit(_list_uninit&&)      = delete;

        // holder is not copy or move assignable
        auto& operator=(const _list_uninit&) = delete;
        auto& operator=(_list_uninit&&)      = delete;

        // uninitialized field
        [[no_unique_address]] T value;
    };


    // simple doubly linked node
    // constructors are present because std::construct_at doesn't seem to work
    //   with aggregate types very well

    template <class T>
        requires std::is_object_v<T>
    struct _list_node
    {
        // default constructor (all other constructors are deleted)
        constexpr _list_node() noexcept = default;

        // data members
        _list_uninit<T> holder;
        _list_node *next;
        _list_node *prev;
    };


    // bidirectional iterator base type
    // wrapper around a pointer to a node
    // use template parameter to specify constness rather than two types
    // needs crtp since ++/-- need to return original type

    template <class T, class Derived, bool is_const>
        requires std::is_object_v<T>  // must be referencable
    class _list_iterator_crtp
    {
        // we need to friend ourselves for templated comparison operators
        template <class T_, class Derived_, bool is_const_>
        friend class _list_iterator_crtp;

    public:

        // member types
        using value_type = T;
        using pointer    = std::conditional_t<is_const, const T*, T*>;
        using reference  = std::conditional_t<is_const, const T&, T&>;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::bidirectional_iterator_tag;

    protected:

        // member types
        using node_type    = _list_node<T>;
        using node_pointer = std::conditional_t<is_const, const node_type *, node_type *>;

        // data member
        node_pointer m_node = nullptr;

        // default constructor creates end() iterator
        constexpr _list_iterator_crtp() noexcept = default;

        // construct from node pointer
        constexpr explicit _list_iterator_crtp(node_pointer node) noexcept
            : m_node{node}
        {}

    private:

        // access this as derived type
        constexpr Derived&
        self() noexcept
        {
            return static_cast<Derived&>(*this);
        }

    public:

        // pre-increment
        // advance to next node or to end()
        constexpr Derived&
        operator++() noexcept
        {
            SBMR_ASSERT_CONSTEXPR(m_node != nullptr);

            m_node = m_node->next;
            return self();
        }

        // post-increment
        // advance to next node or to end()
        constexpr Derived
        operator++(int) noexcept
        {
            SBMR_ASSERT_CONSTEXPR(m_node != nullptr);

            auto copy = self();
            m_node = m_node->next;
            return copy;
        }

        // pre-decrement
        // retreat to previous node
        constexpr Derived&
        operator--() noexcept
        {
            SBMR_ASSERT_CONSTEXPR(m_node != nullptr);

            m_node = m_node->prev;
            return self();
        }

        // post-decrement
        // retreat to previous node
        constexpr Derived
        operator--(int) noexcept
        {
            SBMR_ASSERT_CONSTEXPR(m_node != nullptr);

            auto copy = self();
            m_node = m_node->prev;
            return copy;
        }

        // dereference
        // returns (const if is_const) reference to node's value
        constexpr reference
        operator*() const noexcept
        {
            SBMR_ASSERT_CONSTEXPR(m_node != nullptr);

            return m_node->holder.value;
        }

        // pointer member access
        // returns pointer to value
        constexpr pointer
        operator->() const noexcept
        {
            SBMR_ASSERT_CONSTEXPR(m_node != nullptr);

            return std::addressof(m_node->holder.value);
        }

        // equality comparison with self
        // compiler will auto-generate operator!=
        template <class D, bool is_c>
        [[nodiscard]] constexpr bool
        operator==(const _list_iterator_crtp<T, D, is_c>& other) const noexcept
        {
            return m_node == other.m_node;
        }
    };


    // constexpr replacement for std::list
    // limited functionality to what's necessary for this library

    template <class T, class Alloc = std::allocator<T>>
        requires std::is_object_v<T>
    class list
    {
    public:

        // member types
        using value_type       = T;
        using allocator_type   = Alloc;
        using size_type        = std::size_t;
        using reference        = T&;
        using const_reference  = const T&;
        using rvalue_reference = T&&;

        // make iterator types opaque (rather than typedefs)
        // gives more understandable error messages at compile time

        struct iterator
            : public _list_iterator_crtp<T, iterator, false>
        {
        private:
            using base_type = _list_iterator_crtp<T, iterator, false>;
            using typename base_type::node_pointer;

            friend list;
            friend struct const_iterator;

            constexpr iterator(node_pointer node) noexcept
                : base_type{node}
            {}

        public:
            constexpr iterator() noexcept = default;
        };

        // const_iterator also gets to be constructible from iterator
        // makes iterator comparable with const_iterator

        struct const_iterator
            : public _list_iterator_crtp<T, const_iterator, true>
        {
        private:
            using base_type = _list_iterator_crtp<T, const_iterator, true>;
            using typename base_type::node_pointer;

            friend list;

            constexpr const_iterator(node_pointer node) noexcept
                : base_type{node}
            {}

        public:
            constexpr const_iterator() noexcept = default;

            constexpr const_iterator(iterator it) noexcept
                : base_type{it.m_node}
            {}
        };

    private:

        // member types
        using node_type = _list_node<T>;
        using rebind_allocator_type   = typename std::allocator_traits<Alloc>::template rebind_alloc<node_type>;
        using rebind_allocator_traits = typename std::allocator_traits<rebind_allocator_type>;

        // data members
        node_type *m_head = nullptr;
        node_type *m_tail = nullptr;
        size_type  m_size = 0;
        [[no_unique_address]] rebind_allocator_type m_alloc;

        // allocate and construct node with value from args
        template <class... Args>
            requires std::constructible_from<value_type, Args...>
        [[nodiscard]] constexpr node_type *
        new_node(Args&&... args)
            noexcept(std::is_nothrow_constructible_v<value_type, Args...> &&
                 noexcept(m_alloc.allocate(1)))
        {
            // no need to catch if allocate throws
            auto *node = rebind_allocator_traits::allocate(m_alloc, 1);

            // node constructor is noexcept
            static_assert(std::is_nothrow_constructible_v<node_type>);
            rebind_allocator_traits::construct(m_alloc, node);

            // attempt to construct T (value_type)
            try {
                auto *addr = std::addressof(node->holder.value);
                std::construct_at(addr, std::forward<Args>(args)...);
            }
            catch (...) {
                rebind_allocator_traits::destroy(m_alloc, node);
                rebind_allocator_traits::deallocate(m_alloc, node, 1);
                throw;
            }

            // success
            return node;
        }

        // destroy and de-allocate a node
        constexpr void
        delete_node(node_type* node) noexcept
        {
            SBMR_ASSERT_CONSTEXPR(node != nullptr);

            std::destroy_at(std::addressof(node->holder.value));
            rebind_allocator_traits::destroy(m_alloc, node);
            rebind_allocator_traits::deallocate(m_alloc, node, 1);
        }

    public:

        // Constructors - labelled according to std::list::list in cppreference

        // (1) default constructor
        constexpr list()
            noexcept(std::is_nothrow_default_constructible_v<rebind_allocator_type>)
            requires std::constructible_from<rebind_allocator_type>
        {}

        // (2) allocator constructor
        constexpr explicit list(const Alloc& alloc)
            noexcept(std::is_nothrow_constructible_v<rebind_allocator_type, const Alloc&>)
            requires std::constructible_from<rebind_allocator_type, const Alloc&>
            : m_alloc{alloc}
        {}

        // destructor
        constexpr ~list() noexcept
        {
            clear();
        }


        // Capacity

        // checks whether the container is empty
        [[nodiscard]] constexpr bool
        empty() const noexcept
        {
            return size() == 0;
        }

        // returns the number of elements
        [[nodiscard]] constexpr size_type
        size() const noexcept
        {
            return m_size;
        }

        // returns the maximum possible number of elements
        [[nodiscard]] constexpr size_type
        max_size() const noexcept
        {
            return std::numeric_limits<size_type>::max();
        }


        // Element access

        // returns a reference to the head element
        // pre-conditions: !empty()
        [[nodiscard]] constexpr reference
        front() noexcept
        {
            SBMR_ASSERT_CONSTEXPR(m_head != nullptr);

            return m_head->holder.value;
        }

        // returns a const_reference to the head element
        // pre-conditions: !empty()
        [[nodiscard]] constexpr const_reference
        front() const noexcept
        {
            SBMR_ASSERT_CONSTEXPR(m_head != nullptr);

            return m_head->holder.value;
        }

        // returns a reference to the tail element
        // pre-conditions: !empty()
        [[nodiscard]] constexpr reference
        back() noexcept
        {
            SBMR_ASSERT_CONSTEXPR(m_tail != nullptr);

            return m_tail->holder.value;
        }

        // returns a const_reference to the tail element
        // pre-conditions: !empty()
        [[nodiscard]] constexpr const_reference
        back() const noexcept
        {
            SBMR_ASSERT_CONSTEXPR(m_tail != nullptr);

            return m_tail->holder.value;
        }


        // Iterators

        // iterator pointing to first element, or end() if empty
        [[nodiscard]] constexpr iterator
        begin() noexcept
        {
            return iterator{m_head};
        }

        // const_iterator pointing to first element, or cend() if empty
        [[nodiscard]] constexpr const_iterator
        begin() const noexcept
        {
            return const_iterator{m_head};
        }

        // const_iterator pointing to first element, or cend() if empty
        [[nodiscard]] constexpr const_iterator
        cbegin() const noexcept
        {
            return const_iterator{m_head};
        }

        // iterator pointing to one-past-last element
        [[nodiscard]] constexpr iterator
        end() noexcept
        {
            return iterator{};
        }

        // const_iterator pointing to one-past-last element
        [[nodiscard]] constexpr const_iterator
        end() const noexcept
        {
            return const_iterator{};
        }

        // const_iterator pointing to one-past-last element
        [[nodiscard]] constexpr const_iterator
        cend() const noexcept
        {
            return const_iterator{};
        }


        // Modifiers

        // constructs a new element in place directly before pos
        // returns an iterator to the new element
        template <class... Args>
            requires std::constructible_from<value_type, Args...>
        constexpr iterator
        emplace(const_iterator pos, Args&&... args)
            noexcept(noexcept(new_node(std::forward<Args>(args)...)))
        {
            // valid as long as we own it since emplace isn't const
            auto* pos_node = const_cast<node_type *>(pos.m_node);

            // construct our new node
            auto *node = new_node(std::forward<Args>(args)...);

            // insert as first element
            if (pos == cend() && m_size == 0)
            {
                m_head = node;
                m_tail = node;
            }

                // insert at begin (and size is not 0)
            else if (pos == cbegin())
            {
                node->next = m_head;
                m_head->prev = node;
                m_head = node;
            }

                // insert at end (and size is not 0)
            else if (pos == cend())
            {
                node->prev = m_tail;
                m_tail->next = node;
                m_tail = node;
            }

                // insert in middle (before pos), with elements on both sides
            else
            {
                node->next = pos_node;
                node->prev = pos_node->prev;
                pos_node->prev = node;
            }

            // modify size to reflect changes
            ++m_size;

            // return iterator to newly created element
            return iterator{node};
        }

        // constructs a new element in place at the beginning
        // returns a reference to the new element (i.e. front())
        template <class... Args>
            requires std::constructible_from<value_type, Args...>
        constexpr reference
        emplace_front(Args&&... args)
            noexcept(noexcept(emplace(cbegin(), std::forward<Args>(args)...)))
        {
            emplace(cbegin(), std::forward<Args>(args)...);
            return front();
        }

        // constructs a new element in place at the end
        // returns a reference to the new element (i.e. back())
        template <class... Args>
            requires std::constructible_from<value_type, Args...>
        constexpr reference
        emplace_back(Args&&... args)
            noexcept(noexcept(emplace(cend(), std::forward<Args>(args)...)))
        {
            emplace(cend(), std::forward<Args>(args)...);
            return back();
        }

        // removes the element at pos
        // returns ++pos if pos is not end(), otherwise end()
        constexpr iterator
        erase(const_iterator pos) noexcept
        {
            // check end()
            if (pos == cend()) { return end(); }

            SBMR_ASSERT_CONSTEXPR(!empty());

            // get node and setup ret
            auto *node = const_cast<node_type *>(pos.m_node);
            auto it_ret = iterator{const_cast<node_type *>((++pos).m_node)};

            auto*& new_next = ((node == m_head) ? m_head : node->prev->next);
            auto*& new_prev = ((node == m_tail) ? m_tail : node->next->prev);

            new_next = node->next;
            new_prev = node->prev;

            delete_node(node);
            --m_size;

            return it_ret;
        }

        // removes the elements in the range [first, last)
        constexpr iterator
        erase(const_iterator first, const_iterator last) noexcept
        {
            auto _first = iterator{const_cast<node_type *>(first.m_node)};
            auto _last  = iterator{const_cast<node_type *>(last.m_node)};

            while (_first != _last) { _first = erase(_first); }

            return _first;
        }

        constexpr void
        clear() noexcept
        {
            erase(cbegin(), cend());
        }
    };


}  // namespace sbmr::_detail


#endif  // SBMR_DETAIL_LIST_HPP
