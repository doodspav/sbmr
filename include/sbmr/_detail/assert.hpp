#ifndef SBMR_DETAIL_ASSERT_HPP
#define SBMR_DETAIL_ASSERT_HPP


#include <cassert>
#include <exception>
#include <utility>


namespace sbmr::_detail {


    // this class is only thrown in constexpr contexts as an assertion
    // it is not intended to be caught, so it saves no state

    struct constexpr_assert_exception
        : public std::exception
    {
        // NOLINTNEXTLINE (misc-forwarding-reference-overload)
        explicit constexpr_assert_exception(auto&&) noexcept
        {}
    };


}  // namespace sbmr::_detail


#define SBMR_ASSERT(expr) \
    assert(expr)

#define SBMR_ASSERTM(expr, msg) \
    assert((expr) && (msg))


#define SBMR_ASSERT_CONSTEXPR(expr)                                       \
    do {                                                                  \
        if (std::is_constant_evaluated()) {                               \
            if (!static_cast<bool>(expr)) {                               \
                throw ::sbmr::_detail::constexpr_assert_exception(#expr); \
            }                                                             \
        }                                                                 \
        else { SBMR_ASSERT(expr); }                                       \
    }                                                                     \
    while (0)

#define SBMR_ASSERTM_CONSTEXPR(expr, msg)                               \
    do {                                                                \
        if (std::is_constant_evaluated()) {                             \
            if (!static_cast<bool>(expr)) {                             \
                throw ::sbmr::_detail::constexpr_assert_exception(msg); \
            }                                                           \
        }                                                               \
        else { SBMR_ASSERTM(expr, msg); }                               \
    }                                                                   \
    while (0)


#define SBMR_ASSERT_CONSTEVAL(expr)                                       \
    do {                                                                  \
        if (std::is_constant_evaluated()) {                               \
            if (!static_cast<bool>(expr)) {                               \
                throw ::sbmr::_detail::constexpr_assert_exception(#expr); \
            }                                                             \
        }                                                                 \
    }                                                                     \
    while (0)

#define SBMR_ASSERTM_CONSTEVAL(expr, msg)                               \
    do {                                                                \
        if (std::is_constant_evaluated()) {                             \
            if (!static_cast<bool>(expr)) {                             \
                throw ::sbmr::_detail::constexpr_assert_exception(msg); \
            }                                                           \
        }                                                               \
    }                                                                   \
    while (0)


#endif  // SBMR_DETAIL_ASSERT_HPP
