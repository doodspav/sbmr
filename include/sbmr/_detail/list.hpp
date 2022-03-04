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


    // simple doubly linked node
    // constructors are present because std::construct_at doesn't seem to work
    //   with aggregate types very well

    template <class T>
    struct _list_node
    {
        // constructor to make allocator_traits::construct happy
        constexpr explicit _list_node(const T& value)
            noexcept(std::is_nothrow_copy_constructible_v<T>)
            requires std::copyable<T>
            : value{value}
        {}

        // ditto ^^
        constexpr explicit _list_node(T&& value)
            noexcept(std::is_nothrow_move_constructible_v<T>)
            requires std::movable<T>
            : value{std::move(value)}
        {}

        // data members
        T value;
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

            return m_node->value;
        }

        // pointer member access
        // returns pointer to value
        constexpr pointer
        operator->() const noexcept
        {
            SBMR_ASSERT_CONSTEXPR(m_node != nullptr);

            return std::addressof(m_node->value);
        }

        // equality comparison with self
        // compiler will auto-generate operator!=
        [[nodiscard]] constexpr bool
        operator==(const Derived& other) const noexcept
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
    };


}  // namespace sbmr::_detail


#endif  // SBMR_DETAIL_LIST_HPP
