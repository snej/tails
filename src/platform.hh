//
// platform.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once


// Magic function attributes!
//
//  Note: The `musttail` attribute is new (as of early 2021) and only supported by Clang.
//  Without it, ops will not use tail recursion _in unoptimized builds_, meaning
//  the call stack will grow with every word called and eventually overflow.
//  <https://clang.llvm.org/docs/AttributeReference.html#id398>
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
