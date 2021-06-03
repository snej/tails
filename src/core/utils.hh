//
// utils.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "stdint.h"
#include "stdio.h"
#include <string>

namespace tails {

    static inline std::string format(const char *fmt, ...) {
        char *str = nullptr;
        va_list args;
        va_start(args, fmt);
        vasprintf(&str, fmt, args);
        va_end(args);
        std::string result(str);
        free(str);
        return result;
    }


    // Constexpr equivalents of strlen and memcmp, for use in constexpr functions:

    constexpr static inline size_t _strlen(const char *str) noexcept {
        if (!str)
            return 0;
        auto c = str;
        while (*c) ++c;
        return c - str;
    }

    constexpr static inline bool _isalpha(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    constexpr static inline bool _compare(const char *a, const char *b, size_t len) {
        while (len-- > 0)
            if (*a++ != *b++)
                return false;
        return true;
    }


    template <typename T>
    constexpr static inline int _cmp(T a, T b)    {return (a==b) ? 0 : ((a<b) ? -1 : 1);}


}
