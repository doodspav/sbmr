#ifndef SBMR_DETAIL_INTEGER_TRAITS_HPP
#define SBMR_DETAIL_INTEGER_TRAITS_HPP


#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>


// helper concepts

namespace sbmr::_detail {


    template <unsigned N, class T>
    concept WithinDigitsLimit =
        std::integral<T> &&
        N <= std::numeric_limits<T>::digits;


}  // namespace sbmr::_detail


// fastN_t

namespace sbmr::_detail {


    template <unsigned N>
        requires WithinDigitsLimit<N, unsigned long long> ||
                 WithinDigitsLimit<N, std::uint_fast64_t>
    struct fast_signed
    {
    private:
        [[nodiscard]] static consteval std::signed_integral auto
        fast_signed_zero() noexcept
        {
            // digits always has to be done with unsigned type
            // because signed type omits the sign bit
            constexpr auto dig8  = std::numeric_limits<std::uint_fast8_t>::digits;
            constexpr auto dig16 = std::numeric_limits<std::uint_fast16_t>::digits;
            constexpr auto dig32 = std::numeric_limits<std::uint_fast32_t>::digits;
            constexpr auto dig64 = std::numeric_limits<std::uint_fast64_t>::digits;

            if constexpr (N <= dig8) { return std::int_fast8_t{}; }
            else if constexpr (N <= dig16) { return std::int_fast16_t{}; }
            else if constexpr (N <= dig32) { return std::int_fast32_t{}; }
            else if constexpr (N <= dig64) { return std::int_fast64_t{}; }
            // explicit cast for clarity
            else { return static_cast<long long>(0); }
        }

    public:
        using type = decltype(fast_signed_zero());
    };

    template <unsigned N>
    using fast_signed_t = typename fast_signed<N>::type;


    template <unsigned N>
    struct fast_unsigned
    {
        using type = std::make_unsigned_t<fast_signed_t<N>>;
    };

    template <unsigned N>
    using fast_unsigned_t = typename fast_unsigned<N>::type;


    // nowrap requires that the type is only used to represent positive values
    //   (even though it may be signed), and that overflow is treated as UB
    //   (even if the actual behaviour may wrap)
    // nowrap guarantees that N bits can be used to represent a positive value
    //   (and thus N does not include the sign bit if nowrap is signed)
    // nowrap is signed where possible
    // i.e. fast_signed_t<N+1> if it exists, else fast_unsigned_t<N>

    template <unsigned N>
        requires (N < std::numeric_limits<unsigned>::max())
    struct fast_nowrap
    {
    private:
        [[nodiscard]] static consteval std::unsigned_integral auto
        fast_nowrap_zero() noexcept
        {
            return fast_unsigned_t<N>{};
        }

        [[nodiscard]] static consteval std::signed_integral auto
        fast_nowrap_zero() noexcept
            // here we check signed with N - same as unsigned with N-1
            requires WithinDigitsLimit<N, long long> ||
                     WithinDigitsLimit<N, std::int_fast64_t>
        {
            // fast_signed includes sign bit in N, so we need N+1
            return fast_signed_t<N+1>{};
        }

    public:
        using type = decltype(fast_nowrap_zero());
    };

    template <unsigned N>
    using fast_nowrap_t = typename fast_nowrap<N>::type;


}  // namespace sbmr::_detail


// leastN_t

namespace sbmr::_detail {


    template <unsigned N>
        requires WithinDigitsLimit<N, unsigned long long> ||
                 WithinDigitsLimit<N, std::uint_fast64_t>
    struct least_signed
    {
    private:
        [[nodiscard]] static consteval std::signed_integral auto
        least_signed_zero() noexcept
        {
            // digits always has to be done with unsigned type
            // because signed type omits the sign bit
            constexpr auto dig8  = std::numeric_limits<std::uint_least8_t>::digits;
            constexpr auto dig16 = std::numeric_limits<std::uint_least16_t>::digits;
            constexpr auto dig32 = std::numeric_limits<std::uint_least32_t>::digits;
            constexpr auto dig64 = std::numeric_limits<std::uint_least64_t>::digits;

            if constexpr (N <= dig8) { return std::int_least8_t{}; }
            else if constexpr (N <= dig16) { return std::int_least16_t{}; }
            else if constexpr (N <= dig32) { return std::int_least32_t{}; }
            else if constexpr (N <= dig64) { return std::int_least64_t{}; }
            // explicit cast for clarity
            else { return static_cast<long long>(0); }
        }

    public:
        using type = decltype(least_signed_zero());
    };

    template <unsigned N>
    using least_signed_t = typename least_signed<N>::type;


    template <unsigned N>
    struct least_unsigned
    {
        using type = std::make_unsigned_t<least_signed_t<N>>;
    };

    template <unsigned N>
    using least_unsigned_t = typename least_unsigned<N>::type;


}  // namespace sbmr::_detail


#endif  // SBMR_DETAIL_INTEGER_TRAITS_HPP
