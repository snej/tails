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
#include "value.hh"
#include "word.hh"
#include "core_words.hh"

namespace tails::gc {
    using namespace tails;
    using namespace std;


    object* object::sFirst = nullptr;
    size_t object::sInstanceCount = 0;


    object::object()
    :_next(intptr_t(sFirst))
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
        object *next, **prev = &sFirst;
        bool lastSwept = false;
        for (object *o = first(); o; o = next) {
            next = o->next();
            if (o->isMarked()) {
                o->unmark();
                if (lastSwept) {
                    *prev = o;
                    lastSwept = false;
                }
                prev = (object**)&o->_next;
                ++kept;
            } else {
                delete o;
                lastSwept = true;
                ++freed;
            }
        }
        if (lastSwept)
            *prev = nullptr;
        assert(kept + freed == sInstanceCount);
        sInstanceCount -= freed;
        return {kept, freed};
    }


    String* String::make(size_t len) {
        return new (len) String(len);
    }


    String* String::make(std::string_view str) {
        return new (str.size()) String(str);
    }


    String::String(size_t len)
    :object()
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

}
