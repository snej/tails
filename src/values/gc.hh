//
// gc.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "value.hh"
#include <stdint.h>
#include <stdlib.h>
#include <string_view>

namespace tails::gc {

    /// Abstract base class of garbage collected objects (referenced by Values)
    class object {
    public:
        static object* first()  {return sFirst;}
        object* next() const    {return (object*)(_next & ~kMarkedBit);}
        static size_t instanceCount()   {return sInstanceCount;}

        void mark()             {_next |= kMarkedBit;}
        void unmark()           {_next &= ~kMarkedBit;}
        bool isMarked() const   {return (_next & kMarkedBit) != 0;}

        static void scanStack(const Value *bottom, const Value *top);
        static void scanWord(const Word*);

        static std::pair<size_t,size_t> sweep();    // returns {kept, freed} count

    protected:

        static void* operator new(size_t baseSize, size_t extra) {
            return ::operator new(baseSize + extra);
        }

        object();

    private:
        static object* sFirst;
        static size_t sInstanceCount;

        enum {
            kMarkedBit = 0x1
        };

        intptr_t _next;
    };


    class String : public object {
    public:
        static String* make(size_t len);
        static String* make(std::string_view str);

        const char* c_str() const               {return _data;}
        std::string_view string_view() const    {return std::string_view(_data, _len);}

    private:
        String(size_t len);
        String(std::string_view str);

        uint32_t _len;
        char     _data[1];
    };


    class Array : public object {
        static Array* make();

    };

}
