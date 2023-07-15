//
// word.hh
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
#include "instruction.hh"
#include "stack_effect.hh"


namespace tails {
    class Compiler;

    /// Abstract base class of a Forth word definition: name, flags and code.
    /// Subclass \ref ROMWord is used for predefined words that are constructed at compile time.
    /// Subclass \ref CompiledWord builds words at runtime.
    class Word {
    public:
        enum Flags : uint8_t {
            NoFlags     = 0x00,
            Native      = 0x01, ///< Implemented in native code (at `_instr.op`)
            HasIntParam = 0x02, ///< This word is followed by an integer param (BRANCH, 0BRANCH)
            HasValParam = 0x04, ///< This word is followed by a Value param (LITERAL)
            HasWordParam= 0x08, ///< This word is followed by a Word param (INTERP, TAILINTERP, etc)
            Magic       = 0x10, ///< Low-level, not allowed in parsed code (0BRANCH, INTERP, etc.)
            Inline      = 0x20, ///< Should be inlined at call site
            Recursive   = 0x40, ///< Calls itself recursively

            MagicIntParam  = Magic | HasIntParam,
            MagicValParam  = Magic | HasValParam,
            MagicWordParam = Magic | HasWordParam,
        };

        constexpr const char* name() const              {return _name;}
        constexpr Instruction const& instruction() const{return _instr;}
        constexpr ROMStackEffect const& stackEffect() const{return *_romEffect;}

        constexpr bool hasFlag(Flags f) const           {return (_flags & f) != 0;}
        constexpr bool isNative() const                 {return hasFlag(Native);}
        constexpr uint8_t parameters() const            {return _nParams;}
        constexpr bool hasIntParams() const             {return hasFlag(HasIntParam);}
        constexpr bool hasValParams() const             {return hasFlag(HasValParam);}
        constexpr bool hasWordParams() const            {return hasFlag(HasWordParam);}
        constexpr bool isMagic() const                  {return hasFlag(Magic);}

        constexpr size_t parametersSize() const {
            size_t s = 0;
            if (_nParams > 0) {
                if (hasIntParams())
                    s = sizeof(AfterInstruction::offset);
                else if (hasValParams())
                    s = sizeof(AfterInstruction::literal);
                else if (hasWordParams())
                    s = sizeof(AfterInstruction::word);
                s *= _nParams;
            }
            return s;
       }

    protected:
        Word() :_instr {}, _name(nullptr), _romEffect(nullptr), _flags(NoFlags) { };

        constexpr Word(const char *name,
                       Instruction instr,
                       Flags flags,
                       uint8_t nParams)
        :_instr(instr)
        ,_name(name)
        ,_romEffect(nullptr)
        ,_flags(flags)
        ,_nParams(nParams)
        { }

        Instruction _instr;         // Instruction that calls it (either an Op or an Instruction*)
        const char* _name;          // Forth name, or NULL if anonymous
        ROMStackEffect const* _romEffect;        // Number of function parameters / return values
        Flags       _flags;         // Flags (see above)
        uint8_t     _nParams = 0;   // Number of parameters following instr in code
    };


    /// A built-in native-code Word created at compile time.
    class ROMWord : public Word {
    public:
        constexpr ROMWord(const char *name,
                          Opcode native,
                          ROMStackEffect const& effect,
                          Flags flags =NoFlags,
                          uint8_t nParams =0)
        :Word(name, Instruction(native), Flags(flags | Native), nParams)
        {
            _effectStorage = effect;
            _romEffect = &_effectStorage;
            if (_nParams == 0 && (flags & (HasIntParam|HasValParam|HasWordParam)))
                _nParams = 1;
        }

    private:
        ROMStackEffect _effectStorage;
    };


    /// A subclass of Word that manages storage of its name and instructions, so it can be
    /// created at runtime.
    class CompiledWord : public Word {
    public:
        CompiledWord(std::string &&name, StackEffect effect, std::vector<Opcode> &&instrs);

        /// Constructs a word from a compiler. Call this instead of Compiler::finish.
        explicit CompiledWord(Compiler&&);

        /// Copies a CompiledWord, adding a name.
        CompiledWord(const CompiledWord&, std::string &&name);

        StackEffect const& stackEffect() const {return _effect;}

    private:
        std::string const           _nameStr;   // Backing store for inherited _name
        StackEffect                 _effect;    // Backing store for inherited _effect
        std::vector<Opcode> const   _instrs {}; // Backing store for inherited _instr
    };


    constexpr inline bool operator==(const Word &a, const Word &b)
                                        {return a.instruction().opcode == b.instruction().opcode;}
    constexpr inline bool operator!=(const Word &a, const Word &b)
                                        {return !(a == b);}

    /// The Word that implements each opcode; for use by disassemblers.
    extern const Word* const OpWords[256];


    // Instruction methods whose implementations depend on Word:

    Word const* Instruction::nativeWord() const {
        return OpWords[uint8_t(opcode)];
    }

    Instruction const* Instruction::next() const {
        return (Instruction const*)(uintptr_t(&param.nextOp) + nativeWord()->parametersSize());
    }

    Instruction Instruction::carefulCopy() const {
        Instruction dst;
        ::memcpy(&dst, this, sizeof(Opcode) + nativeWord()->parametersSize());
        return dst;
    }

}
