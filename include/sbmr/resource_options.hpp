#ifndef SBMR_RESOURCE_OPTIONS_HPP
#define SBMR_RESOURCE_OPTIONS_HPP


#include <algorithm>
#include <bit>
#include <compare>
#include <cstddef>
#include <limits>
#include <ostream>
#include <type_traits>
#include <utility>

#include <sbmr/_detail/assert.hpp>


namespace sbmr {


    namespace _detail {


        // check the sizeof of a chunk_resource
        // i.e. it can be represented by size_t and ptrdiff_t, and isn't 0
        // this should only be called using values from a normalized
        //   chunk_options object because it does not take align into account
        [[nodiscard]] constexpr bool
        valid_sizeof(std::size_t size, std::size_t count) noexcept
        {
            constexpr auto umax = std::numeric_limits<std::size_t>::max();
            constexpr auto imax = std::numeric_limits<std::ptrdiff_t>::max();

            // sizeof cannot return 0
            if (size == 0 || count == 0) { return false; }

            // check we don't overflow std::size_t or std::ptrdiff_t
            bool overflows_size = (size > umax / count);
            bool overflows_ptrdiff = ((size * count) > imax);

            return !overflows_size && !overflows_ptrdiff;
        }


    }  // namespace _detail


    struct chunk_options
    {
        // do not change member order - will affect operator<=>
        // order must be size -> align -> count
        std::size_t block_size;
        std::size_t block_align;
        std::size_t block_count;

        // .normalized() will not increase block_align past this value
        static constexpr std::size_t max_default_align = __STDCPP_DEFAULT_NEW_ALIGNMENT__;

        // checks if a block as described by the instance can hold an object of
        // type T[n]
        template <class T>
            requires std::is_object_v<T>
        [[nodiscard]] constexpr bool
        compatible_with(std::size_t n = 1) const noexcept
        {
            // check for overflow
            if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
                return false;
            }

            // count isn't a concern here
            // align is an issue even if n == 0
            return (sizeof(T) * n <= block_size) && (alignof(T) <= block_align);
        }

        // increase size to include padding caused by alignment requirement
        // increase align without affecting padding or size
        // pre-conditions: valid() == true
        [[nodiscard]] constexpr chunk_options
        normalized() const noexcept
        {
            // pre-conditions (equivalent to .valid())
            // check non-zero separately for better diagnostics
            SBMR_ASSERT_CONSTEXPR(block_size > 0);
            SBMR_ASSERT_CONSTEXPR(block_count > 0);
            SBMR_ASSERT_CONSTEXPR(std::has_single_bit(block_align));
            SBMR_ASSERT_CONSTEXPR(_detail::valid_sizeof(block_size, block_count));

            // expand size to include padding
            // i.e. increase to smallest multiple of align not less than size
            auto size = block_align;
            while (size < block_size) { size += block_align; }

            // increase align to the highest power of 2
            // WITHOUT increasing padding
            // i.e. to the largest power of 2 that size is divisible by
            // this should never be less than block_align (if valid() == true)
            // DOES NOT increase align past max_default_align
            // going past this limit requires block_align being set to a larger
            //   value by the user
            auto align = block_align;
            if (align < max_default_align)
            {
                align = size & (~(size - 1u));
                align = std::min(align, max_default_align);
            }

            // create new options object
            return {
                .block_size  = size,
                .block_align = align,
                .block_count = block_count
            };
        }

        // check all fields are in a valid state, separately and together
        [[nodiscard]] constexpr bool
        valid() const noexcept
        {
            // block_size and block_count != 0
            // block_size * block_count fits in size_t and ptrdiff_t
            // block_align is a power of 2
            return _detail::valid_sizeof(block_size, block_count) &&
                   std::has_single_bit(block_align);
        }

        // returns .valid()
        [[nodiscard]] explicit constexpr
        operator bool() const noexcept
        {
            return valid();
        }

        // should <=> size, then align, and finally count
        // with early exit if any compare neq
        [[nodiscard]] constexpr std::strong_ordering
        operator<=>(const chunk_options&) const noexcept = default;

        // outputs: {.block_size=S, .block_align=A, .block_count=C}
        friend std::ostream&
        operator<<(std::ostream& os, const chunk_options& opts)
        {
            return os << "{.block_size=" << opts.block_size
                      << ", .block_align=" << opts.block_align
                      << ", .block_count=" << opts.block_count
                      << '}';
        }
    };


    // provided in addition to .valid() (identical functionality)
    // generates more informative diagnostics than `requires Opts.valid()`
    template <chunk_options Opts>
    concept ValidChunkOptions =
        Opts.block_size > 0 &&
        Opts.block_count > 0 &&
        std::has_single_bit(Opts.block_align) &&
        _detail::valid_sizeof(Opts.block_size, Opts.block_count);


}  // namespace sbmr


#endif  // SBMR_RESOURCE_OPTIONS_HPP
