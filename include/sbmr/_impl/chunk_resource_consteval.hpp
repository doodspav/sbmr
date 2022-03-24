#ifndef SBMR_IMPL_CHUNK_RESOURCE_CONSTEVAL_HPP
#define SBMR_IMPL_CHUNK_RESOURCE_CONSTEVAL_HPP


#include <algorithm>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

#include <sbmr/_detail/assert.hpp>
#include <sbmr/_detail/dyn_array.hpp>


namespace sbmr::_impl {


    // this implementation is constexpr
    // it defers to std::allocator at compile time, and does nothing at runtime
    // it is only intended to be used at compile time, and it is UB to use any
    //   member functions at runtime (other than constructor and destructor)
    // there are no "block" based restrictions in this class; it is up to the
    //   user of the class to implement such restrictions

    class chunk_resource_consteval
    {
    public:

        // default constructor
        constexpr chunk_resource_consteval() noexcept = default;

        // memory resource should not be copyable or movable
        chunk_resource_consteval(const chunk_resource_consteval&) = delete;
        chunk_resource_consteval(chunk_resource_consteval&&)      = delete;

        // memory resource should not be copy or move assignable
        auto& operator=(const chunk_resource_consteval&) = delete;
        auto& operator=(chunk_resource_consteval&&)      = delete;

        // member type
        struct alloc_info {

            const void *p;
            std::size_t n;

            [[nodiscard]] constexpr bool
            operator==(const alloc_info& other) const noexcept = default;
        };

    private:

        // member type
        using allocations_type = typename sbmr::_detail::dyn_array<alloc_info>;

        // data member
        allocations_type *m_ptrs = nullptr;

    public:

        // number of allocations currently not de-allocated
        [[nodiscard]] constexpr std::size_t
        allocation_count() const noexcept
        {
            // compile time
            if (std::is_constant_evaluated())
            {
                if (m_ptrs == nullptr) { return 0; }
                else { return m_ptrs->size(); }
            }
            // runtime
            else
            {
                SBMR_ASSERT(!"cannot be called at runtime");
                return 0;
            }
        }

        // UB to call at runtime
        // checks if a pointer is currently allocated at compile time, but
        //   does not check for `n` that was passed to the allocation function
        [[nodiscard]] constexpr bool
        is_maybe_allocated(const void *unknown_ptr) const noexcept
        {
            // compile time
            if (std::is_constant_evaluated())
            {
                // guard before accessing _m_ptrs
                if (m_ptrs == nullptr) { return false; }

                auto cmp_p = [=](alloc_info ai) -> bool {
                    return ai.p == unknown_ptr;
                };

                auto it = std::find_if(m_ptrs->cbegin(), m_ptrs->cend(), cmp_p);
                return it != m_ptrs->cend();
            }
            // runtime
            else
            {
                SBMR_ASSERT(!"cannot be called at runtime");
                return false;
            }
        }

        // UB to call at runtime
        // checks if a pointer is currently allocated at compile time
        // returns index of ptr in list if it is allocated, otherwise -1
        // return value should be treated as a token to be passed to de-allocator
        // function returns a token to minimize duplicated work in de-allocation
        // return value is INVALIDATED upon call to any non-const member function
        // pre-conditions: none
        [[nodiscard]] constexpr std::ptrdiff_t
        is_allocated(const void *unknown_ptr, std::size_t n) noexcept
        {
            // compile time
            if (std::is_constant_evaluated())
            {
                // guard before accessing _m_ptrs
                if (m_ptrs == nullptr) { return -1; }

                auto val  = alloc_info{.p=unknown_ptr, .n=n};
                auto it   = std::find(m_ptrs->begin(), m_ptrs->end(), val);
                auto diff = static_cast<std::ptrdiff_t>(it - m_ptrs->begin());

                return (it == m_ptrs->end()) ? -1 : diff;
            }
            // runtime
            else
            {
                SBMR_ASSERT(!"cannot be called at runtime");
                return -1;
            }
        }

        // UB to call at runtime
        // perform an allocation at compile time
        // technically this can throw, but we can mark it noexcept since it can
        //   only throw at compile time
        // pre-conditions: none
        template <class T>
            requires std::is_object_v<T>
        [[nodiscard, gnu::returns_nonnull]] constexpr T *
        obtain_ptr_unchecked(std::size_t n) noexcept
        {
            // compile time
            if (std::is_constant_evaluated())
            {
                // guard before accessing _m_ptrs
                if (m_ptrs == nullptr) { m_ptrs = new allocations_type; }

                // perform allocation
                auto *ptr = std::allocator<T>().allocate(n);
                m_ptrs->push_back({.p=static_cast<const void *>(ptr), .n=n});
                return ptr;
            }
            // runtime
            else
            {
                SBMR_ASSERT(!"cannot be called at runtime");
                return nullptr;
            }
        }

        // UB to call at runtime
        // perform a de-allocation at compile time
        // pos must be the value return valid of is_allocated if not -1
        // pre-conditions: pos >= 0 &&
        //                 pos < m_ptrs.size() &&
        //                 allocated_ptr == m_ptrs[pos]
        template <class T>
            requires std::is_object_v<T>
        constexpr void
        return_ptr_unchecked(T *allocated_ptr, std::size_t n, std::ptrdiff_t pos) noexcept
        {
            // compile time
            if (std::is_constant_evaluated())
            {
                // prevent compilers complaining about comparisons
                auto upos = static_cast<std::size_t>(pos);

                // pre-conditions checks
                SBMR_ASSERTM_CONSTEVAL(pos != -1,
                    "token indicates is_allocated() failed");
                SBMR_ASSERTM_CONSTEVAL(pos >= 0,
                    "token not obtained from is_allocated()");
                SBMR_ASSERTM_CONSTEVAL(m_ptrs != nullptr,
                                       "token not obtained from is_allocated or already invalidated by calling non-const member function after is_allocated()");
                SBMR_ASSERTM_CONSTEVAL(upos < m_ptrs->capacity(),
                    "token not obtained from is_allocated()");
                SBMR_ASSERTM_CONSTEVAL(upos < m_ptrs->size(),
                    "token likely invalidated by calling non-const member function after is_allocated()");
                SBMR_ASSERTM_CONSTEVAL(allocated_ptr == (*m_ptrs)[upos].p,
                    "token likely invalidated by calling non-const member function after is_allocated()");
                SBMR_ASSERTM_CONSTEVAL(n == (*m_ptrs)[upos].n,
                    "token likely invalidated by calling non-const member function after is_allocated()");

                // free memory
                auto it = m_ptrs->begin() + pos;
                m_ptrs->erase(it);
                std::allocator<T>().deallocate(allocated_ptr, n);

                // de-allocate dynamic array if empty
                if (m_ptrs->empty())
                {
                    delete m_ptrs;
                    m_ptrs = nullptr;
                }
            }
            // runtime
            else { SBMR_ASSERT(!"cannot be called at runtime"); }
        }
    };


}  // namespace sbmr::_impl


#endif  // SBMR_IMPL_CHUNK_RESOURCE_CONSTEVAL_HPP
