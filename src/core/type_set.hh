//
// type_set.hh
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
#include "value.hh"
#include "utils.hh"
#include <algorithm>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <stdint.h>

namespace tails {

    /// A set of Value types. Describes one item, an input or output, in a word's stack effect.
    /// If it's a StackEffect output, it can optionally declare that it matches the type of an input.
    class TypeSet {
    public:
        constexpr TypeSet() { }

        constexpr TypeSet(Value::Type type)        {addType(type);}

        constexpr TypeSet(std::initializer_list<Value::Type> types) {
            for (auto type : types)
                addType(type);
        }

        constexpr static TypeSet anyType()                  {return TypeSet(kTypeFlags);}
        constexpr static TypeSet noType()                   {return TypeSet();}

        constexpr bool exists() const                       {return _flags != 0;}
        constexpr bool canBeAnyType() const                 {return typeFlags() == kTypeFlags;}
        constexpr bool canBeType(Value::Type type) const    {return (_flags & (1 << int(type))) != 0;}

        constexpr bool multiType() const {
            uint8_t t = typeFlags();
            return t != 0 && (t & (t - 1)) !=0;
        }

        std::optional<Value::Type> firstType() const {
            for (int i = 0; i < kNumTypes; ++i)
                if (_flags & (1<<i))
                    return Value::Type(i);
            return std::nullopt;
        }

        constexpr void addType(Value::Type type)            {_flags |= (1 << int(type));}
        constexpr void addAllTypes()                        {_flags = kTypeFlags;}

        constexpr void setTypes(TypeSet t)                  {_flags = t.typeFlags() | (_flags & kInputMatchFlags);
        }

        /// True if the type matches the type of an input in a StackEffect.
        constexpr bool isInputMatch() const                 {return (_flags & kInputMatchFlags) != 0;}
        /// The index of the StackEffect input that this matches.
        constexpr int inputMatch() const                    {return (_flags >> kNumTypes) - 1;}

        /// Declares that this TypeSet matches an input's type.
        constexpr void setInputMatch(TypeSet inputEntry, unsigned inputNo) {
            assert(inputNo <= 6);
            _flags = ((inputNo+1) << kNumTypes) | (inputEntry._flags & kTypeFlags);
        }

        /// Adds an input index without changing my declared type.
        /// Used when declaring built-in words.
        constexpr TypeSet operator/ (unsigned inputNo) const {
            assert(inputNo <= 6);
            return TypeSet(typeFlags() | ((inputNo+1) << kNumTypes));
        }

        /// I am "greater than" another entry if I support types it doesn't.
        constexpr int compare(const TypeSet &other) const {
            if (typeFlags() == other.typeFlags())
                return 0;
            else if ((typeFlags() & ~other.typeFlags()) != 0)
                return 1;
            else
                return -1;
        }

        constexpr bool operator== (const TypeSet &other) const   {return compare(other) == 0;}
        constexpr bool operator!= (const TypeSet &other) const   {return compare(other) != 0;}
        constexpr bool operator>  (const TypeSet &other) const   {return compare(other) > 0;}
        constexpr bool operator<  (const TypeSet &other) const   {return compare(other) < 0;}

        constexpr explicit operator bool() const                 {return exists();}

        constexpr TypeSet operator| (TypeSet s) const {return (_flags | s._flags) & kTypeFlags;}
        constexpr TypeSet operator& (TypeSet s) const {return (_flags & s._flags) & kTypeFlags;}
        constexpr TypeSet operator- (TypeSet s) const {return (_flags & ~s._flags) & kTypeFlags;}

        constexpr uint8_t typeFlags() const                      {return _flags & kTypeFlags;}
        constexpr uint8_t flags() const                          {return _flags;} // tests only

    private:
        constexpr TypeSet(int flags) :_flags(uint8_t(flags)) { }

        static constexpr int kNumTypes = 5;
        static constexpr uint8_t kTypeFlags = (1 << kNumTypes) - 1;
        static constexpr uint8_t kInputMatchFlags = ~kTypeFlags;

        uint8_t _flags = 0;
    };



    /// A reference to a list of TypeSets. (Basically like a C++20 range.)
    /// The items in memory are in stack bottom-to-top order,
    /// but the API pretends they are top-to-bottom: `[0]` is the top, `[1]` is below it, etc.
    class TypesView {
    public:
        constexpr TypesView()                           :_bottom(nullptr), _top(nullptr) { }

        constexpr TypesView(TypeSet *bottom, TypeSet *top)
        :_bottom(bottom), _top(top + 1)
        { assert(bottom && top && _bottom <= _top); }

        constexpr TypesView(TypeSet *bottom, size_t size)
        :TypesView(bottom, bottom + size - 1)
        { }

        constexpr int size() const                      {return int(_top - _bottom);}

        // Indexing is from the top of the stack
        constexpr TypeSet operator[] (size_t i) const   {assert (i < size()); return *(_top-1-i);}
        constexpr TypeSet& operator[] (size_t i)        {assert (i < size()); return *(_top-1-i);}

        // rbegin/rend start at the bottom of the stack
        // (begin() / end() would take more work to implement since ++ needs to decrement the ptr)
        constexpr const TypeSet* rbegin() const         {return _bottom;}
        constexpr TypeSet* rbegin()                     {return _bottom;}
        constexpr const TypeSet* rend() const           {return _top;}
        constexpr TypeSet* rend()                       {return _top;}

        constexpr bool operator== (const TypesView &other) const {
            auto sz = size();
            if (sz != other.size())
                return false;
            for (size_t i = 0; i < sz; ++i)
                if (_bottom[i] != other._bottom[i])
                    return false;
            return true;
        }

        constexpr bool operator!= (const TypesView &other) const {
            return !(*this == other);
        }

    private:
        TypeSet* const _bottom; // bottom of stack
        TypeSet* const _top;    // 1 past top of stack
    };

}
