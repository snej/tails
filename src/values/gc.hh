//
// gc.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "value.hh"
#include <memory>
#include <stdint.h>
#include <stdlib.h>
#include <string_view>

#ifndef SIMPLE_VALUE

namespace tails {
    class Word;
    class CompiledWord;
}

namespace tails::gc {

    /// Abstract base class of garbage collected objects (referenced by Values.)
    /// This class hierarchy doesn't use C++ virtual methods; instead the subclass is indicated by
    /// two bits in the `_next` pointer. (The only time the subclass needs to be determined this
    /// way is when freeing an object; otherwise the Value that points to the object already knows
    /// the type.)
    class object {
    public:
        /// Marks all objects found in the stack from `bottom` to `top` (inclusive.)
        static void scanStack(const Value *bottom, const Value *top);
        /// Marks all object literals found in a word.
        static void scanWord(const Word*);

        /// Frees all objects that have not been marked.
        /// Returns the number still alive, and the number freed.
        static std::pair<size_t,size_t> sweep();

        static object* first()          {return sFirst;}
        object* next() const            {return (object*)(_next & ~kTagBits);}
        static size_t instanceCount()   {return sInstanceCount;}

        int type() const                {return _next & kTypeBits;}

    protected:
        object(int type);
        bool mark()                     {bool chg = !isMarked(); _next |= kMarkedBit; return chg;}
        void unmark()                   {_next &= ~kMarkedBit;}
        bool isMarked() const           {return (_next & kMarkedBit) != 0;}
        void collect();
        void setNext(object *o)         {_next = (intptr_t(o) & ~kTagBits) | (_next & kTagBits);}

        enum {
            kTypeBits  = 0x3,           // Bits 0,1 indicate the object's subclass
                kStringType = 0x1,
                kArrayType  = 0x2,
                kQuoteType  = 0x3,
            kMarkedBit = 0x4,           // Bit 2 is set when object is marked as live during GC
            kTagBits   = kTypeBits | kMarkedBit
        };

    private:
        static object* sFirst;          // Start of linked list of all allocated objects
        static size_t  sInstanceCount;

        intptr_t _next;                 // Pointer to next object, plus 3 tag bits
    };


    /// A heap-allocated garbage-collected string.
    class String : public object {
    public:
        static String* make(size_t len)         {return new (len) String(len);}
        static String* make(std::string_view s) {return new (s.size()) String(s);}
        const char* c_str() const               {return _data;}
        std::string_view string_view() const    {return std::string_view(_data, _len);}
        /// Marks this string as in use.
        void mark()                             {object::mark();}

        static void operator delete(void *ptr)  {::operator delete(ptr);}
    private:
        static void* operator new(size_t baseSize, size_t extra) {
            return ::operator new(baseSize + extra);
        }

        String(size_t len);
        String(std::string_view str);

        uint32_t _len;
        char     _data[1]; // actual length is variable
    };


    /// A heap-allocated garbage-collected array.
    class Array : public object {
    public:
        Array()                                 :object(kArrayType) { }
        Array(std::vector<Value>&& a)           :object(kArrayType), _array(std::move(a)) { }
        std::vector<Value>& array()             {return _array;}
        /// Marks this array, and all objects in it, as in use.
        void mark();
    private:
        std::vector<Value> _array;
    };


    /// A heap-allocated garbage-collected anonymous Word.
    class Quote : public object {
    public:
        Quote(CompiledWord*);
        ~Quote();
        const CompiledWord* word() const        {return _word.get();}
        /// Marks this quote, and any objects it references as literals, as in use.
        void mark();
    private:
        std::unique_ptr<CompiledWord> _word;
    };

}

#endif // SIMPLE_VALUE
