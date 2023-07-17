//
// compiler+stackcheck.cc
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

#include "compiler.hh"
#include "core_words.hh"
#include "effect_stack.hh"
#include "core_words.hh"
#include "utils.hh"
#include <optional>

namespace tails {
    using namespace std;
    using namespace tails::core_words;

    using InstructionPos = Compiler::InstructionPos;

    struct CheckContext {
        StackEffect& effect;
        bool const effectCanAddInputs, effectCanAddOutputTypes;
        bool& effectCanAddOutputs;
        Word::Flags const flags;
    };


    using CheckOp = void (*)(InstructionPos i,
                             EffectStack &curStack,
                             CheckContext&);

    extern CheckOp OpcodeChecks[256];


    // Computes the stack effect of the word, throwing if it's inconsistent.
    void Compiler::computeEffect() {
        computeEffect(_words.begin(), EffectStack(_effect));
    }


    // Subroutine that traces control flow, memoizing stack effects at each instruction.
    // @param i  The item in `_words` to start at
    // @param curStack  The known stack before the word at `i`
    // @param finalEffect  The cumulative stack effect will be stored here.
    // @throw compile_error if stack is inconsistent or there's an invalid branch offset.
    void Compiler::computeEffect(InstructionPos i, EffectStack curStack)
    {
        CheckContext checkCtx {
            .effect                 = _effect,
            .effectCanAddInputs     = _effectCanAddInputs,
            .effectCanAddOutputs    = _effectCanAddOutputs,
            .effectCanAddOutputTypes= _effectCanAddOutputTypes,
            .flags                  = _flags,
        };

        Opcode opcode;
        do {
            assert(i != _words.end());
            try {
                // Handle merging flows of control at branch destinations:
                if (i->isBranchDestination) {
                    if (i->knownStack) {
                        if (*i->knownStack == curStack) {
                            // Nothing more to do: already handled this control flow + types
                            break;
                        } else {
                            // Merge current stack and the one from the other control path.
                            curStack.mergeWith(*i->knownStack);
                            *i->knownStack = curStack;
                        }
                    } else {
                        // Memoize the current stack so when we parse the other flow of control we
                        // can compare with this flow:
                        i->knownStack = make_unique<EffectStack>(curStack);
                    }
                }
                
                //cerr << "ComputeEffect: " << curStack << "  ... now " << i->word->name() << endl;
                
                // Apply the instruction's compile-time behavior to the type-stack:
                opcode = i->word->instruction().opcode;
                OpcodeChecks[uint8_t(opcode)](i, curStack, checkCtx);

            } catch(compile_error const& err) {
                throw err.withLocation(i->sourceCode);
            }
            
            // Handle control flow:
            if (opcode == Opcode::_BRANCH || opcode == Opcode::_ZBRANCH) {
                assert(i->branchTo);
                // If this is a 0BRANCH, recurse to follow the non-branch case too:
                if (i->word == &_ZBRANCH)
                    computeEffect(next(i), curStack);
                
                // Follow the branch:
                i = *i->branchTo;
            } else {
                // Continue to next instruction:
                ++i;
            }
        } while (opcode != Opcode::_RETURN);
    }


#pragma mark - OPCODE CHECK UTILITIES:


    // Default checking, but given a stack effect to use:
    static void defaultCheckWithEffect(InstructionPos i, EffectStack& curStack, CheckContext& ctx,
                                       StackEffect const& nextEffect)
    {
        assert(!nextEffect.isWeird());
        if (ctx.effectCanAddInputs) {
            // We are parsing code with unknown inputs, i.e. a quotation. If the word being
            // called takes more inputs than are on the stack, make them inputs of this code.
            const auto nInputs = nextEffect.inputCount();
            auto nAvailable = curStack.depth();
            for (auto i = int(nAvailable); i < nInputs; ++i) {
                auto entry = nextEffect.inputs()[i];
                curStack.addAtBottom(entry);
                ctx.effect.addInputAtBottom(entry);
            }
        }

        // apply the word's effect:
        curStack.add(*i->word, nextEffect);
    }


    // The default check, that works for many opcodes.
    static void defaultCheck(InstructionPos i, EffectStack& curStack, CheckContext& ctx) {
        // Determine the effect of a word:
        StackEffect nextEffect = i->word->stackEffect();
        if (nextEffect.isWeird())
            throw compile_error(format("Oops, don't know word '%s's stack effect",
                                       i->word->name()), i->sourceCode);
        defaultCheckWithEffect(i, curStack, ctx, nextEffect);
    }


#pragma mark - OPCODE CHECKS:


