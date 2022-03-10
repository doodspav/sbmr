#ifndef SBMR_IMPL_CHUNK_RESOURCE_HPP
#define SBMR_IMPL_CHUNK_RESOURCE_HPP


#include <algorithm>
#include <bit>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

#include <sbmr/resource_options.hpp>

#include <sbmr/_detail/assert.hpp>
#include <sbmr/_detail/dyn_array.hpp>
#include <sbmr/_detail/integer_traits.hpp>
#include <sbmr/_detail/optimistic_sort.hpp>


namespace sbmr::_impl {


    // this implementation is constexpr, and all functions work at compile time
    // unfortunately it only works in terms of void*, and reinterpret_cast is
    //   not available at compile time, so a user would not be able to use the
    //   allocated pointers for much

    template <chunk_options Opts>
        requires ValidChunkOptions<Opts.normalized()>
    class chunk_resource_runtime
    {
    public:

        // fill index stack with indexes in reverse order
        // so that the first block allocated is at index 0
        constexpr chunk_resource_runtime() noexcept
        {
            // we cannot use std::itoa + std::reverse_iterator
            // gcc takes forever to compile that if it tries to compute the loop
            //   at compile time

            auto size = s_options.block_count;
            for (auto& i : m_block_index_stack) {

                // explicit conversion to avoid compiler complaining
                // size-1 always fits in block_index_type
                i = static_cast<block_index_type>(--size);
            }
        }

        // memory resource should not be copyable or movable
        chunk_resource_runtime(const chunk_resource_runtime&) = delete;
        chunk_resource_runtime(chunk_resource_runtime&&)      = delete;

        // memory resource should not be copy or move assignable
        auto& operator=(const chunk_resource_runtime&) = delete;
        auto& operator=(chunk_resource_runtime&&)      = delete;

        // use normalized version of our options
        // size and align are extended w.r.t. each other and padding
        // this does not cause blocks to take up any more space than they would
        //   have before normalization
        static constexpr auto s_options = Opts.normalized();

        // member types
        using block_count_type = _detail::fast_nowrap_t<std::bit_width(s_options.block_count)>;
        using block_index_type = _detail::least_unsigned_t<std::bit_width(s_options.block_count - 1u)>;
        using block_type = struct {
            alignas(s_options.block_align) std::byte arr[s_options.block_size];
        };

        // special block whose address is to be returned when allocating 0 bytes
        // its value should never be accessed
        static constexpr block_type s_zero_block {};

        // data members
        block_count_type m_available_blocks = static_cast<block_count_type>(s_options.block_count);
        block_index_type m_block_index_stack[s_options.block_count];
        block_type m_blocks[s_options.block_count];

        // pointer to base of index stack
        // essentially .begin() on the underlying container
        [[nodiscard]] constexpr block_index_type *
        stack_begin() noexcept
        {
            return std::data(m_block_index_stack);
        }

        // const pointer to base of index stack
        [[nodiscard]] constexpr const block_index_type *
        stack_cbegin() const noexcept
        {
            return std::data(m_block_index_stack);
        }

        // pointer to one-past-last element of index stack's underlying array
        // essentially .end() on the underlying container
        [[nodiscard]] constexpr block_index_type *
        stack_end() noexcept
        {
            return stack_begin() + s_options.block_count;
        }

        // const pointer to one-past-last element of index stack's underlying array
        [[nodiscard]] constexpr const block_index_type *
        stack_cend() const noexcept
        {
            return stack_cbegin() + s_options.block_count;
        }

        // pointer to the first index of an unavailable block
        // this technically represents the end of the stack (as opposed to
        //   stack_end() which represents the end of the underlying container)
        // decrementing this pointer down to stack_begin() yields the indexes of
        //   all the available blocks
        // incrementing this pointer up to stack_end() yields the indexes of all
        //   the unavailable blocks
        [[nodiscard]] constexpr block_index_type *
        stack_midpoint() noexcept
        {
            return stack_begin() + m_available_blocks;
        }

        // const pointer to the first index of an unavailable block
        [[nodiscard]] constexpr const block_index_type *
        stack_cmidpoint() const noexcept
        {
            return stack_cbegin() + m_available_blocks;
        }

