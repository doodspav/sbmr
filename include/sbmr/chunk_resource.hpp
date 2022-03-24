#ifndef SBMR_CHUNK_RESOURCE_HPP
#define SBMR_CHUNK_RESOURCE_HPP


#include <bit>
#include <cstddef>
#include <limits>
#include <new>
#include <ostream>
#include <type_traits>
#include <utility>

#include <sbmr/bad_alloc.hpp>
#include <sbmr/resource_options.hpp>

#include <sbmr/_impl/chunk_resource_runtime.hpp>
#include <sbmr/_impl/chunk_resource_consteval.hpp>


namespace sbmr {


    // gnu::malloc does not seem to accept templated or member functions
    // the workaround used here is to have dummy global functions that can be
    //   passed to gnu::malloc and are called in the appropriate de-allocators

    namespace _impl {


        // no-op
        // can be passed to gnu::malloc attribute, and should then be called
        //   at the start of de-allocation to satisfy sanitizers
        constexpr void
        chunk_deallocate_bytes_noop_for_sanitizers(void *) noexcept
        {}

        // no-op
        // can be passed to gnu::malloc attribute, and should then be called
        //   at the start of de-allocation to satisfy sanitizers
        constexpr void
        chunk_deallocate_object_noop_for_sanitizers(void *) noexcept
        {}


    }  // namespace _impl


    template <chunk_options Opts>
        requires ValidChunkOptions<Opts.normalized()>
    class chunk_resource
    {
    public:

        // member types
        using size_type    = std::size_t;
        using align_type   = std::align_val_t;
        using options_type = chunk_options;

    private:

        // impl types
        using impl_consteval = _impl::chunk_resource_consteval;
        using impl_runtime   = _impl::chunk_resource_runtime<Opts.normalized()>;

        // data members
        impl_consteval m_consteval;
        impl_runtime   m_runtime;

        // checks n * sz is a valid array size
        // i.e. it doesn't overflow std::size_t or std::ptrdiff_t
        [[nodiscard]] static constexpr bool
        check_no_overflow(size_type n, size_type sz) noexcept
        {
            // both can be 0 since calloc(0, 0) is valid
            // but in our case, sz will never be 0
            // since sz is obtained directly from sizeof(T)
            SBMR_ASSERT_CONSTEXPR(sz != 0);
            if (n == 0 || sz == 0) { return true; }

            // defer to function from resource_options (flipped args)
            else { return _detail::valid_sizeof(sz, n); }
        }

    public:

        // default constructor
        constexpr chunk_resource() noexcept = default;

        // memory resource should not be copyable or movable
        chunk_resource(const chunk_resource&) = delete;
        chunk_resource(chunk_resource&&)      = delete;

        // memory resource should not be copy or move assignable
        auto& operator=(const chunk_resource&) = delete;
        auto& operator=(chunk_resource&&)      = delete;

        // returns Opts.normalized()
        [[nodiscard]] static constexpr options_type
        options() noexcept
        {
            return impl_runtime::s_options;
        }

        // returns the number of blocks available to be allocated
        // if value is 0, allocation will unconditionally fail
        [[nodiscard]] constexpr size_type
        available_blocks() const noexcept
        {
            auto count = m_runtime.m_available_blocks;
            if (std::is_constant_evaluated()) {
                using count_type = typename impl_runtime::block_count_type;
                count -= static_cast<count_type>(m_consteval.allocation_count());
            }
            return static_cast<size_type>(count);
        }

        // this is NOT a check for whether a pointer is valid to deallocate
        //   it may return true for invalid pointers, and false for nullptr and
        //   the pointer returned by allocate(0)
        // this function is intended to help disambiguate memory between two
        //   different chunk_resource objects with non-overlapping memory
        // if this function returns true, it is guaranteed that the pointer is
        //   not owned by another chunk_resource object that doesn't overlap
        //   memory with this one
        [[nodiscard]] constexpr bool
        maybe_owns(const void *unknown_ptr) const noexcept
        {
            if (std::is_constant_evaluated()) {
                return m_consteval.is_maybe_allocated(unknown_ptr) ||
                       m_runtime.is_maybe_owned(unknown_ptr);
            }
            else { return m_runtime.is_maybe_owned(unknown_ptr); }
        }