    static void chk__LITERAL(InstructionPos i, EffectStack& curStack, CheckContext& ctx) {
        curStack.push(i->param.param.literal);
    }


    static void chk__INT(InstructionPos i, EffectStack& curStack, CheckContext& ctx) {
        curStack.push(Value(i->param.param.offset));
    }


    static void chk__ROTn(InstructionPos i, EffectStack& curStack, CheckContext& ctx) {
        curStack.rotate(i->param.param.offset);
    }


    // This function handles both _GETARG and _SETARG:
    static void chk__GETARG(InstructionPos i, EffectStack& curStack, CheckContext& ctx) {
        if (i->param.param.offset <= 0) {
            // Get/set a function argument. Adjust the offset for the current stack:
            auto offset = i->param.param.offset;
            assert(offset <= 0);
            i->param.param.offset -= curStack.depth() - ctx.effect.inputCount();

            TypeSet paramType = ctx.effect.inputs()[-offset];
            if (i->word == &_GETARG)
                curStack.push(paramType);
            else
                curStack.add(*i->word, StackEffect({paramType}, {}));

        } else {
            // Get/set a local variable:
            auto offset = i->param.param.offset;
            assert(offset > 0);
            offset -= curStack.depth() - ctx.effect.inputCount();
            i->param.param.offset = offset;
            offset = -offset;

            if (i->word == &_GETARG) {
                curStack.over(offset);
                if (!curStack.at(0).types())
                    throw compile_error("Reading local before it's assigned a value");
            } else {
                TypeSet localType = curStack[offset].types();
                TypeSet valueType = curStack[0].types();
                if (localType) {
                    if (TypeSet badTypes = valueType - localType) {
                        throw compile_error(format("Type mismatch assigning to local"));
                    }
                } else {
                    curStack.setTypeAt(offset, valueType);
                }
                curStack.pop();
            }
        }
    }

    auto chk__SETARG = &chk__GETARG;


    static void chk__LOCALS(InstructionPos i, EffectStack& curStack, CheckContext& ctx) {
        // Reserving space for local variables:
        for (auto n = i->param.param.offset; n > 0; --n)
            curStack.push(TypeSet());   // type starts out as 'none'; will be set on 1st assignment
    }


    static void chk__DROPARGS(InstructionPos i, EffectStack& curStack, CheckContext& ctx) {
        // Popping the parameters:
        auto nParams = i->param.param.drop.locals;
        auto nResults = i->param.param.drop.results;
        auto actualResults = curStack.depth() - nParams;
        if (actualResults != nResults)
            throw compile_error(format("Should return %d values, not %d",
                                       nResults, actualResults));
        curStack.erase(nResults, nResults + nParams);
    }


    static void chk_CALL(InstructionPos i, EffectStack& curStack, CheckContext& ctx) {
        TypeSet callee = curStack.pop().types();
        if (callee != Value::AQuote)
            throw compile_error(format("Can't call a value of type %s",
                                       callee.description().c_str()));
        auto quoteEffect = callee.quoteEffect();
        if (!quoteEffect)
            throw compile_error("This quote's parameters aren't known");
        curStack.add(*i->word, *quoteEffect);
    }


    static void chk__RETURN(InstructionPos i, EffectStack& curStack, CheckContext& ctx) {
        // The stack when RETURN is reached determines the word's output effect.
        curStack.checkOutputs(ctx.effect,
                              ctx.effectCanAddOutputs,  ctx.effectCanAddOutputTypes);
        ctx.effectCanAddOutputs = false;
        if (curStack.maxGrowth() > ctx.effect.max())
            ctx.effect = ctx.effect.withMax(int(curStack.maxGrowth()));
    }


    static void chk__RECURSE(InstructionPos i, EffectStack& curStack, CheckContext& ctx) {
        if (ctx.effectCanAddInputs || ctx.effectCanAddOutputs)
            throw compile_error("RECURSE requires an explicit stack effect declaration");
        StackEffect nextEffect = ctx.effect;
        if (!next(i)->returnsImmediately()) {
            if (ctx.flags & Word::Inline)
                throw compile_error("Illegal recursion in an inline word");
            nextEffect = nextEffect.withUnknownMax();   // non-tail recursion
        }
        defaultCheckWithEffect(i, curStack, ctx, nextEffect);
    }


