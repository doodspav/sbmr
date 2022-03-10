#ifndef SBMR_DETAIL_DYN_ARRAY_HPP
#define SBMR_DETAIL_DYN_ARRAY_HPP


#include <algorithm>
#include <bit>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

#include <sbmr/_detail/assert.hpp>


namespace sbmr::_detail {


    // minimal constexpr alternative to std::vector<T>
    // necessary because clang hasn't made theirs constexpr as of writing this
    // it's restricted to trivial T, and always uses std::allocator<T>
    // only used in sbmr::_impl::chunk_resource_consteval

    template <class T>
        requires std::is_trivial_v<T>
    class dyn_array
        : private std::allocator<T>
    {
    public:

        // member types
        using value_type      = T;
        using size_type       = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference       = value_type&;
        using const_reference = const value_type&;
        using pointer         = T *;
        using const_pointer   = const T *;
        using iterator        = pointer;
        using const_iterator  = const_pointer;

    private:

        // allocator member types
        using alloc_type   = std::allocator<T>;
        using alloc_traits = std::allocator_traits<alloc_type>;

        // data members
        pointer m_data = nullptr;
        pointer m_end  = nullptr;
        pointer m_cap  = nullptr;
        //[[no_unique_address]] alloc_type get_alloc();

        // returns an *this cast to the allocator base type
        [[nodiscard]] constexpr alloc_type&
        get_alloc() noexcept
        {
            return static_cast<alloc_type&>(*this);
        }

        // constants
        static constexpr size_type PAGE_SIZE = 4096u;

        // converts const_iterator to iterator
        [[nodiscard]] constexpr iterator
        unconst_iterator(const_iterator cit) noexcept
        {
            return const_cast<iterator>(cit);
        }

        // checks chat pos is in range [begin(), end()]
        [[nodiscard]] constexpr bool
        is_valid_iterator(const_iterator pos) const noexcept
        {
            return std::greater_equal{}(pos, m_data) &&
                   std::less_equal{}(pos, m_end);
        }

        // changes the capacity of the container
        // pre-conditions: new_cap >= size()
        constexpr void
        reserve_or_shrink_capacity(size_type new_cap)
        {
            // check pre-condition
            SBMR_ASSERT_CONSTEXPR(new_cap >= size());

            // check if we need to do work
            if (new_cap == capacity())
                ;

            // special case so that we can ensure we don't carry a compile-time
            //   allocation into runtime
            else if (new_cap == 0)
            {
                do_deallocate(m_data, capacity());

                m_data = nullptr;
                m_end  = nullptr;
                m_cap  = nullptr;
            }

            // do re-allocation
            else
            {
                auto old_cap = capacity();

                auto *new_data = alloc_traits::allocate(get_alloc(), new_cap);
                auto *new_end  = new_data;

                for (auto *it = m_data; it != m_end;)
                {
                    auto *tmp = it++;
                    alloc_traits::construct(get_alloc(), new_end, *tmp);
                    alloc_traits::destroy(get_alloc(), tmp);
                    ++new_end;
                }

                do_deallocate(m_data, old_cap);

                m_data = new_data;
                m_end  = new_end;
                m_cap  = new_data + new_cap;
            }
        }

        // ensures container can hold size() + count elements
        // pre-conditions: size() + count <= max_size() (and no overflow)
        constexpr void
        grow_if_needed_by(size_type count)
        {
            // check pre-condition
            SBMR_ASSERT_CONSTEXPR((size() + count) <= max_size());
            SBMR_ASSERT_CONSTEXPR((std::numeric_limits<size_type>::max() - size()) > count);

            // calculate new capacity
            auto new_cap = size() + count;
            if (new_cap < PAGE_SIZE) { new_cap = std::bit_ceil(new_cap); }
            else { new_cap += PAGE_SIZE - (new_cap % PAGE_SIZE); }

            // grow
            reserve(new_cap);
        }

        // wrapper around alloc_traits::deallocate(get_alloc(), ...)
        // required because de-allocating a null pointer is not constexpr
        constexpr void
        do_deallocate(pointer p, size_type n) noexcept
        {
            if (std::is_constant_evaluated()) {
                if (p == nullptr) { return; }
            }
            alloc_traits::deallocate(get_alloc(), p, n);
        }

    public:

        // Constructors, Assignment, and Destructor

        // default constructor
        constexpr dyn_array() noexcept = default;

        // destructor
        constexpr ~dyn_array() noexcept
        {
            clear();
            shrink_to_fit();
        }

        // copy constructor
        constexpr dyn_array(const dyn_array& other)
        {
            reserve(other.capacity());

            for (const auto& val: other) { push_back(val); }
        }

        // move constructor
        constexpr dyn_array(dyn_array&& other) noexcept
            : m_data(std::exchange(other.m_data, nullptr)),
              m_end(std::exchange(other.m_end, nullptr)),
              m_cap(std::exchange(other.m_cap, nullptr))
        {}

        // copy assignment
        constexpr dyn_array&
        operator=(const dyn_array& other)
        {
            if (&other != this)
            {
                clear();
                reserve_or_shrink_capacity(other.capacity());

                for (const auto& val : other) { push_back(val); }
            }

            return *this;
        }

        // move assignment
        constexpr dyn_array&
        operator=(dyn_array&& other) noexcept
        {
            if (&other != this)
            {
                clear();
                shrink_to_fit();

                m_data = std::exchange(other.m_data, nullptr);
                m_end  = std::exchange(other.m_end, nullptr);
                m_cap  = std::exchange(other.m_cap, nullptr);
            }

            return *this;
        }


        // Capacity

        // checks whether the container is empty
        [[nodiscard]] constexpr bool
        empty() const noexcept
        {
            return m_data == m_end;
        }

        // returns the number of elements in the container
        [[nodiscard]] constexpr size_type
        size() const noexcept
        {
            // std::distance is not constexpr
            return static_cast<size_type>(m_end - m_data);
        }

        // returns the maximum size the container can grow to
        [[nodiscard]] constexpr size_type
        max_size() const noexcept
        {
            auto umax = std::numeric_limits<size_type>::max();
            auto imax = std::numeric_limits<difference_type>::max();

            if (std::cmp_greater(imax, umax)) { return umax; }
            else { return static_cast<size_type>(imax); }
        }

        // returns the number of elements the container can hold without re-allocation
        [[nodiscard]] constexpr size_type
        capacity() const noexcept
        {
            // std::distance is not constexpr
            return static_cast<size_type>(m_cap - m_data);
        }

        // increase capacity to at least new_cap
        constexpr void
        reserve(size_type new_cap)
        {
            if (new_cap > capacity()) { reserve_or_shrink_capacity(new_cap); }
        }

        // requests the removal of unused capacity (non-binding)
        constexpr void
        shrink_to_fit()
        {
            reserve_or_shrink_capacity(size());
        }


        // Element Access

        // returns a reference to the element at index pos
        [[nodiscard]] constexpr reference
        operator[](size_type pos) noexcept
        {
            SBMR_ASSERT_CONSTEXPR(pos < size());
            return m_data[pos];
        }

        // returns a const_reference to the element at index pos
        [[nodiscard]] constexpr const_reference
        operator[](size_type pos) const noexcept
        {
            SBMR_ASSERT_CONSTEXPR(pos < size());
            return m_data[pos];
        }

        // returns a pointer to the underlying array
        [[nodiscard]] constexpr T *
        data() noexcept
        {
            return m_data;
        }

        [[nodiscard]] constexpr const T *
        data() const noexcept
        {
            return m_data;
        }


        // Modifiers

        // inserts value to the end of the container
        constexpr void
        push_back(const_reference value)
        {
            grow_if_needed_by(1);
            alloc_traits::construct(get_alloc(), m_end, value);
            ++m_end;
        }

        // removes last element from container
        // pre-conditions: !empty()
        constexpr void
        pop_back() noexcept
        {
            SBMR_ASSERT_CONSTEXPR(!empty());
            --m_end;
            alloc_traits::destroy(get_alloc(), m_end);
        }

        // removes element at pos from the container
        // pre-conditions: pos in [begin(), end()) (pos cannot be end())
        // returns iterator following last removed element
        constexpr iterator
        erase(const_iterator pos)
        {
            // check pre-conditions
            SBMR_ASSERT_CONSTEXPR(is_valid_iterator(pos));
            SBMR_ASSERT_CONSTEXPR(pos != cend());

            // defer to overload
            return erase(pos, pos + 1);
        }

        // removes elements in the range [first, last) from the container
        // pre-conditions: first <= last and both in [begin(), end()]
        // returns iterator following last removed element, or last if empty range
        constexpr iterator
        erase(const_iterator first, const_iterator last)
        {
            // check pre-conditions
            SBMR_ASSERT_CONSTEXPR(is_valid_iterator(first));
            SBMR_ASSERT_CONSTEXPR(is_valid_iterator(last));
            SBMR_ASSERT_CONSTEXPR(first <= last);

            // check if we need to do any work
            auto diff = last - first;
            if (diff == 0) { return unconst_iterator(last); }

            // shift into elements to remove them
            auto ret = std::shift_left(unconst_iterator(first), m_end, diff);
            m_end -= diff;

            // destroy everything that was removed
            for (auto *it = m_end; diff > 0; ++it, --diff) {
                alloc_traits::destroy(get_alloc(), it);
            }

            return ret;
        }

        // removes all elements from the container
        constexpr void
        clear()
        {
            auto *it = m_data;
            while (it != m_end)
            {
                auto *tmp = it++;
                alloc_traits::destroy(get_alloc(), tmp);
            }
            m_end = m_data;
        }


        // Iterators

        // returns an iterator to the beginning
        [[nodiscard]] constexpr iterator
        begin() noexcept
        {
            return iterator{m_data};
        }

        // returns a const_iterator to the beginning
        [[nodiscard]] constexpr const_iterator
        begin() const noexcept
        {
            return const_iterator{m_data};
        }

        // returns a const_iterator to the beginning
        [[nodiscard]] constexpr const_iterator
        cbegin() const noexcept
        {
            return begin();
        }

        // returns an iterator to the end
        [[nodiscard]] constexpr iterator
        end() noexcept
        {
            return iterator{m_end};
        }

        // returns a const_iterator to the end
        [[nodiscard]] constexpr const_iterator
        end() const noexcept
        {
            return const_iterator{m_end};
        }

        // returns a const_iterator to the end
        [[nodiscard]] constexpr const_iterator
        cend() const noexcept
        {
            return end();
        }
    };


}  // namespace sbmr::_detail


#endif  // SBMR_DETAIL_DYN_ARRAY_HPP
