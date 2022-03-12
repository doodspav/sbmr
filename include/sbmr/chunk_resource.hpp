#ifndef SBMR_CHUNK_RESOURCE_HPP
#define SBMR_CHUNK_RESOURCE_HPP


#include <bit>
#include <cstddef>
#include <new>
#include <ostream>
#include <type_traits>
#include <utility>

#include <sbmr/bad_alloc.hpp>
#include <sbmr/resource_options.hpp>

#include <sbmr/_impl/chunk_resource_runtime.hpp>
#include <sbmr/_impl/chunk_resource_consteval.hpp>


namespace sbmr {


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
        using impl_runtime   = _impl::chunk_resource_runtime<Opts.normalized()>;
        using impl_consteval = _impl::chunk_resource_consteval;

        // data members
        impl_runtime   m_runtime;
        impl_consteval m_consteval;

        // de-allocates the storage pointed to by ptr
        // pre-conditions: ptr obtained from allocate_bytes(n, ...)
        static constexpr void
        do_deallocate_bytes(chunk_resource& cr, void *ptr, size_type) noexcept
        {
            // check nullptr and zero block
            if (ptr == nullptr || ptr == &impl_runtime::s_zero_block) { return; }

            // check pre-conditions
            SBMR_ASSERTM_CONSTEXPR(cr.m_runtime.is_owned(ptr), "invalid pointer");
            SBMR_ASSERTM_CONSTEXPR(cr.m_runtime.is_allocated(ptr) != -1, "double free");

            // perform de-allocation
            auto token = cr.m_runtime.is_allocated(ptr);
            cr.m_runtime.return_block_unchecked(token);
        }

        // de-allocates the storage pointed to by ptr
        // pre-conditions: ptr obtained from allocate_object(n, ...)
        template <class T>
            requires std::is_object_v<T>
        static constexpr void
        do_deallocate_object(chunk_resource& cr, T *ptr, size_type n) noexcept
        {
            // check nullptr
            if (ptr == nullptr) { return; }

            // allocated using m_consteval
            if (std::is_constant_evaluated())
            {
                // check pre-conditions
                SBMR_ASSERTM_CONSTEVAL(cr.m_consteval.is_maybe_allocated(ptr), "invalid pointer");
                SBMR_ASSERTM_CONSTEVAL(cr.m_consteval.is_allocated(ptr, n) != -1, "invalid size");

                // perform de-allocation
                auto token = cr.m_consteval.is_allocated(ptr, n);
                cr.m_consteval.return_ptr_unchecked(ptr, n, token);
            }
            // allocated using m_runtime
            else
            {
                // don't defer to do_deallocate_bytes even though it's identical
                // would be confusing to call this and have assertion come from
                //   the other do_deallocate function

                // check zero block (already checked nullptr)
                if (ptr == &impl_runtime::s_zero_block) { return; }

                // check pre-conditions
                SBMR_ASSERTM_CONSTEXPR(cr.m_runtime.is_owned(ptr), "invalid pointer");
                SBMR_ASSERTM_CONSTEXPR(cr.m_runtime.is_allocated(ptr) != -1, "double free");

                // perform de-allocation
                auto token = cr.m_runtime.is_allocated(ptr);
                cr.m_runtime.return_block_unchecked(token);
            }
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
                count -= m_consteval.m_ptrs.size();
            }
            return count;
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
        [[gnu::malloc(do_deallocate_bytes, 2)]]
        [[gnu::assume_aligned(options().block_align)]]
        [[gnu::returns_nonnull, gnu::alloc_size(2)]]
        [[nodiscard]] constexpr void *
        allocate_bytes(size_type n)
        {
            // check size in range
            if (n > options().block_size) {
                throw bad_alloc_unsupported_size(n, options().block_size);
            }

            // check availability
            if (available_blocks() == 0) {
                throw bad_alloc_out_of_memory();
            }

            // success
            return m_runtime.obtain_ptr_unchecked();
        }

        // allocates n bytes of storage, checking align meets requirements
        // allocation may persist from compile-time into runtime
        // throws a subtype of std::bad_alloc on failure
        [[gnu::malloc(do_deallocate_bytes, 2)]]
        [[gnu::assume_aligned(options().block_align)]]
        [[gnu::returns_nonnull, gnu::alloc_size(2), gnu::alloc_align(3)]]
        [[nodiscard]] constexpr void *
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
        [[gnu::malloc(do_deallocate_bytes, 2)]]
        [[gnu::assume_aligned(options().block_align)]]
        [[gnu::alloc_size(2)]]
        [[nodiscard]] constexpr void *
        allocate_bytes(size_type n, const std::nothrow_t&) noexcept
        {
            // check size in range and availability
            if ((n > options().block_size) || (available_blocks() == 0)) {
                return nullptr;
            }

            // success
            return m_runtime.obtain_ptr_unchecked();
        }

        // allocates n bytes of storage, checking align meets requirements
        // allocation may persist from compile-time into runtime
        // returns nullptr on failure (which need not be de-allocated)
        [[gnu::malloc(do_deallocate_bytes, 2)]]
        [[gnu::assume_aligned(options().block_align)]]
        [[gnu::alloc_size(2), gnu::alloc_align(3)]]
        [[nodiscard]] constexpr void *
        allocate_bytes(size_type n, align_type align, const std::nothrow_t&) noexcept
        {
            auto a = static_cast<size_type>(align);

            // check align is power of 2 and in range
            if ((!std::has_single_bit(a)) || (a > options().block_align)) {
                return nullptr;
            }

            // success
            return allocate_bytes(n, std::nothrow);
        }

        // de-allocates the storage pointed to by ptr
        // pre-conditions: ptr obtained from allocate_bytes(n, ...)
        constexpr void
        deallocate_bytes(void *ptr, size_type n) noexcept
        {
            do_deallocate_bytes(*this, ptr, n);
        }

        // de-allocates the storage pointed to by ptr
        // pre-conditions: ptr obtained from allocate_object(n, ...)
        template <class T>
            requires std::is_object_v<T>
        constexpr void
        deallocate_object(T *ptr, size_type n) noexcept
        {
            do_deallocate_object<T>(*this, ptr, n);
        }
    };


}  // namespace sbmr


#endif  // SBMR_CHUNK_RESOURCE_HPP