    static void chk_IFELSE(InstructionPos i, EffectStack& curStack, CheckContext& ctx) {
        // The two top stack items must be literal quotation values (not just types):
        auto getQuoteEffect = [&](int stackPos) -> StackEffect {
            if (auto effect = curStack[stackPos].types().quoteEffect())
                return *effect;
            throw compile_error("IFELSE must be preceded by two quotations");
        };
        StackEffect a = getQuoteEffect(1), b = getQuoteEffect(0);

        // Check if the quotations' effects are compatible, and merge them:
        if (a.net() != b.net())
            throw compile_error("IFELSE quotes have inconsistent stack depths");

        StackEffect opEffect = a;

        for (int inp = 0; inp < b.inputCount(); inp++) {
            auto entry = b.inputs()[inp];
            if (inp < a.inputCount()) {
                entry = entry & opEffect.inputs()[inp];
                if (!entry)
                    throw compile_error(format("IFELSE quotes have incompatible parameter #%d",
                                               inp));
                opEffect.inputs()[inp] = entry;
            } else {
                opEffect.addInput(entry);
            }
        }

        for (int i = 0; i < b.outputCount(); i++) {
            auto entry = b.outputs()[i];
            if (i < a.outputCount()) {
                opEffect.outputs()[i] = opEffect.outputs()[i] | entry;
            } else {
                opEffect.addOutput(entry);
            }
        }

        // Add the inputs of IFELSE itself -- the test and quotes:
        opEffect.addInput(TypeSet::anyType());
        opEffect.addInput(TypeSet(Value::AQuote));
        opEffect.addInput(TypeSet(Value::AQuote));

        opEffect = opEffect.withMax( max(0, max(a.max(), b.max()) - 3) );
        defaultCheckWithEffect(i, curStack, ctx, opEffect);
    }


    // Opcodes without their own checks (above) use the default check:
    static CheckOp chk__INTERP = defaultCheck;
    static CheckOp chk__TAILINTERP = defaultCheck;
    static CheckOp chk__BRANCH = defaultCheck;
    static CheckOp chk__ZBRANCH = defaultCheck;
    static CheckOp chk_NOP = defaultCheck;
    static CheckOp chk_DROP = defaultCheck;
    static CheckOp chk_DUP = defaultCheck;
    static CheckOp chk_OVER = defaultCheck;
    static CheckOp chk_ROT = defaultCheck;
    static CheckOp chk_SWAP = defaultCheck;
    static CheckOp chk_ZERO = defaultCheck;
    static CheckOp chk_ONE = defaultCheck;
    static CheckOp chk_EQ = defaultCheck;
    static CheckOp chk_NE = defaultCheck;
    static CheckOp chk_EQ_ZERO = defaultCheck;
    static CheckOp chk_NE_ZERO = defaultCheck;
    static CheckOp chk_GE = defaultCheck;
    static CheckOp chk_GT = defaultCheck;
    static CheckOp chk_GT_ZERO = defaultCheck;
    static CheckOp chk_LE = defaultCheck;
    static CheckOp chk_LT = defaultCheck;
    static CheckOp chk_LT_ZERO = defaultCheck;
    static CheckOp chk_ABS = defaultCheck;
    static CheckOp chk_MAX = defaultCheck;
    static CheckOp chk_MIN = defaultCheck;
    static CheckOp chk_DIV = defaultCheck;
    static CheckOp chk_MOD = defaultCheck;
    static CheckOp chk_MINUS = defaultCheck;
    static CheckOp chk_MULT = defaultCheck;
    static CheckOp chk_PLUS = defaultCheck;
    static CheckOp chk_NULL_ = defaultCheck;
    static CheckOp chk_LENGTH = defaultCheck;
    static CheckOp chk_DEFINE = defaultCheck;
    static CheckOp chk_PRINT = defaultCheck;
    static CheckOp chk_SP = defaultCheck;
    static CheckOp chk_NL = defaultCheck;
    static CheckOp chk_NLQ = defaultCheck;


    CheckOp OpcodeChecks[256] = {
#define DEFINE_OP(O) chk_##O,
#include "opcodes.hh"
    };

}
