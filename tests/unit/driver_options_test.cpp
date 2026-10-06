// How `cppl` reads a command line: which argument is an option's value, as
// Clang's driver reads it (compiler/driver/src/options.cpp).
//
// The command line is handed on with its inputs and outputs taken out, to the
// preprocessor and to the analysis, and with the verified unit replaced, to
// the compile of the runtime program. An argument taken for an input or an
// option when it is a value would be taken out, or kept, apart from its option,
// which would then take the next argument as its value in one command and not
// in another, and the analysis would read other options than the program is
// compiled with (SPEC.md ARITH-014).

#include "cppl/driver/options.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <string>
#include <vector>

using cppl::driver::Options;
using cppl::driver::parse;
using cppl::driver::separate_values;

namespace {

Options read(const std::vector<std::string>& arguments) {
    std::vector<const char*> argv{"cppl"};
    for (const std::string& argument : arguments) {
        argv.push_back(argument.c_str());
    }
    return parse(static_cast<int>(argv.size()), argv.data());
}

std::vector<std::size_t> values_of(const Options& options) {
    std::vector<std::size_t> values;
    for (std::size_t index = 0; index < options.option_value.size(); ++index) {
        if (options.option_value[index]) {
            values.push_back(index);
        }
    }
    return values;
}

} // namespace

// SPEC: ARITH-014
CPPL_TEST(each_kind_of_option_reads_as_many_values_as_the_driver_does) {
    // Separate, in each spelling the driver accepts.
    CPPL_CHECK_EQ(separate_values("--config"), 1U);
    CPPL_CHECK_EQ(separate_values("--include-directory"), 1U);
    CPPL_CHECK_EQ(separate_values("-dependency-file"), 1U);
    CPPL_CHECK_EQ(separate_values("-Xanalyzer"), 1U);
    CPPL_CHECK_EQ(separate_values("--serialize-diagnostics"), 1U);
    // JoinedOrSeparate written alone, and with its value joined to it.
    CPPL_CHECK_EQ(separate_values("-I"), 1U);
    CPPL_CHECK_EQ(separate_values("-Iinclude"), 0U);
    CPPL_CHECK_EQ(separate_values("-iapinotes-modules"), 1U);
    CPPL_CHECK_EQ(separate_values("--config=x.cfg"), 0U);
    // JoinedAndSeparate.
    CPPL_CHECK_EQ(separate_values("-Xarch_arm64"), 1U);
    CPPL_CHECK_EQ(separate_values("-Xopenmp-target=nvptx64"), 1U);
    // MultiArg.
    CPPL_CHECK_EQ(separate_values("-sectcreate"), 3U);
    CPPL_CHECK_EQ(separate_values("-segaddr"), 2U);
    // Flags and inputs.
    CPPL_CHECK_EQ(separate_values("-funsigned-char"), 0U);
    CPPL_CHECK_EQ(separate_values("-c"), 0U);
    CPPL_CHECK_EQ(separate_values("main.cpp"), 0U);
}

// SPEC: ARITH-014
CPPL_TEST(a_value_is_never_an_input) {
    const Options options = read({"--config", "x.cfg", "--include-directory", "include", "main.cpp"});
    CPPL_CHECK_EQ(values_of(options), (std::vector<std::size_t>{1, 3}));
    CPPL_CHECK_EQ(options.positional, (std::vector<std::size_t>{4}));
    CPPL_CHECK_EQ(options.inputs.size(), 1U);
    CPPL_CHECK_EQ(options.inputs.front().path, std::string("main.cpp"));
}

// SPEC: ARITH-014
CPPL_TEST(every_value_of_an_option_that_reads_several_is_its_own) {
    const Options options = read({"-sectcreate", "segment", "section", "file.cpp", "main.cpp"});
    CPPL_CHECK_EQ(values_of(options), (std::vector<std::size_t>{1, 2, 3}));
    CPPL_CHECK_EQ(options.positional, (std::vector<std::size_t>{4}));
}

// SPEC: ARITH-014
CPPL_TEST(a_value_that_reads_as_an_option_is_still_a_value) {
    const Options options = read({"-Xanalyzer", "-c", "-Xanalyzer", "-o", "-funsigned-char", "main.cpp"});
    CPPL_CHECK_EQ(values_of(options), (std::vector<std::size_t>{1, 3}));
    CPPL_CHECK(!options.compile_only);
    CPPL_CHECK(options.output.empty());
    CPPL_CHECK_EQ(options.positional, (std::vector<std::size_t>{5}));

    const Options explicit_language = read({"-Xclang", "-x", "main.cpp"});
    CPPL_CHECK(!explicit_language.explicit_language);

    const Options output = read({"-c", "main.cpp", "-o", "-o"});
    CPPL_CHECK(output.compile_only);
    CPPL_CHECK_EQ(output.output, std::string("-o"));
}

// SPEC: ARITH-014
CPPL_TEST(the_standard_is_never_read_from_a_value) {
    const Options options = read({"-std=c++20", "-Xanalyzer", "-std=c++11", "main.cpp"});
    CPPL_CHECK_EQ(options.standard, std::string("c++20"));
}
