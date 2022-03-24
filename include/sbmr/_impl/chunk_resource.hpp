#ifndef SBMR_IMPL_CHUNK_RESOURCE_HPP
#define SBMR_IMPL_CHUNK_RESOURCE_HPP


#include <sbmr/resource_options.hpp>

#include <sbmr/_impl/chunk_resource_consteval.hpp>
#include <sbmr/_impl/chunk_resource_runtime.hpp>


namespace sbmr::_impl {


    // this implementation exists to provide uniform access to both consteval
    //   and runtime implementations, even though internally one is a member of
    //   the other

    template <chunk_options Opts>
        requires ValidChunkOptions<Opts.normalized()>
    class chunk_resource
    {
    public:

        // default constructor
        constexpr chunk_resource() noexcept = default;

        // memory resource should not be copyable or movable
        chunk_resource(const chunk_resource&) = delete;
        chunk_resource(chunk_resource&&)      = delete;

        // memory resource should not be copy or move assignable
        auto& operator=(const chunk_resource&) = delete;
        auto& operator=(chunk_resource&&)      = delete;

        // member types
        using impl_consteval_type = chunk_resource_consteval;
        using impl_runtime_type   = chunk_resource_runtime<Opts>;

    private:

        // data members
        // impl_consteval_type m_consteval;  accessible as m_runtime.m_consteval
        impl_runtime_type m_runtime;

    public:

        // reference to consteval resource
        [[nodiscard]] constexpr impl_consteval_type&
        impl_consteval() noexcept
        {
            return m_runtime.m_consteval;
        }

        // const reference to consteval resource
        [[nodiscard]] constexpr const impl_consteval_type&
        impl_consteval() const noexcept
        {
            return m_runtime.m_consteval;
        }

        // reference to runtime resource
        [[nodiscard]] constexpr impl_runtime_type&
        impl_runtime() noexcept
        {
            return m_runtime;
        }

        // const reference to runtime resource
        [[nodiscard]] constexpr const impl_runtime_type&
        impl_runtime() const noexcept
        {
            return m_runtime;
        }
    };


}  // namespace sbmr::_impl


#endif  // SBMR_IMPL_CHUNK_RESOURCE_HPP
