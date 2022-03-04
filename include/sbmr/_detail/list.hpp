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
        operator==(const _list_iterator_crtp& other) const noexcept
        {
            return m_node == other.m_node;
        }
    };


}  // namespace sbmr::_detail


#endif  // SBMR_DETAIL_LIST_HPP
