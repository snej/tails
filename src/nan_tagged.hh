//
// nan_tagged.hh
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

// Inspiration and details from:
// https://www.npopov.com/2012/02/02/Pointer-magic-for-efficient-dynamic-value-representations.html

#pragma once
#include "platform.hh"
#include <assert.h>
#include <initializer_list>
#include <limits>
#include <math.h>
#include <string.h>  // for memcpy()

namespace tails {

    struct slice {
        const void *data;
        size_t size;
    };

    /** A self-describing 8-byte value that can store a double, a pointer, or six bytes of inline
        data; and can identify which it's holding at any time.
        (By virtue of storing doubles, it can also store exact integers up to ±2^51.)
        Uses the so-called "NaN tagging" or "Nan boxing" trick that's used by several dynamic
        language runtimes, such as LuaJIT and both WebKit's and Mozilla's JavaScript VMs.

        Theory of operation:
        - All non-NaN double values represent themselves.
        - "Quiet" NaNs with the leading bits 0x7ff8 or 0xfff8 are special:
          - If the sign bit is set, the lower 48 bits are a pointer, which will be extended to
            64 bits. (No current mainstream CPUs use more than 48 bits of address space.)
          - Otherwise the lower 48 bits are 6 bytes of inline data.
          - Two tag bits are available; you could use them to distinguish between four types of
            pointers or inline data, for instance. */
    template <class TO>
    class NanTagged {
    public:
        /** How many bytes of inline data I can hold. */
        static constexpr size_t kInlineCapacity = 6;

        constexpr NanTagged() noexcept                              :_bits(kPointerType) { }
        constexpr NanTagged(double d) noexcept                      {setDouble(d);}
        constexpr NanTagged(const TO *ptr) noexcept                 {setPointer(ptr);}
        constexpr NanTagged(std::initializer_list<uint8_t> b) noexcept {setInline(b);}

        bool operator== (NanTagged n) const noexcept _pure          {return _bits == n._bits;}
        bool operator!= (NanTagged n) const noexcept _pure          {return _bits != n._bits;}

        // Type testing:

        bool isDouble() const noexcept _pure        {return (_bits & kMagicBits) != kMagicBits;}
        bool isPointer() const noexcept _pure       {return (_bits & kTypeMask) == kPointerType;}
        bool isInline() const noexcept _pure        {return (_bits & kTypeMask) == kInlineType;}

        // Getters:

        /// Returns the `double` this stores, or an `NaN` if it's not holding a double.
        double asDouble() const noexcept _pure      {return _asDouble;}
        /// Returns the `double` this stores, or 0.0 if it's not holding a double.
        double asDoubleOrZero() const noexcept _pure{return isDouble() ? _asDouble : 0.0;}
        /// Returns the pointer this stores, or nullptr if it's not holding a pointer.
        const TO* asPointer() const noexcept _pure  {return isPointer() ? pointerValue() : nullptr;}
        /// Returns the inline data this stores, or `{nullptr,0}` if it's not inline.
        slice asInline() const noexcept _pure       {return isInline() ? inlineValue() : slice();}


        // Pointer & inline values have two free tag bits:
        bool tag1() const noexcept _pure    {return (_bits & kTagBit1) != 0; }
        bool tag2() const noexcept _pure    {return (_bits & kTagBit2) != 0; }
        int tags() const noexcept _pure     {return int((_bits >> 48) & 0x03);}

        // Setters:

        void setDouble(double d) noexcept {
            // We can't accept a double that's a NaN, because it could match our magic bit pattern
            // and be mistaken for a pointer or inline. Therefore we turn any NaN value into null.
            if (isnan(d))
                setPointer(nullptr);
            else
                _asDouble = d;
        }

        constexpr void setPointer(const TO *p) noexcept {
            _bits = (uint64_t)p | kPointerType;
        }

        /// Makes this an inline value, containing all zeroes, and returns a pointer to the
        /// inline storage so you can write to it.
        void* setInline() noexcept          {_bits = kMagicBits; return &_bytes[kInlineOffset];}

        /// Makes this an inline value, copying the input bytes.
        /// \note The input length is not preserved; \ref asInline will return all 6 bytes.
        void setInline(slice bytes) noexcept {
            assert(bytes.size <= kInlineCapacity);
            setInline();
            ::memcpy(&_bytes[0], bytes.data, bytes.size);
        }

        void setInline(std::initializer_list<uint8_t> inlineBytes) noexcept {
            setInline({inlineBytes.begin(), inlineBytes.size()});
        }

        void setTag1(bool b) noexcept       {if (b) _bits |= kTagBit1; else _bits &= ~kTagBit1;}
        void setTag2(bool b) noexcept       {if (b) _bits |= kTagBit2; else _bits &= ~kTagBit2;}

        void setTags(int t) noexcept {
            _bits = (_bits & ~(kTagBit1 | kTagBit2)) | (uint64_t(t & 0x03) << 48);
        }

    protected:
        NanTagged(void**) { }   // no-op initializer for subclass constructors to call
        const TO* pointerValue() const noexcept _pure  {return (TO*)(_bits & kPtrBits);}
        slice inlineValue() const noexcept _pure {return {&_bytes[kInlineOffset], kInlineCapacity};}

    private:
        static constexpr uint64_t kSignBit  = 0x8000000000000000; // Sign bit of a double
        static constexpr uint64_t kQNaNBits = 0x7ff8000000000000; // Bits set in a 'quiet' NaN
        static constexpr uint64_t kMagicBits= 0x7ffc000000000000; // Bits that indicate non-double
        static constexpr uint64_t kTagBit1  = 0x0001000000000000; // Two pointer tag bits
        static constexpr uint64_t kTagBit2  = 0x0002000000000000; // Two pointer tag bits
        static constexpr uint64_t kPtrBits  = 0x0000FFFFFFFFFFFF; // Bits available to pointers

        static constexpr uint64_t kTypeMask    = kMagicBits | kSignBit; // Bits involved in tagging
        static constexpr uint64_t kPointerType = kMagicBits | kSignBit; // Tag bits in a pointer
        static constexpr uint64_t kInlineType  = kMagicBits;            // Tag bits in an inline


        // In little-endian the 51 free bits are at the start, in big-endian at the end.
#ifdef __BIG_ENDIAN__
        static constexpr auto kInlineOffset = 2;
#else
        static constexpr auto kInlineOffset = 0;
#endif

        union {
            double              _asDouble;
            uint64_t            _bits;
            uint8_t             _bytes[sizeof(double)];
        };
    };


    // Sanity checking:
    static_assert(sizeof(double) == 8);
    static_assert(sizeof(void*) <= 8);
}
