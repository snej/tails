//
// platform.hh
//
// Copyright (C) 2021 Jens Alfke. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once


// The __has_xxx() macros are only(?) implemented by Clang. (Except GCC has __has_attribute...)
// Define them to return 0 on other compilers.
// https://clang.llvm.org/docs/AttributeReference.html
// https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html

#ifndef __has_attribute
    #define __has_attribute(x) 0
#endif

#ifndef __has_builtin
    #define __has_builtin(x) 0
#endif

#ifndef __has_feature
    #define __has_feature(x) 0
#endif

#ifndef __has_extension
    #define __has_extension(x) 0
#endif


// Magic function attributes!
//
// Note: The `musttail` attribute is new (as of early 2021) and only supported by Clang.
// Without it, Tails ops will not use tail recursion _in unoptimized builds_, meaning
// the call stack will grow with every word called and eventually overflow.
// <https://clang.llvm.org/docs/AttributeReference.html#id398>
#ifdef __has_attribute
#   if __has_attribute(musttail)
#       define MUSTTAIL [[clang::musttail]]
#   endif
#   if __has_attribute(always_inline)
#       define ALWAYS_INLINE [[gnu::always_inline]]
#   endif
#   if __has_attribute(noinline)
#       define NOINLINE [[gnu::noinline]]
#   endif
#endif
#ifndef MUSTTAIL
#   define MUSTTAIL
#endif
#ifndef ALWAYS_INLINE
#   define ALWAYS_INLINE
#endif
#ifndef NOINLINE
#   define NOINLINE
#endif


// `_pure` functions are _read-only_. They cannot write to memory (in a way that's detectable),
// and they cannot access volatile data or do I/O.
//
// Calling a pure function twice in a row with the same arguments MUST return the same result.
// This guarantee allows the compiler to optimize out redundant calls.
//
// "Many functions have no effects except the return value, and their return value depends only on
//  the parameters and/or global variables. Such a function can be subject to common subexpression
//  elimination and loop optimization just as an arithmetic operator would be. These functions
//  should be declared with the attribute pure.
// "The pure attribute prohibits a function from modifying the state of the program that is
//  observable by means other than inspecting the function’s return value. However, functions
//  declared with the pure attribute can safely read any non-volatile objects, and modify the value
//  of objects in a way that does not affect their return value or the observable state of the
//  program." -- GCC manual
#if defined(__GNUC__) || __has_attribute(__pure__)
    #define _pure                      __attribute__((__pure__))
#else
    #define _pure
#endif
