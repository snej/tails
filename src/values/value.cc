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
#include "gc.hh"
#include "compiler.hh"  // just for CompiledWord
#include "io.hh"
#include <iomanip>
#include <iostream>
#include <string.h>

namespace tails {
    using namespace std;

    /**
     ### Value data representation:

     `Value` is a subclass of `NanTagged` (see nan_tagged.hh), which magically allows numbers,
     pointers and inline data to be stored in 64 bits. It exposes a `double`, a pointer,
     two tag bits, and an "inline" flag.

     - A number is represented as a regular `double` value. (This includes exact storage of integers
       up to ±2^51.)
     - A string has `kStringTag`. If up to 6 bytes long it can be stored inline; the length is
       determined by the number of trailing zero bytes. Otherwise it points to a gc::String object.
     - An array has `kArrayTag` and points to a `gc::Array` object. (It's never inline.)
     - A quotation has `kQuoteTag` and points to a `gc::Quote` object. (It's never inline.)
     - Null is a singleton value that's tagged as a String but has a null pointer.
    */


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
        vector<Value> array(arrayItems);
        setPointer(new gc::Array(std::move(array)));
        setTags(kArrayTag);
    }


    Value::Value(vector<Value> &&array)
    :NanTagged((void**)0)
    {
        setPointer(new gc::Array(std::move(array)));
        setTags(kArrayTag);
    }


    Value::Value(CompiledWord *word)
    :NanTagged(word)
    {
        assert(word);
        setPointer(new gc::Quote(word));
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
            auto heapStr = gc::String::make(len);
            setPointer(heapStr);
            return (char*)heapStr->c_str();
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
                return ((gc::String*)asPointer())->string_view();
            }
        }
        return string_view();
    }


    vector<Value>* Value::asArray() const {
        if (tags() == kArrayTag)
            return &((gc::Array*)asPointer())->array();
        return nullptr;
    }


    const Word* Value::asQuote() const {
        if (tags() == kQuoteTag)
            return ((gc::Quote*)asPointer())->word();
        return nullptr;
    }


    void Value::mark() const {
        switch (tags()) {
            case kStringTag:
                if (!isDouble() && !isInline())
                    ((gc::String*)asPointer())->mark();
                break;
            case kArrayTag:
                ((gc::Array*)asPointer())->mark();
                break;
            case kQuoteTag:
                ((gc::Quote*)asPointer())->mark();
            default:
                break;
        }
    }


    Value::operator bool() const {
        if (isDouble())
            return asDouble() != 0;
        else
            return !isNull();
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
                const vector<Value> *a = asArray(), *b = v.asArray();
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
        abort(); // unreachable, but GCC doesn't know that :p
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
            vector<Value> newArray(*asArray());
            Value newVal(std::move(newArray));
            newVal.asArray()->push_back(v);
            return newVal;
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


    static std::ostream& operator<< (std::ostream &out, const vector<Value> &array) {
        out << '[';
        int n = 0;
        for (auto value : array) {
            if (n++ > 0)
                out << ", ";
            out << value;
        }
        out << ']';
        return out;
    }


    std::ostream& operator<< (std::ostream &out, Value value) {
        switch (value.type()) {
            case Value::ANull:   return out << "null";
            case Value::ANumber: return out << value.asDouble();
            case Value::AString: return out << std::quoted(value.asString());
            case Value::AnArray: return out << *value.asArray();
            case Value::AQuote:  return out << "{(" << value.asQuote()->stackEffect() << ")}";
        }
        return out;
    }

}
