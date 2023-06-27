//
//  CaseListReporter.hh
//
//  Created by Jens Alfke on 8/26/16.
//  Copyright (c) 2016 Couchbase. All rights reserved.
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

#include "catch.hpp"
#include <chrono>
#include <iostream>
#include <time.h>

#ifdef CASE_LIST_STOPWATCH
#include "Stopwatch.hh"
#endif

#ifdef CASE_LIST_BACKTRACE
#include "Backtrace.hh"
#endif


/** Custom reporter that logs a line for every test file and every test case being run.
    Use CLI args "-r list" to use it. */
struct CaseListReporter : public Catch::ConsoleReporter {
    CaseListReporter( Catch::ReporterConfig const& _config )
    :   Catch::ConsoleReporter( _config )
    {
        _start = time(nullptr);
        stream << "STARTING TESTS AT " << ctime(&_start);
        stream.flush();
    }

    virtual ~CaseListReporter() override {
        auto now = time(nullptr);
        stream << "ENDED TESTS IN " << (now - _start) << "sec, AT " << ctime(&now);
        stream.flush();
    }

    static std::string getDescription() {
        return "Logs a line for every test case";
    }

    virtual void testCaseStarting( Catch::TestCaseInfo const& _testInfo ) override {
        std::string file = _testInfo.lineInfo.file;
        if (file != _curFile) {
            _curFile = file;
            auto slash = file.rfind('/');
            stream << "## " << file.substr(slash+1) << ":\n";
        }
        stream << "    >>> " << _testInfo.name << "\n";
        _firstSection = true;
        _sectionNesting = 0;
        stream.flush();
        ConsoleReporter::testCaseStarting(_testInfo);
#ifdef CASE_LIST_STOPWATCH
        _stopwatch.reset();
#endif
    }
#ifdef CASE_LIST_STOPWATCH
    virtual void testCaseEnded( Catch::TestCaseStats const& _testCaseStats ) override {
        stream << "        [" << _stopwatch.elapsed() << " sec]\n";
        stream.flush();
        ConsoleReporter::testCaseEnded(_testCaseStats);
    }
#endif

    virtual void sectionStarting( Catch::SectionInfo const& _sectionInfo ) override {
        if (_firstSection)
            _firstSection = false;
        else {
            for (unsigned i = 0; i < _sectionNesting; ++i)
                stream << "    ";
            stream << "    --- " << _sectionInfo.name << "\n";
            stream.flush();
        }
        ++_sectionNesting;
        ConsoleReporter::sectionStarting(_sectionInfo);
    }

    void sectionEnded( Catch::SectionStats const& sectionStats ) override {
        --_sectionNesting;
        ConsoleReporter::sectionEnded(sectionStats);
    }

#ifdef CASE_LIST_BACKTRACE
    virtual bool assertionEnded( Catch::AssertionStats const& stats ) override {
        if (stats.assertionResult.getResultType() == Catch::ResultWas::FatalErrorCondition) {
            std::cerr << "\n\n********** CRASH: "
                      << stats.assertionResult.getMessage()
                      << " **********";
            fleece::Backtrace bt(5);
            bt.writeTo(std::cerr);
            std::cerr << "\n********** CRASH **********\n";
        }
        return Catch::ConsoleReporter::assertionEnded(stats);
    }
#endif

    std::string _curFile;
    bool _firstSection;
    unsigned _sectionNesting;
    time_t _start;
#ifdef CASE_LIST_STOPWATCH
    fleece::Stopwatch _stopwatch;
#endif
};

CATCH_REGISTER_REPORTER("list", CaseListReporter )
