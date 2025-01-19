cmake_minimum_required(VERSION 3.20)

project( tails
        VERSION     0.1.0
        DESCRIPTION "Minimal Forth-like language"
        LANGUAGES   CXX C
)


set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD            11)
set(CMAKE_C_STANDARD_REQUIRED   ON)


#### CONFIG


if (NOT(CMAKE_BUILD_TYPE STREQUAL "Debug"))
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)    # Enable link-time optimization
endif()


if (MSVC)
    # MSVC:
    add_definitions(-DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00 -DNOMINMAX)
else()
    # Clang & GCC:
    add_compile_options(
            -Werror
            -Wall
            -Wpedantic
            -Wno-gnu-case-range
            -Wno-vla-extension
            -Wno-unknown-pragmas
            -Wno-unknown-warning-option
    )
    if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
        # GCC-specific:
        add_compile_options(
                -Wno-psabi          # suppress annoying GCC ABI warning
                -Wno-sign-compare   # apparently comes with `pedantic` in GCC
                -D_FORTIFY_SOURCE=2 # static+dynamic buffer-overflow checks
        )
    else()
        # Clang-specific:
        add_compile_options(
                -Wno-nullability-extension
                -Wno-gnu-zero-variadic-macro-arguments
                -Wno-gnu-statement-expression-from-macro-expansion
                -Wno-ambiguous-reversed-operator
        )
    endif()
endif()

add_compile_options(
        $<$<CONFIG:Debug>:-fsanitize=address>
        $<$<CONFIG:Debug>:-fsanitize=undefined>
        
        $<$<CONFIG:Release>:-Ofast>     # This improves speed, at least w/Clang 16
)

add_link_options(
        $<$<CONFIG:Debug>:-fsanitize=address>
)


add_library( libtails STATIC
        src/compiler/compiler.cc
        src/compiler/parser.cc
        src/compiler/vocabulary.cc
        src/core/core_words.cc
        src/values/gc.cc
        src/values/value.cc
        src/more_words.cc
)

target_include_directories( libtails PUBLIC
        src/
        src/compiler/
        src/core/
        src/values/
)

# These flags _should_ speed up the interpreter by removing extra stack manipulation instructions,
# but as of Jan 2025 (M1 MacBook Pro, AppleClang 16) they seem to make no difference:
#target_compile_options(libtails PRIVATE
#        $<$<CONFIG:Release>:-fomit-frame-pointer>
#        $<$<CONFIG:Release>:-fno-stack-protector>
#)


#### TESTS


add_executable( tails_tests
        src/test.cc
)

target_link_libraries( tails_tests
        libtails
)


#### CLI


add_executable( tails
        src/repl.cc
        vendor/linenoise/linenoise.c
        vendor/linenoise/utf8.c
)

target_include_directories( tails PRIVATE
        vendor/linenoise/
)

target_link_libraries( tails
        libtails
)
