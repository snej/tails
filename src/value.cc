//
// value.cc
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

#include "value.hh"
#include <iomanip>
#include <iostream>
#include <string.h>

namespace tails {
    using namespace std;

#ifdef SIMPLE_VALUE

    Value::Value(const char*, size_t) {
        throw std::runtime_error("Value doesn't support strings in SIMPLE_VALUE configuration");
    }

    std::ostream& operator<< (std::ostream &out, Value value) {
        return out << value.asDouble();
    }

#else

    Value::Value(const char* str)
    :Value(str, (str ? strlen(str) : 0))
    { }


    Value::Value(const char* str, size_t len)
    :NanTagged((void**)0)
    {
        if (str == nullptr) {
            assert(len == 0);
            setPointer(nullptr);
        } else {
            char *dst = allocString(len);
            memcpy(dst, str, len);
        }
    }


    // Makes me a string with space for a string `len` bytes long.
    // Returns a pointer to the storage.
    char* Value::allocString(size_t len) {
        if (len <= NanTagged::kInlineCapacity) {
            return (char*)setInline();
        } else {
            char *heapStr = (char*) malloc(len + 1);
            if (!heapStr)
                throw std::bad_alloc();
            heapStr[len] = 0;
            setPointer(heapStr);
            return heapStr;
        }
    }


    string_view Value::asString() const {
        if (isInline()) {
            // The inline string ends before the first 0 byte, else at the end of the inline data.
            auto str = (const char*)asInline().data;
            size_t len = 0;
            while (str[len] != 0 && len < NanTagged::kInlineCapacity)
                ++len;
            return string_view(str, len);
        } else if (asPointer()) {
            return string_view(asPointer());
        } else {
            return string_view();
        }
    }


    bool Value::operator== (const Value &v) const {
        if (NanTagged::operator==(v))
            return true;
        else if (isNull() || v.isNull())
            return false;
        else
            return asString() == v.asString();
    }


    int Value::cmp(Value v) const {
        if (int nullA = isNull(), nullB = v.isNull(); nullA || nullB) {
            return nullB - nullA;
        } else if (isDouble()) {
            if (!v.isDouble())
                return -1;
            auto a = asDouble(), b = v.asDouble();
            return (a == b) ? 0 : ((a < b) ? -1 : 1);
        } else {
            if (v.isDouble())
                return 1;
            return asString().compare(v.asString());
        }
    }


    Value Value::operator+ (Value v) const {
        if (isDouble() || v.isDouble()) {
            // Addition:
            return Value(asDouble() + v.asDouble());
        } else {
            // String concatenation:
            auto str1 = asString(), str2 = v.asString();
            if (str1.size() == 0)
                return v;
            else if (str2.size() == 0)
                return *this;

            Value result;
            char *dst = result.allocString(str1.size() + str2.size());
            memcpy(dst,               str1.data(), str1.size());
            memcpy(dst + str1.size(), str2.data(), str2.size());
            return result;
        }
    }


    Value Value::operator- (Value v) const {
        return Value(asDouble() - v.asDouble());
    }


    Value Value::operator* (Value v) const {
        return Value(asDouble() * v.asDouble());
    }


    Value Value::operator/ (Value v) const {
        return Value(asDouble() / v.asDouble());
    }


    Value Value::operator% (Value v) const {
        // Modulo only operates on integers, and the denominator can't be zero:
        if (isDouble() && v.isDouble()) {
            if (int denom = v.asInt(); denom != 0)
                return Value(asInt() % denom);
        }
        return Value();
    }


    std::ostream& operator<< (std::ostream &out, Value value) {
        if (value.isDouble())
            out << value.asDouble();
        else if (value.isString())
            out << std::quoted(value.asString());
        else
            out << "null";
        return out;
    }

#endif

}