        // sorts all available indexes in reverse order
        // intended to improve performance of cyclic allocation patterns
        constexpr void
        rsort_available_indexes() noexcept
        {
            std::sort(stack_begin(), stack_midpoint(), std::greater{});
        }

        // equivalent to rsort_available_indexes(), but optimises under the
        //   assumption that input is almost or completely sorted
        constexpr void
        rsort_optimistic_available_indexes() noexcept
        {
            using _detail::optimistic_sort;
            optimistic_sort(stack_begin(), stack_midpoint(), std::greater{});
        }

        // checks if a pointer points to memory contained by m_blocks
        // returns false for nullptr and &s_zero_block
        // pre-conditions: none
        [[nodiscard]] constexpr bool
        is_maybe_owned(const void *unknown_ptr) const noexcept
        {
            if ((unknown_ptr == nullptr) ||
                (unknown_ptr == &s_zero_block))
            { return false; }

            auto *lo  = std::data(m_blocks);
            auto *hi  = lo + s_options.block_count;
            auto *ptr = unknown_ptr;

            // use std::greater_equal and std::less for total ordering
            return std::greater_equal{}(ptr, lo) && std::less{}(ptr, hi);
        }

        // checks if a pointer points to the start of a block
        // returns false for nullptr and &s_zero_block
        // pre-conditions: none
        [[nodiscard]] constexpr bool
        is_owned(const void *unknown_ptr) const noexcept
        {
            // check that ptr points to some memory in m_blocks
            if (!is_maybe_owned(unknown_ptr)) { return false; }

            if (std::is_constant_evaluated())
            {
                // check if ptr matches any block's address
                // use std::less_equal for total ordering

                // check allocated blocks first (since this function is almost
                //   exclusively called right before is_allocated())
                auto *const mid = std::data(m_blocks) + m_available_blocks;
                auto *it = mid;
                for (; std::less_equal{}(it, unknown_ptr); ++it)
                {
                    if (it == unknown_ptr) { return true; }
                }

                // check non-allocated blocks if above loop didn't return
                it = std::data(m_blocks);
                for (; it != mid && std::less_equal{}(it, unknown_ptr); ++it)
                {
                    if (it == unknown_ptr) { return true; }
                }

                // no blocks matched
                return false;
            }
            else  // is_runtime_evaluated()
            {
                // check that diff is a multiple of the block size
                auto *lo  = reinterpret_cast<const char *>(std::data(m_blocks));
                auto *ptr = reinterpret_cast<const char *>(unknown_ptr);
                auto diff = ptr - lo;

                return (diff % sizeof(block_type)) == 0;
            }
        }

        // obtains the block index of the block the given pointer points to
        // pre-conditions: is_owned(ptr) == true
        [[nodiscard]] constexpr block_index_type
        block_index(const void *owned_ptr) const noexcept
        {
            if (std::is_constant_evaluated())
            {
                block_index_type idx = 0;
                auto *it = std::data(m_blocks);

                // use std::less_equal for total ordering
                for (; std::less_equal{}(it, owned_ptr); ++it, ++idx)
                {
                    // pre-conditions check
                    SBMR_ASSERTM_CONSTEVAL(idx < s_options.block_count,
                        "is_owned(ptr) not satisfied");

                    if (it == owned_ptr) { break; }
                }

                return idx;
            }
            else  // is_runtime_evaluated()
            {
                auto *ptr = reinterpret_cast<const block_type *>(owned_ptr);
                auto idiff = ptr - std::data(m_blocks);  // ptrdiff_t
                auto udiff = static_cast<std::size_t>(idiff);

                // pre-conditions checks
                // signed to check non-negative
                SBMR_ASSERTM(idiff >= 0,
                    "is_owned(ptr) not satisfied");
                // unsigned because block_index_type is also unsigned
                SBMR_ASSERTM(udiff <= std::numeric_limits<block_index_type>::max(),
                    "is_owned(ptr) not satisfied");
                // unsigned because block_count is also unsigned
                SBMR_ASSERTM(udiff < s_options.block_count,
                    "is_owned(ptr) not satisfied");

                return static_cast<block_index_type>(udiff);
            }
        }

