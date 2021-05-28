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
#include "nan_tagged.hh"
#include <iosfwd>
#include <string_view>
#include <vector>


namespace tails {

#ifdef SIMPLE_VALUE

    /// Type of values stored on the stack.
    ///
    /// This is just a simple example; you'll probably want a type that uses tag bits to represent
    /// multiple data types, like at least strings. But remember not to make `Value` larger than a
    /// pointer, or it'll bloat your code since \ref Instruction contains a `Value`.
    class Value {
    public:
        constexpr Value(double n = 0)       :_number(n) { }
        constexpr Value(int n)              :_number(n) { }
        explicit Value(const char*, size_t=0);

        constexpr double asNumber() const {return _number;}
        constexpr double asDouble() const {return _number;}
        constexpr int    asInt() const    {return int(_number);}

        constexpr explicit operator bool() const {return _number != 0.0;}

        constexpr bool operator== (const Value &v) const {return _number == v._number;}

        int cmp(Value v) const {return _number == v._number ? 0 : (_number < v._number ? -1 : 1);}

        Value operator+(Value v) const  {return Value(asNumber() + v.asNumber());}
        Value operator-(Value v) const  {return Value(asNumber() - v.asNumber());}
        Value operator*(Value v) const  {return Value(asNumber() * v.asNumber());}
        Value operator/(Value v) const  {return Value(asNumber() / v.asNumber());}
        Value operator%(Value v) const  {return Value(asInt() % v.asInt());}

    private:
        double _number;
    };

#else

    class Word;

    /// Type of values stored on the stack.
    ///
    /// This is a more complex implementation that can store numbers, strings and arrays.
    /// (But there is no garbage collector, so strings and arrays just leak...)
    class Value : private NanTagged<char> {
    public:
        using Array = std::vector<Value>;

        constexpr Value()          :NanTagged(nullptr) { }
        constexpr Value(nullptr_t) :Value() { }
        constexpr Value(double n)  :NanTagged(n) { }
        constexpr Value(int n)     :Value(double(n)) { }
        constexpr Value(size_t n)  :Value(double(n)) { }

        Value(const char* str);
        Value(const char* str, size_t len);

        Value(std::initializer_list<Value> arrayItems);
        Value(Array *array);

        Value(const Word*);

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
        Array*           asArray() const;
        const Word*      asQuote() const;

        constexpr explicit operator bool() const {
            if (isDouble())
                return asDouble() != 0;
            else
                return isString();
        }

        bool operator== (const Value &v) const;
        int cmp(Value v) const;

        Value length() const;

        Value operator+ (Value v) const;
        Value operator- (Value v) const;
        Value operator* (Value v) const;
        Value operator/ (Value v) const;
        Value operator% (Value v) const;

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

    std::ostream& operator<< (std::ostream&, Value);
}
