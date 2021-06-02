//
// repl.cc
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
#include "gc.hh"
#include "more_words.hh"
#include "vocabulary.hh"
#include "linenoise.h"
#include "utf8.h"
#include <algorithm>
#include <iostream>
#include <optional>
#include <mutex>
#include <sstream>
#include <unistd.h>

using namespace std;


#ifdef ENABLE_TRACING
namespace tails {
    void TRACE(Value *sp, const Instruction *pc) { }
}
#endif


namespace repl {
    using namespace tails;


    static optional<string> readLine(const char *prompt) {
        static once_flag sOnce;
        call_once(sOnce, [] {
            linenoiseSetEncodingFunctions(linenoiseUtf8PrevCharLen, linenoiseUtf8NextCharLen,
                                          linenoiseUtf8ReadCode);
#ifdef __APPLE__
            // Prevent linenoise from trying to use ANSI escapes in the Xcode console on macOS,
            // which is a TTY but does not set $TERM. For some reason linenoise thinks a missing $TERM
            // indicates an ANSI-compatible terminal (isUnsupportedTerm() in linenoise.c.)
            // So if $TERM is not set, set it to "dumb", which linenoise does understand.
            if (isatty(STDIN_FILENO) && getenv("TERM") == nullptr)
                setenv("TERM", "dumb", false);
#endif
        });

        char *cline = linenoise(prompt);
        if (!cline)
            return nullopt;
        string line = cline;
        linenoiseFree(cline);
        return line;
    }


    using Stack = std::vector<Value>;


#ifdef ENABLE_TRACING
    // Exposed while running, for the TRACE function to use
    static Value * StackBase;
#endif

    
    /// Top-level function to run a Word.
    /// @return  The top value left on the stack.
    static Stack run(const Word &word, Stack &stack) {
        assert(!word.isNative());           // must be interpreted
        if (word.stackEffect().inputs() > stack.size())
            throw compile_error("Stack would underflow", nullptr);
        auto depth = stack.size();
        stack.resize(depth + word.stackEffect().max());

        auto stackBase = &stack[0];
#ifdef ENABLE_TRACING
        StackBase = stackBase;
#endif
        auto stackTop = call(&stack[depth] - 1, word.instruction().word);
        stack.resize(stackTop - stackBase + 1);
        return stack;
    }


    static void eval(const string &source, Stack &stack) {
        Compiler comp;
        comp.setInputStack(&stack.front(), &stack.back());
        comp.parse(source);
        CompiledWord compiled(move(comp));
        run(compiled, stack);
    }


    static void garbageCollect(Stack &stack) {
        Compiler::activeVocabularies.gcScan();
        gc::object::scanStack(&stack.front(), &stack.back());
#if 1
        gc::object::sweep();
#else
        auto [preserved, freed] = gc::object::sweep();
        if (freed > 0)
            cout << "GC: freed " << freed << " objects; " << preserved << " left.\n";
#endif
    }


    static constexpr int kPromptIndent = 40;


    // Right-justified output
    static void print(const string &str) {
        size_t len = min(str.size(), size_t(kPromptIndent));
        size_t start = str.size() - len;
        cout << string(kPromptIndent - len, ' ') << str.substr(start, len);
    }


    // Print stack, right-justified
    static void print(const Stack &stack) {
        stringstream out;
        for (tails::Value v : stack)
            out << v << ' ';
        print(out.str());
    }
}

using namespace repl;


int main(int argc, const char **argv) {
    tails::Vocabulary defaultVocab(tails::word::kWords);
    tails::Compiler::activeVocabularies.push(defaultVocab);
    tails::Compiler::activeVocabularies.setCurrent(defaultVocab);

    cout << "Tails interpreter!!  Empty line clears stack.  Ctrl-D to exit.\n";
    Stack stack;
    while (true) {
        print(stack);
        cout.flush();
        optional<string> line = readLine(" ➤ ");
        if (!line)
            break;
        else if (line->empty()) {
            if (stack.empty()) {
                print("Cleared stack.");
                cout << '\n';
            }
            stack.clear();
        } else {
            try {
                eval(*line, stack);
                tails::word::endLine();
                garbageCollect(stack);
            } catch (const tails::compile_error &x) {
                if (x.location) {
                    auto pos = x.location - line->data();
                    assert(pos >= 0 && pos <= line->size());
                    cout << string(kPromptIndent + 3 + pos, ' ') << "⬆︎\n";
                }
                cout << string(kPromptIndent + 3, ' ') << "Error: " << x.what() << "\n";
            }
        }
    }
    return 0;
}