        // checks if a pointer is currently allocated (and thus unavailable)
        // returns index of block index if it is allocated, otherwise -1
        // return value should be treated as a token to be passed to de-allocator
        // function returns a token to minimize duplicated work in de-allocation
        // return value is INVALIDATED upon call to any non-const member function
        // pre-conditions: is_owned(ptr) == true
        [[nodiscard]] constexpr std::ptrdiff_t
        is_allocated(const void *owned_ptr) const noexcept
        {
            // block_index() checks our pre-conditions for us
            const auto idx = block_index(owned_ptr);

            // midpoint -> end is allocated blocks
            // optimised for cyclic allocation/de-allocation usage pattern
            // i.e. *stack_midpoint() should be most likely to hold the index of
            //   the block we want to de-allocate (last allocated, fifo)
            auto it = std::find(stack_cmidpoint(), stack_cend(), idx);

            // return -1 if not found (i.e. not allocated)
            if (it == stack_cend()) { return -1; }
            // else return index of idx
            else { return std::distance(stack_cbegin(), it); }
        }

        // perform an allocation (i.e. mark an available block as unavailable)
        // pre-conditions: m_available_blocks > 0
        [[nodiscard, gnu::returns_nonnull]] constexpr block_type *
        obtain_ptr_unchecked() noexcept
        {
            // pre-conditions check
            SBMR_ASSERTM_CONSTEXPR(m_available_blocks > 0,
                "no blocks available");

            auto idx = m_block_index_stack[--m_available_blocks];
            return std::data(m_blocks) + idx;
        }

        // perform a de-allocation (i.e. mark an unavailable block as available)
        // index_index must be the valid return value of is_allocated if not -1
        // pre-conditions: index_index >= m_available_blocks &&
        //                 index_index <  s_options.block_count
        constexpr void
        return_block_unchecked(std::ptrdiff_t index_index) noexcept
        {
            // explicit conversion to avoid compiler complaining
            // block_count can always be represented by ptrdiff_t
            // since we require that size * count can be too (and count != 0)
            constexpr auto block_count = static_cast<std::ptrdiff_t>(s_options.block_count);

            // pre-conditions checks
            SBMR_ASSERTM_CONSTEXPR(index_index < block_count,
                "token not obtained from is_allocated()");
            SBMR_ASSERTM_CONSTEXPR(index_index >= 0,
                "token not obtained from is_allocated()");
            SBMR_ASSERTM_CONSTEXPR(index_index >= m_available_blocks,
                "token likely invalidated by calling non-const member function after is_allocated()");

            auto *idx = stack_begin() + index_index;
            auto *mid = stack_midpoint();

            std::swap(*idx, *mid);
            ++m_available_blocks;
        }
    };


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

        // data member
        sbmr::_detail::dyn_array<alloc_info> m_ptrs;

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
                auto val  = alloc_info{.p=unknown_ptr, .n=n};
                auto it   = std::find(m_ptrs.begin(), m_ptrs.end(), val);
                auto diff = static_cast<std::ptrdiff_t>(it - m_ptrs.begin());

                return (it == m_ptrs.end()) ? -1 : diff;
            }
            // runtime
            else
            {
                SBMR_ASSERT(!"cannot be called at runtime");
                return {};
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
                auto *ptr = std::allocator<T>().allocate(n);
                m_ptrs.push_back({.p=static_cast<const void *>(ptr), .n=n});
                return ptr;
            }
            // runtime
            else
            {
                SBMR_ASSERT(!"cannot be called at runtime");
                return {};
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
                SBMR_ASSERTM_CONSTEVAL(upos < m_ptrs.capacity(),
                    "token not obtained from is_allocated()");
                SBMR_ASSERTM_CONSTEVAL(upos < m_ptrs.size(),
                    "token likely invalidated by calling non-const member function after is_allocated()");
                SBMR_ASSERTM_CONSTEVAL(allocated_ptr == m_ptrs[upos].p,
                    "token likely invalidated by calling non-const member function after is_allocated()");
                SBMR_ASSERTM_CONSTEVAL(n == m_ptrs[upos].n,
                    "token likely invalidated by calling non-const member function after is_allocated()");

                // free memory
                auto it = m_ptrs.begin() + pos;
                m_ptrs.erase(it);
                std::allocator<T>().deallocate(allocated_ptr, n);
            }
            // runtime
            else { SBMR_ASSERT(!"cannot be called at runtime"); }
        }
    };


}  // namespace sbmr::_impl


#endif  // SBMR_IMPL_CHUNK_RESOURCE_HPP
