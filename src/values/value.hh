//
// value.hh
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
#include "platform.hh"
#include <stdint.h>

#ifndef SIMPLE_VALUE
#include "nan_tagged.hh"
#include <string_view>
#include <vector>
#endif


namespace tails {

#ifdef SIMPLE_VALUE

    /// Type of values stored on the stack.
    ///
    /// This is just a simple example; you'll probably want a type that uses tag bits to represent
    /// multiple data types, like at least strings. But remember not to make `Value` larger than a
    /// pointer, or it'll bloat your code since \ref Instruction contains a `Value`.
    class Value {
    public:
        constexpr Value(double n = 0)                       :_number(n) { }
        constexpr Value(int n)                              :_number(n) { }

        constexpr double asNumber() const                   {return _number;}
        constexpr double asDouble() const                   {return _number;}
        constexpr int    asInt() const                      {return int(_number);}

        constexpr explicit operator bool() const            {return _number != 0.0;}

        constexpr bool operator== (const Value &v) const    {return _number == v._number;}

        int cmp(Value v) const {
            return _number == v._number ? 0 : (_number < v._number ? -1 : 1);
        }

        Value operator+(Value v) const  {return Value(asNumber() + v.asNumber());}
        Value operator-(Value v) const  {return Value(asNumber() - v.asNumber());}
        Value operator*(Value v) const  {return Value(asNumber() * v.asNumber());}
        Value operator/(Value v) const  {return Value(asNumber() / v.asNumber());}
        Value operator%(Value v) const  {return Value(asInt() % v.asInt());}

        // A minimal type system with only one type:
        enum Type {
            ANumber,
        };
        constexpr Type type() const       {return ANumber;}
        static const char* typeName(Type) {return "number";}

    private:
        double _number;
    };

#else

    class Word;
    class CompiledWord;

    /// Type of values stored on the stack.
    ///
    /// This is a more complex implementation that can store numbers, strings, arrays, and
    /// "quotations" (anonymous words, aka lambdas.) This all still fits in 64 bits thanks to the
    /// magic of NaN Tagging.
    class Value : private NanTagged<void> {
    public:
        constexpr Value()          :NanTagged(nullptr) { }
        constexpr Value(nullptr_t) :Value() { }

        constexpr Value(double n)  :NanTagged(n) { }
        constexpr Value(int n)     :Value(double(n)) { }
        constexpr Value(size_t n)  :Value(double(n)) { }

        Value(const char* str);
        Value(const char* str, size_t len);

        Value(std::initializer_list<Value> arrayItems);
        Value(std::vector<Value>&&);

        explicit Value(CompiledWord*);

        enum Type {
            ANull,
            ANumber,
            AString,
            AnArray,
            AQuote,
        };

        Type type() const;
        static const char* typeName(Type);

        constexpr bool isNull() const       {return NanTagged::isNullPointer();}
        constexpr bool isDouble() const     {return NanTagged::isDouble();}
        constexpr bool isString() const     {return (asPointer() || isInline()) && tags() == kStringTag;}
        constexpr bool isArray() const      {return asPointer() && tags() == kArrayTag;}
        constexpr bool isQuote() const      {return asPointer() && tags() == kQuoteTag;}

        constexpr double asNumber() const   {return NanTagged::asDouble();}
        constexpr double asDouble() const   {return NanTagged::asDouble();}
        constexpr int    asInt() const      {return int(asDoubleOrZero());}
        std::string_view asString() const;
        std::vector<Value>* asArray() const;
        const Word*      asQuote() const;

        /// 'Truthiness' -- any Value except 0 and null is considered truthy.
        explicit operator bool() const;
        /// Equality comparison
        bool operator== (const Value &v) const;
        /// 3-way comparison, like the C++20 `<=>` operator.
        int cmp(Value v) const;

        // Arithmetic operators. `+` is overloaded to concatenate strings and arrays.
        Value operator+ (Value v) const;
        Value operator- (Value v) const;
        Value operator* (Value v) const;
        Value operator/ (Value v) const;
        Value operator% (Value v) const;

        /// Returns the length of a string or array; not valid for other types.
        Value length() const;

        /// Marks this value as in use during garbage collection. (See `gc.hh` for main GC API.)
        void mark() const;

    private:
        enum { kStringTag = 0, kArrayTag = 1, kQuoteTag = 2, };

        char* allocString(size_t len);
    };

    constexpr Value NullValue;

#endif

    static inline bool operator!= (const Value &a, const Value &b) {return !(a == b);}
    static inline bool operator>  (const Value &a, const Value &b) {return a.cmp(b) > 0;}
    static inline bool operator>= (const Value &a, const Value &b) {return a.cmp(b) >= 0;}
    static inline bool operator<  (const Value &a, const Value &b) {return a.cmp(b) < 0;}
    static inline bool operator<= (const Value &a, const Value &b) {return a.cmp(b) <= 0;}
}
