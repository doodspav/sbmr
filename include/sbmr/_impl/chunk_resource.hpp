#ifndef SBMR_IMPL_CHUNK_RESOURCE_HPP
#define SBMR_IMPL_CHUNK_RESOURCE_HPP


#include <algorithm>
#include <bit>
#include <cstddef>
#include <functional>
#include <iterator>
#include <utility>

#include <sbmr/resource_options.hpp>

#include <sbmr/_detail/assert.hpp>
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
            for (auto& i : m_block_index_stack) { i = --size; }
        }

        // memory resource should not be copyable or movable
        chunk_resource_runtime(const chunk_resource_runtime&) = delete;
        chunk_resource_runtime(chunk_resource_runtime&&)      = delete;

        // memory resource should not be copy or move assignable
        auto& operator=(const chunk_resource_runtime&) = delete;
        auto& operator=(chunk_resource_runtime&&)      = delete;

        // use normalized version of our options
        // size and align are extended w.r.t each other and padding
        // this does not cause blocks to take up any more space than they would
        //   have before normalization
        static constexpr auto s_options = Opts.normalized();

        // member types
        using block_count_type = _detail::fast_nowrap_t<std::bit_width(s_options.block_count)>;
        using block_index_type = _detail::least_unsigned_t<std::bit_width(s_options.block_count)>;
        using block_type = struct {
            alignas(s_options.block_align) std::byte arr[s_options.block_size];
        };

        // special block whose address is to be returned when allocating 0 bytes
        // its value should never be accessed
        static constexpr block_type s_zero_block {};

        // data members
        block_count_type m_available_blocks = s_options.block_count;
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
            auto *hi  = lo + sizeof(m_blocks);
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
                auto *it = std::data(m_blocks);

                // use std::less_equal for total ordering
                for (; std::less_equal{}(it, unknown_ptr); ++it)
                {
                    if (it == unknown_ptr) { return true; }
                }
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
                    if (it == owned_ptr) { break; }
                }

                // pre-conditions check
                SBMR_ASSERTM_CONSTEVAL(idx < m_available_blocks,
                    "pre-conditions not met: is_owned(ptr) == true");

                return idx;
            }
            else  // is_runtime_evaluated()
            {
                auto *ptr = reinterpret_cast<const block_type *>(owned_ptr);
                auto diff = ptr - std::data(m_blocks);

                // pre-condition checks
                SBMR_ASSERTM(diff >= 0,
                    "pre-conditions not met: is_owned(ptr) == true");
                SBMR_ASSERTM(diff > std::numeric_limits<block_index_type>::max(),
                    "pre-conditions not met: is_owned(ptr) == true");

                return static_cast<block_index_type>(diff);
            }
        }

        // checks if a pointer is currently allocated (and thus available)
        // returns index of block index if it is allocated, otherwise -1
        // return value should be treated as a token to be passed to de-allocator
        // function returns a token to minimize duplicated work in de-allocation
        // pre-conditions: is_owned(ptr) == true
        [[nodiscard]] constexpr std::ptrdiff_t
        is_allocated(const void *owned_ptr) const noexcept
        {
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
        [[nodiscard, gnu::malloc, gnu::returns_nonnull]] constexpr block_type *
        obtain_ptr_unchecked() noexcept
        {
            SBMR_ASSERT_CONSTEXPR(m_available_blocks > 0);

            auto idx = m_block_index_stack[--m_available_blocks];
            return std::data(m_blocks) + idx;
        }

        // perform a de-allocation (i.e. mark an unavailable block as available)
        // index_index should be the return value of is_allocated if not -1
        // pre-condition: index_index >= m_available_blocks &&
        //                index_index <  s_options.block_count
        constexpr void
        return_block_unchecked(std::ptrdiff_t index_index) noexcept
        {
            SBMR_ASSERT_CONSTEXPR(index_index < s_options.block_count);
            SBMR_ASSERT_CONSTEXPR(index_index >= m_available_blocks);

            auto *idx = stack_begin() + index_index;
            auto *mid = stack_midpoint();

            std::swap(*idx, *mid);
            ++m_available_blocks;
        }
    };


}  // namespace sbmr::_impl


#endif  // SBMR_IMPL_CHUNK_RESOURCE_HPP
