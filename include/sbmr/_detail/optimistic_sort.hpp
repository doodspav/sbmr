#ifndef SBMR_DETAIL_OPTIMISTIC_SORT_HPP
#define SBMR_DETAIL_OPTIMISTIC_SORT_HPP


#include <functional>
#include <iterator>
#include <utility>


namespace sbmr::_detail {


    // optimistic_sort assumes that input is (almost) sorted
    // based on insertion sort, with branch hints assuming input is sorted

    // no conditional noexcept because library only uses this with It and Sen
    //   being pointers to integer types (and conditional noexcept specification
    //   would take 6 lines)

    template <class It, class Sen, class Cmp = std::less<void>>
        requires std::bidirectional_iterator<It> &&
                 std::sentinel_for<Sen, It>      &&
                 std::sortable<It, Cmp>
    constexpr void
    optimistic_sort(It it, Sen sen, Cmp cmp = {}) noexcept
    {
        // check for empty range
        if (it == sen) { return; }

        // sort elements
        for (const It first = it; ++it, it != sen;)
        {
            It rhs = it;
            It lhs = it;
            --lhs;

            if (!std::invoke(cmp, *lhs, *it)) [[unlikely]]
            {
                // store rightmost element
                auto key = std::move(*it);

                // rotate-right
                do {
                    [[unlikely]] *rhs = std::move(*lhs);
                }
                while ((--rhs != first) && !std::invoke(cmp, *(--lhs), key));

                // rotate-right ends
                *rhs = std::move(key);
            }
        }
    }


}  // namespace sbmr::_detail


#endif  // SBMR_DETAIL_OPTIMISTIC_SORT_HPP
