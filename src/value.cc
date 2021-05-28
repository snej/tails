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


    Value::Value(std::initializer_list<Value> arrayItems)
    :NanTagged((void**)0)
    {
        auto array = new Array(arrayItems);
        setPointer((char*)array);
        setTags(kArrayTag);
    }


    Value::Value(Array *array)
    :NanTagged((void**)0)
    {
        setPointer((char*)array);
        setTags(kArrayTag);
    }


    Value::Value(const Word *word)
    :NanTagged((char*)word)
    {
        assert(word);
        setTags(kQuoteTag);
    }


    Value::Type Value::type() const {
        if (isDouble())
            return ANumber;
        else if (isNullPointer())
            return ANull;
        else
            return Type(int(AString) + tags());
    }


    const char* Value::typeName(Type type) {
        static constexpr const char* kNames[5] = {
            "null", "number", "string", "array", "quotation"
        };
        return kNames[type];
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
        if (tags() == kStringTag && !isDouble()) {
            if (isInline()) {
                // The inline string ends before the first 0 byte, else at the end of the inline data.
                auto str = (const char*)asInline().data;
                size_t len = 0;
                while (str[len] != 0 && len < NanTagged::kInlineCapacity)
                    ++len;
                return string_view(str, len);
            } else if (!isNull()) {
                return string_view(asPointer());
            }
        }
        return string_view();
    }


    Value::Array* Value::asArray() const {
        if (tags() == kArrayTag)
            return (Array*)asPointer();
        return nullptr;
    }


    const Word* Value::asQuote() const {
        if (tags() == kQuoteTag)
            return (const Word*)asPointer();
        return nullptr;
    }


    bool Value::operator== (const Value &v) const {
        if (NanTagged::operator==(v))
            return true;
        Type myType = type();
        if (myType == ANull || myType == ANumber || v.type() != myType)
            return false;
        else if (myType == AString)
            return asString() == v.asString();
        else if (myType == AnArray)
            return *asArray() == *v.asArray();
        else
            return false;
    }


    template <typename T>
    static inline int _cmp(T a, T b)    {return (a==b) ? 0 : ((a<b) ? -1 : 1);}


    int Value::cmp(Value v) const {
        Type myType = type(), vType = v.type();
        if (myType != vType)
            return int(myType) - int(vType);
        switch (myType) {
            case ANull:
                return 0;
            case ANumber:
                return _cmp(asDouble(), v.asDouble());
            case AString:
                return asString().compare(v.asString());
            case AnArray: {
                const Array *a = asArray(), *b = v.asArray();
                auto ia = a->begin(), ib = b->begin();
                for (size_t n = min(a->size(), b->size()); n > 0; --n, ++ia, ++ib) {
                    if (int c = ia->cmp(*ib); c != 0)
                        return c;
                }
                return _cmp(a->size(), b->size());
            }
            case AQuote:
                return _cmp(asQuote(), v.asQuote());    // arbitrary ordering by address
        }
    }


    Value Value::length() const {
        if (isString())
            return asString().size();
        else if (isArray())
            return asArray()->size();
        else
            return NullValue;
    }


    Value Value::operator+ (Value v) const {
        if (isDouble() || v.isDouble()) {
            // Addition:
            return Value(asDouble() + v.asDouble());
        } else if (isString() && v.isString()) {
            // String concatenation:
            auto str1 = asString(), str2 = v.asString();
            if (str1.size() == 0)
                return v;
            else if (str2.size() == 0)
                return *this;
            else {
                Value result;
                char *dst = result.allocString(str1.size() + str2.size());
                memcpy(dst,               str1.data(), str1.size());
                memcpy(dst + str1.size(), str2.data(), str2.size());
                return result;
            }
        } else if (isArray()) {
            // Add item to array:
            auto newArray = new Array(*asArray());
            newArray->push_back(v);
            return Value((char*)newArray);
        } else {
            return NullValue;
        }
    }


    // Numeric-only operations don't need type checking. If either value is non-numeric, then
    // `asDouble` returns a NaN by definition, and the Value constructor changes that to `null`.

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
        return NullValue;
    }


    static std::ostream& operator<< (std::ostream &out, const Value::Array &array) {
        out << '{';
        int n = 0;
        for (auto value : array) {
            if (n++ > 0)
                out << ", ";
            out << value;
        }
        out << '}';
        return out;
    }


    std::ostream& operator<< (std::ostream &out, Value value) {
        switch (value.type()) {
            case Value::ANull:   return out << "null";
            case Value::ANumber: return out << value.asDouble();
            case Value::AString: return out << std::quoted(value.asString());
            case Value::AnArray: return out << *value.asArray();
            case Value::AQuote:  return out << "[QUOTE]";
        }
        return out;
    }

#endif

}
