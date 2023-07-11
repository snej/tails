//
// tests.hh
//
// 
//

#pragma once

#include "core_words.hh"
#include "compiler.hh"
#include "disassembler.hh"
#include "gc.hh"
#include "more_words.hh"
#include "stack_effect_parser.hh"
#include "vocabulary.hh"
#include "io.hh"
#include <array>
#include <iomanip>
#include <iostream>

#include "catch.hpp"

using namespace std;
using namespace tails;


void initVocabulary();


#ifdef ENABLE_TRACING
// Exposed while running, for the TRACE function to use
inline Value * StackBase;
#endif


/// Top-level function to run a Word.
/// @return  The top value left on the stack.
Value run(const Word &word, std::initializer_list<Value> inputs = {});

void garbageCollect();

void printStackEffect(StackEffect f);

void printDisassembly(const Word *word);