        // may improve memory locality for subsequent allocations following a
        //   stack-like cyclic allocation pattern if called at the beginning of
        //   every major cycle
        // prefer this to defrag_optimistic() if, until this call, allocations
        //   haven't followed such a pattern
        // is a no-op at compile time
        constexpr void
        defrag() noexcept
        {
            m_runtime.rsort_available_indexes();
        }

        // may improve memory locality for subsequent allocations following a
        //   stack-like cyclic allocation pattern if called at the beginning of
        //   every major cycle
        // prefer this to defrag() if, until this call, allocations have (mostly)
        //   followed such a pattern
        // if allocation patterns before this call fully follow such a pattern,
        //   then no call to this function is needed (would effectively be a no-op)
        // is a no-op at compile time
        constexpr void
        defrag_optimistic() noexcept
        {
            m_runtime.rsort_optimistic_available_indexes();
        }

        // equality comparison
        // no two distinct resource objects will compare equal
        [[nodiscard]] constexpr bool
        operator==(const chunk_resource& other) const noexcept
        {
            return this == &other;
        }

        // ostream operator
        // outputs: "chunk_resource<{.block_size=S, .block_align=A, .block_count=C}>"
        // values are from .options(), not from Opts template parameter
        friend std::ostream&
        operator<<(std::ostream& os, const chunk_resource& cr)
        {
            return os << "chunk_resource<" << cr.options() << '>';
        }

        // allocates n bytes of storage
        // allocation may persist from compile-time into runtime
        // throws a subtype of std::bad_alloc on failure
        [[gnu::malloc(_impl::chunk_deallocate_bytes_noop_for_sanitizers, 1)]]
        [[gnu::assume_aligned(options().block_align)]]
        [[gnu::returns_nonnull, gnu::alloc_size(2)]]
        [[nodiscard]] constexpr unsigned char *
        allocate_bytes(size_type n)
        {
            // check size in range
            if (n > options().block_size) {
                throw bad_alloc_unsupported_size(n, options().block_size);
            }

            // check availability (unless n == 0)
            else if (n != 0 && available_blocks() == 0) {
                throw bad_alloc_out_of_memory();
            }

            // success
            if (n == 0) {
                return const_cast<unsigned char *>(impl_runtime::zero_block_ptr());
            }
            else { return m_runtime.obtain_ptr_unchecked(); }
        }

        // allocates n bytes of storage, checking align meets requirements
        // allocation may persist from compile-time into runtime
        // throws a subtype of std::bad_alloc on failure
        [[gnu::malloc(_impl::chunk_deallocate_bytes_noop_for_sanitizers, 1)]]
        [[gnu::assume_aligned(options().block_align)]]
        [[gnu::returns_nonnull, gnu::alloc_size(2), gnu::alloc_align(3)]]
        [[nodiscard]] constexpr unsigned char *
        allocate_bytes(size_type n, align_type align)
        {
            auto a = static_cast<size_type>(align);

            // check align is power of 2
            if (!std::has_single_bit(a)) {
                throw bad_alloc_invalid_align(a);
            }

            // check align in range
            else if (a > options().block_align) {
                throw bad_alloc_unsupported_align(a, options().block_align);
            }

            // success
            return allocate_bytes(n);
        }

        // allocates n bytes of storage
        // allocation may persist from compile-time into runtime
        // returns nullptr on failure (which need not be de-allocated)
        [[gnu::malloc(_impl::chunk_deallocate_bytes_noop_for_sanitizers, 1)]]
        [[gnu::assume_aligned(options().block_align)]]
        [[gnu::alloc_size(2)]]
        [[nodiscard]] constexpr unsigned char *
        allocate_bytes(size_type n, const std::nothrow_t&) noexcept
        {
            // check size in range
            // check availability (unless n == 0)
            if ((n > options().block_size) ||
                (n != 0 && available_blocks() == 0)) {
                return nullptr;
            }

            // success
            if (n == 0) {
                return const_cast<unsigned char *>(impl_runtime::zero_block_ptr());
            }
            else { return m_runtime.obtain_ptr_unchecked(); }
        }

