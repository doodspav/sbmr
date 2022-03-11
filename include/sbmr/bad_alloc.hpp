#ifndef SBMR_BAD_ALLOC_HPP
#define SBMR_BAD_ALLOC_HPP


#include <algorithm>
#include <charconv>
#include <cstddef>
#include <limits>
#include <new>
#include <string_view>


namespace sbmr {


    struct bad_alloc
        : public std::bad_alloc
    {
    public:

        [[nodiscard]] const char *
        what() const noexcept override
        {
            return "sbmr::bad_alloc";
        }

    protected:

        static constexpr std::size_t UZN = 1 + std::numeric_limits<std::size_t>::digits10;

        [[nodiscard]] static char *
        sprint_size(char *first, char *last, std::size_t size) noexcept
        {
            auto res = std::to_chars(first, last, size);
            if (res.ec != std::errc::value_too_large) { first = res.ptr; }
            else {
                for (int i = 0; i < 3 && first != last; ++i) { *first++ = '?'; }
            }
            return first;
        }
    };


    struct bad_alloc_out_of_memory
        : public bad_alloc
    {
        [[nodiscard]] const char *
        what() const noexcept override
        {
            return "memory resource is out of blocks";
        }
    };


    class bad_alloc_unsupported_size
        : public bad_alloc
    {
    public:

        bad_alloc_unsupported_size(std::size_t size, std::size_t max_size) noexcept
            : m_size{size},
              m_max_size{max_size}
        {
            char *it = std::data(m_what);
            it = sprint_size(it, it + UZN, m_size);
            it = std::ranges::copy(sva, it).out;
            it = sprint_size(it, it + UZN, m_max_size);
            it = std::ranges::copy(svb, it).out;
            *it = '\0';
        }

        [[nodiscard]] const char *
        what() const noexcept override
        {
            return m_what;
        }

        [[nodiscard]] std::size_t
        size() const noexcept
        {
            return m_size;
        }

        [[nodiscard]] std::size_t
        max_size() const noexcept
        {
            return m_max_size;
        }

    private:

        static constexpr std::string_view sva = " exceeds ";
        static constexpr std::string_view svb = ", the max size supported by the memory resource";

        char m_what[UZN + sva.size() + UZN + svb.size() + 1]{};
        std::size_t m_size;
        std::size_t m_max_size;
    };


    class bad_alloc_unsupported_align
        : public bad_alloc
    {
    public:

        bad_alloc_unsupported_align(std::align_val_t align, std::align_val_t max_align) noexcept
            : bad_alloc_unsupported_align(align, static_cast<std::size_t>(max_align))
        {}

        bad_alloc_unsupported_align(std::align_val_t align, std::size_t max_align) noexcept
            : bad_alloc_unsupported_align(static_cast<std::size_t>(align), max_align)
        {}

        bad_alloc_unsupported_align(std::size_t align, std::size_t max_align) noexcept
            : m_align{align},
              m_max_align{max_align}
        {
            char *it = std::data(m_what);
            it = sprint_size(it, it + UZN, m_align);
            it = std::ranges::copy(sva, it).out;
            it = sprint_size(it, it + UZN, m_max_align);
            it = std::ranges::copy(svb, it).out;
            *it = '\0';
        }

        [[nodiscard]] const char *
        what() const noexcept override
        {
            return m_what;
        }

        [[nodiscard]] std::size_t
        align() const noexcept
        {
            return m_align;
        }

        [[nodiscard]] std::size_t
        max_align() const noexcept
        {
            return m_max_align;
        }

    private:

        static constexpr std::string_view sva = " exceeds ";
        static constexpr std::string_view svb = ", the max alignment supported by the memory resource";

        char m_what[UZN + sva.size() + UZN + svb.size() + 1]{};
        std::size_t m_align;
        std::size_t m_max_align;
    };


    class bad_alloc_invalid_align
        : public bad_alloc
    {
    public:

        explicit bad_alloc_invalid_align(std::align_val_t align) noexcept
            : bad_alloc_invalid_align(static_cast<std::size_t>(align))
        {}

        explicit bad_alloc_invalid_align(std::size_t align) noexcept
            : m_align{align}
        {
            char *it = std::data(m_what);
            it = sprint_size(it, it + UZN, m_align);
            it = std::ranges::copy(sva, it).out;
            *it = '\0';
        }

        [[nodiscard]] const char *
        what() const noexcept override
        {
            return m_what;
        }

        [[nodiscard]] std::size_t
        align() const noexcept
        {
            return m_align;
        }

    private:

        static constexpr std::string_view sva = " is not a valid alignment, must be a power of 2";

        char m_what[UZN + sva.size() + 1]{};
        std::size_t m_align;
    };


}  // namespace sbmr


#endif  // SBMR_BAD_ALLOC_HPP
