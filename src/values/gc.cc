//
// gc.cc
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
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

#include "gc.hh"
#include "compiler.hh"  // just for CompiledWord
#include "value.hh"
#include "word.hh"
#include "core_words.hh"

#ifndef SIMPLE_VALUE

namespace tails::gc {
    using namespace std;
    using namespace tails;


    object* object::sFirst = nullptr;
    size_t object::sInstanceCount = 0;


    object::object(int type)
    :_next(intptr_t(sFirst) | (type & kTypeBits))
    {
        assert(next() == sFirst);
        sFirst = this;
        ++sInstanceCount;
    }

    void object::scanStack(const Value *bottom, const Value *top) {
        if (bottom && top) {
            for (auto val = bottom; val <= top; ++val)
                val->mark();
        }
    }


    void object::scanWord(const Word *word) {
        if (!word->isNative()) {
            for (const Instruction *pc = word->instruction().word; *pc != core_words::_RETURN; ++pc) {
                if (*pc == core_words::_LITERAL)
                    (++pc)->literal.mark();
            }
        }
    }


    pair<size_t,size_t> object::sweep() {
        size_t freed = 0, kept = 0;
        object *next, *prev = nullptr, *o;
        bool fixLink = false;

        auto updateLink = [&] {
            if (fixLink) {
                if (prev) prev->setNext(o); else sFirst = o;
                fixLink = false;
            }
        };

        for (o = first(); o; o = next) {
            next = o->next();
            if (o->isMarked()) {
                o->unmark();
                updateLink();
                prev = o;
                ++kept;
            } else {
                // Free unmarked objects:
                o->collect();
                fixLink = true;
                ++freed;
            }
        }
        updateLink();
        assert(kept + freed == sInstanceCount);
        sInstanceCount -= freed;
        return {kept, freed};
    }


    void object::collect() {
        switch (type()) {
            case kStringType:  delete (String*)this; break;
            case kArrayType:   delete (Array*)this; break;
            case kQuoteType:   delete (Quote*)this; break;
            default:           break;
        }
    }


#pragma mark - STRING:


    String::String(size_t len)
    :object(kStringType)
    ,_len(uint32_t(len))
    {
        assert(len < UINT32_MAX);
        _data[len] = 0;
    }


    String::String(std::string_view str)
    :String(str.size())
    {
        memcpy(_data, str.data(), str.size());
    }


#pragma mark - ARRAY:


    void Array::mark() {
        if (object::mark()) {
            for (auto val : _array)
                val.mark();
        }
    }


#pragma mark - QUOTE:


    Quote::Quote(CompiledWord *word)
    :object(kQuoteType)
    ,_word(word)
    { }

    Quote::~Quote() = default;

    void Quote::mark() {
        if (object::mark())
            scanWord(_word.get());
    }

}

#endif // SIMPLE_VALUE