        // allocates n bytes of storage, checking align meets requirements
        // allocation may persist from compile-time into runtime
        // returns nullptr on failure (which need not be de-allocated)
        [[gnu::malloc(_impl::chunk_deallocate_bytes_noop_for_sanitizers, 1)]]
        [[gnu::assume_aligned(options().block_align)]]
        [[gnu::alloc_size(2), gnu::alloc_align(3)]]
        [[nodiscard]] constexpr unsigned char *
        allocate_bytes(size_type n, align_type align, const std::nothrow_t&) noexcept
        {
            auto a = static_cast<size_type>(align);

            // check align is power of 2
            // check align in range
            if ((!std::has_single_bit(a)) || (a > options().block_align)) {
                return nullptr;
            }

            // success
            return allocate_bytes(n, std::nothrow);
        }

        // allocates suitable storage for n objects of type T
        // allocation CANNOT persist from compile-time into runtime
        // throws a subtype of std::bad_alloc on failure
        template <class T>
            requires std::is_object_v<T>
        [[gnu::malloc(_impl::chunk_deallocate_object_noop_for_sanitizers, 1)]]
        [[gnu::assume_aligned(alignof(T))]]  // not from options() due to m_consteval
        [[gnu::returns_nonnull]]  // no param for gnu::alloc_size
        [[nodiscard]] constexpr T *
        allocate_object(size_type n)
        {
            // check n * sizeof(T) does not overflow std::size_t or std::ptrdiff_t
            auto size = n * sizeof(T);
            if (!check_no_overflow(n, sizeof(T))) {
                throw bad_alloc_array_length(n, sizeof(T));
            }

            // check alignof(T) in range
            else if (alignof(T) > options().block_align) {
                throw bad_alloc_unsupported_align(alignof(T), options().block_align);
            }

            // check size in range
            else if (size > options().block_size) {
                throw bad_alloc_unsupported_size(size, options().block_size);
            }

            // check availability (unless n == 0)
            else if (n != 0 && available_blocks() == 0) {
                throw bad_alloc_out_of_memory();
            }

            // success
            if (std::is_constant_evaluated()) {
                return m_consteval.obtain_ptr_unchecked<T>(n);
            }
            else {
                const unsigned char *ptr;
                if (n == 0) { ptr = impl_runtime::zero_block_ptr(); }
                else { ptr = m_runtime.obtain_ptr_unchecked(); }
                return const_cast<T *>(reinterpret_cast<const T *>(ptr));
            }
        }

        // allocates suitable storage for n objects of type T, checking align
        // allocation CANNOT persist from compile-time into runtime
        // throws a subtype of std::bad_alloc on failure
        template <class T>
            requires std::is_object_v<T>
        [[gnu::malloc(_impl::chunk_deallocate_object_noop_for_sanitizers, 1)]]
        [[gnu::assume_aligned(alignof(T))]]  // not from options() due to m_consteval
        [[gnu::returns_nonnull, gnu::alloc_align(3)]]  // no param for gnu::alloc_size
        [[nodiscard]] constexpr T *
        allocate_object(size_type n, align_type align)
        {
            auto a = static_cast<size_type>(align);

            // check align is a power of 2
            if (!std::has_single_bit(a)) {
                throw bad_alloc_invalid_align(a);
            }

            // check align in range
            else if (a > options().block_align) {
                throw bad_alloc_unsupported_align(a, options().block_align);
            }

            // if align < alignof(T) but is valid, align is ignored
            // alignof(T) < options().block_align is checked in call below

            // success
            return allocate_object<T>(n);
        }

        // allocates suitable storage for n objects of type T
        // allocation CANNOT persist from compile-time into runtime
        // returns nullptr on failure (which need not be de-allocated)
        template <class T>
            requires std::is_object_v<T>
        [[gnu::malloc(_impl::chunk_deallocate_object_noop_for_sanitizers, 1)]]
        [[gnu::assume_aligned(alignof(T))]]  // not from options() due to m_consteval
        // no param for gnu::alloc_size
        [[nodiscard]] constexpr T *
        allocate_object(size_type n, const std::nothrow_t&) noexcept
        {
            // check n * sizeof(T) does not overflow std::size_t or std::ptrdiff_t
            // check alignof(T) in range
            // check size in range
            // check availability (unless n == 0)
            if ((!check_no_overflow(n, sizeof(T)))     ||
                (alignof(T) > options().block_align)   ||
                (n * sizeof(T) > options().block_size) ||
                (n != 0 && available_blocks() == 0))
            { return nullptr; }

            // success
            if (std::is_constant_evaluated()) {
                return m_consteval.obtain_ptr_unchecked<T>(n);
            }
            else {
                const unsigned char *ptr;
                if (n == 0) { ptr = impl_runtime::zero_block_ptr(); }
                else { ptr = m_runtime.obtain_ptr_unchecked(); }
                return const_cast<T *>(reinterpret_cast<const T *>(ptr));
            }
        }

