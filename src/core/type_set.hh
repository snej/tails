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
    class StackEffect;

    /// A set of Value types. Describes one item, an input or output, in a word's stack effect.
    /// If it's a StackEffect output, it can optionally declare that it matches type of an input.
    class ROMTypeSet {
    public:
        constexpr ROMTypeSet() { }

        constexpr ROMTypeSet(Value::Type type)        {addType(type);}

        constexpr ROMTypeSet(std::initializer_list<Value::Type> types) {
            for (auto type : types)
                addType(type);
        }

        constexpr static ROMTypeSet anyType()                  {return ROMTypeSet(kTypeFlags);}
        constexpr static ROMTypeSet noType()                   {return ROMTypeSet();}

        constexpr bool exists() const                       {return _flags != 0;}
        constexpr bool canBeAnyType() const                 {return typeFlags() == kTypeFlags;}
        constexpr bool canBeType(Value::Type type) const  {return (_flags & (1 << int(type))) != 0;}

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
        void addAllTypes()                        {_flags = kTypeFlags;}

        void setTypes(ROMTypeSet t) {
            _flags = t.typeFlags() | (_flags & kInputMatchFlags);
        }

        /// True if the type matches the type of an input in a StackEffect.
        bool isInputMatch() const               {return (_flags & kInputMatchFlags) != 0;}
        /// The index of the StackEffect input that this matches.
        int inputMatch() const                    {return (_flags >> kNumTypes) - 1;}

        /// Declares that this TypeSet matches an input's type.
        constexpr void setInputMatch(ROMTypeSet inputType, unsigned inputNo) {
            assert(inputNo <= 6);
            _flags = ((inputNo+1) << kNumTypes) | (inputType._flags & kTypeFlags);
        }

        /// Adds an input index without changing my declared type.
        /// Used when declaring built-in words.
        constexpr ROMTypeSet operator/ (unsigned inputNo) const {
            assert(inputNo <= 6);
            return ROMTypeSet(typeFlags() | ((inputNo+1) << kNumTypes));
        }

        /// I am "greater than" another entry if I support types it doesn't.
        int compare(const ROMTypeSet &other) const {
            if (typeFlags() == other.typeFlags())
                return 0;
            else if ((typeFlags() & ~other.typeFlags()) != 0)
                return 1;
            else
                return -1;
        }

        bool operator== (const ROMTypeSet &other) const   {return compare(other) == 0;}
        bool operator!= (const ROMTypeSet &other) const   {return compare(other) != 0;}
        bool operator>  (const ROMTypeSet &other) const   {return compare(other) > 0;}
        bool operator<  (const ROMTypeSet &other) const   {return compare(other) < 0;}

        constexpr explicit operator bool() const                 {return exists();}

        constexpr ROMTypeSet operator| (ROMTypeSet s) const {return (_flags | s._flags) & kTypeFlags;}
        ROMTypeSet operator& (ROMTypeSet s) const {return (_flags & s._flags) & kTypeFlags;}
        ROMTypeSet operator- (ROMTypeSet s) const {return (_flags & ~s._flags) & kTypeFlags;}

        ROMTypeSet& operator|= (ROMTypeSet s) &              {*this = *this | s; return *this;}

        constexpr uint8_t typeFlags() const                      {return _flags & kTypeFlags;}
        constexpr uint8_t flags() const                          {return _flags;} // tests only

        std::string description() const {
            if (canBeAnyType())
                return "any type";
            else if (!exists())
                return "no type";
            else {
                std::string s;
                for (int i = 0; i < kNumTypes; ++i) {
                    if (_flags & (1<<i)) {
                        if (!s.empty()) s += "|";
                        s += Value::typeName(Value::Type(i));
                    }
                }
                return s;
            }
        }

    private:
        constexpr ROMTypeSet(int flags) :_flags(uint8_t(flags)) { }

        static constexpr int kNumTypes = 5;
        static constexpr uint8_t kTypeFlags = (1 << kNumTypes) - 1;
        static constexpr uint8_t kInputMatchFlags = ~kTypeFlags;

        uint8_t _flags = 0;
    };


    class TypeSet : public ROMTypeSet {
    public:
        TypeSet() = default;
        TypeSet(ROMTypeSet ts)  :ROMTypeSet(ts) { }

        TypeSet& withQuoteEffect(StackEffect const& fx);
        std::optional<StackEffect> quoteEffect() const;

    private:
        std::shared_ptr<StackEffect> _quoteEffect;
    };

}
