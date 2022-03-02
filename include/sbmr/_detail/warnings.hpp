#ifndef SBMR_DETAIL_WARNINGS_HPP
#define SBMR_DETAIL_WARNINGS_HPP


// usable repeatably in all scopes without ODR violation
#define SBMR_REQUIRE_SEMICOLON() static_assert(true)


// push and pop warnings
#if defined(__GNUC__)
    #define SBMR_WARNINGS_PUSH() _Pragma("GCC diagnostic push")
    #define SBMR_WARNINGS_POP()  _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
    #define SBMR_WARNINGS_PUSH() _Pragma("warning(push)")
    #define SBMR_WARNINGS_POP()  _Pragma("warning(pop)")
#else
    #define SBMR_WARNINGS_PUSH() SBMR_REQUIRE_SEMICOLON()
    #define SBMR_WARNINGS_POP()  SBMR_REQUIRE_SEMICOLON()
#endif


// warnings supported:
// - W_THROW_NOEXCEPT
// - W_THROW_UNCAUGHT

// warning groups supported:
// - WG_ASSERT_CONSTEVAL


// W_THROW_NOEXCEPT
#if defined(_MSC_VER)
    #define SBMR_WARNINGS_DISABLE_W_THROW_NOEXCEPT() \
        _Pragma("warning(disable : 4297)")
#endif
#ifndef SBMR_WARNINGS_DISABLE_W_THROW_NOEXCEPT
    #define SBMR_WARNINGS_DISABLE_W_THROW_NOEXCEPT() \
        SBMR_REQUIRE_SEMICOLON()
#endif


// W_THROW_UNCAUGHT
#if defined(__GNUC__) && !defined(__clang__)  // GCC
    #define SBMR_WARNINGS_DISABLE_W_THROW_UNCAUGHT() \
        _Pragma("GCC diagnostic ignored \"-Wterminate\"")
#elif defined(__has_warning)  // GCC doesn't support __has_warning
    #if __has_warning("-Wterminate")
        #define SBMR_WARNINGS_DISABLE_W_THROW_UNCAUGHT() \
            _Pragma("GCC diagnostic ignored \"-Wterminate\"")
    #endif
#endif
#ifndef SBMR_WARNINGS_DISABLE_W_THROW_UNCAUGHT
    #define SBMR_WARNINGS_DISABLE_W_THROW_UNCAUGHT() \
        SBMR_REQUIRE_SEMICOLON()
#endif


// WG_CONSTEVAL_ASSERT
#define SBMR_WARNINGS_DISABLE_WG_ASSERT_CONSTEVAL() \
    SBMR_WARNINGS_DISABLE_W_THROW_NOEXCEPT();       \
    SBMR_WARNINGS_DISABLE_W_THROW_UNCAUGHT()


#endif  // SBMR_DETAIL_WARNINGS_HPP