        // allocates suitable storage for n objects of type T, checking align
        // allocation CANNOT persist from compile-time into runtime
        // returns nullptr on failure (which need not be de-allocated)
        template <class T>
            requires std::is_object_v<T>
        [[gnu::malloc(_impl::chunk_deallocate_object_noop_for_sanitizers, 1)]]
        [[gnu::assume_aligned(alignof(T))]]  // not from options() due to m_consteval
        [[gnu::alloc_align(3)]]  // no params for gnu::alloc_size
        [[nodiscard]] constexpr T *
        allocate_object(size_type n, align_type align, const std::nothrow_t&) noexcept
        {
            auto a = static_cast<size_type>(align);

            // check align is a power of 2
            // check align in range
            if (!std::has_single_bit(a) || a > options().block_align) {
                return nullptr;
            }

            // if align < alignof(T) but is valid, align is ignored
            // alignof(T) < options().block_align is checked in call below

            // success
            return allocate_object<T>(n, std::nothrow);
        }

        // de-allocates the storage pointed to by ptr
        // pre-conditions: ptr obtained from allocate_bytes(n, ...)
        constexpr void
        deallocate_bytes(void *ptr, [[maybe_unused]] size_type n) noexcept
        {
            // inform sanitizers
            _impl::chunk_deallocate_bytes_noop_for_sanitizers(ptr);

            // check nullptr and zero block
            constexpr auto *p_zero = static_cast<const void *>(impl_runtime::zero_block_ptr());
            if (ptr == nullptr || ptr == p_zero) { return; }

            // check pre-conditions
            SBMR_ASSERTM_CONSTEXPR(m_runtime.is_owned(ptr), "invalid pointer");
            SBMR_ASSERTM_CONSTEXPR(m_runtime.is_allocated(ptr) != -1, "double free");

            // perform de-allocation
            auto token = m_runtime.is_allocated(ptr);
            m_runtime.return_block_unchecked(token);
        }

        // de-allocates the storage pointed to by ptr
        // pre-conditions: ptr obtained from allocate_object(n, ...)
        template <class T>
            requires std::is_object_v<T>
        constexpr void
        deallocate_object(T *ptr, size_type n) noexcept
        {
            // inform sanitizers
            _impl::chunk_deallocate_object_noop_for_sanitizers(ptr);

            // check nullptr
            if (ptr == nullptr) { return; }

            // allocated using m_consteval
            if (std::is_constant_evaluated())
            {
                // check pre-conditions
                SBMR_ASSERTM_CONSTEVAL(m_consteval.is_maybe_allocated(ptr), "invalid pointer");
                SBMR_ASSERTM_CONSTEVAL(m_consteval.is_allocated(ptr, n) != -1, "invalid size");

                // perform de-allocation
                auto token = m_consteval.is_allocated(ptr, n);
                m_consteval.return_ptr_unchecked(ptr, n, token);
            }
            // allocated using m_runtime
            else
            {
                // don't defer to deallocate_bytes even though it's identical
                // would be confusing to call this function and have assertion
                //   come from other deallocate function

                // check zero block (already checked nullptr)
                constexpr auto *p_zero = static_cast<const void *>(impl_runtime::zero_block_ptr());
                if (ptr == p_zero) { return; }

                // check pre-conditions
                SBMR_ASSERTM_CONSTEXPR(m_runtime.is_owned(ptr), "invalid pointer");
                SBMR_ASSERTM_CONSTEXPR(m_runtime.is_allocated(ptr) != -1, "double free");

                // perform de-allocation
                auto token = m_runtime.is_allocated(ptr);
                m_runtime.return_block_unchecked(token);
            }
        }
    };


}  // namespace sbmr


#endif  // SBMR_CHUNK_RESOURCE_HPP
