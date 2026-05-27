#include "doost/string.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    int g_failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                       \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                           \
                      << ": check failed: " #condition "\n";                  \
            ++g_failures;                                                       \
        }                                                                       \
    } while (false)

    struct StringState {
        std::string value;
        bool unique;

        friend bool operator==(const StringState& lhs,
                               const StringState& rhs) {
            return lhs.value == rhs.value && lhs.unique == rhs.unique;
        }
    };

    std::vector<StringState> capture_states(
        const std::vector<doost::String>& values) {
        std::vector<StringState> states;
        states.reserve(values.size());
        for (const doost::String& value : values) {
            states.push_back({value.value(), value.unique()});
        }
        return states;
    }

    void bubble_sort(std::vector<doost::String>& values) {
        if (values.empty()) {
            return;
        }

        for (std::size_t pass = 0; pass != values.size(); ++pass) {
            for (std::size_t i = 1; i < values.size() - pass; ++i) {
                if (values[i] < values[i - 1]) {
                    using std::swap;
                    swap(values[i - 1], values[i]);
                }
            }
        }
    }

    void test_default_and_string_initialization() {
        doost::String empty;
        CHECK(!empty.has_value());
        CHECK(empty.empty());
        CHECK(empty.size() == 0);
        CHECK(!empty.unique());
        CHECK(empty.value().empty());
        CHECK(std::string_view(empty.c_str()).empty());

        std::ostringstream empty_output;
        empty_output << empty;
        CHECK(empty_output.str() == "String{unique=0, value=<null>}");

        doost::String text("alpha");
        CHECK(text.has_value());
        CHECK(!text.empty());
        CHECK(text.size() == 5);
        CHECK(text.unique());
        CHECK(text.value() == "alpha");
        CHECK(text.view() == "alpha");
        CHECK(std::string_view(text.c_str()) == "alpha");

        const std::string source = "beta";
        doost::String from_string(source);
        CHECK(from_string.unique());
        CHECK(from_string.value() == source);
    }

    void test_copy_initialization_and_assignment() {
        doost::String first("first");
        doost::String second(first);

        CHECK(first.value() == "first");
        CHECK(second.value() == "first");
        CHECK(!first.unique());
        CHECK(!second.unique());

        second = "second";
        CHECK(first.value() == "first");
        CHECK(second.value() == "second");
        CHECK(first.unique());
        CHECK(second.unique());

        doost::String third;
        third = first;
        CHECK(third.value() == "first");
        CHECK(!first.unique());
        CHECK(!third.unique());

        third = nullptr;
        CHECK(!third.has_value());
        CHECK(first.unique());
    }

    void test_move_and_swap_preserve_reference_bits() {
        doost::String shared("shared");
        doost::String alias(shared);
        doost::String unique("unique");

        CHECK(!shared.unique());
        CHECK(!alias.unique());
        CHECK(unique.unique());

        swap(shared, unique);

        CHECK(shared.value() == "unique");
        CHECK(unique.value() == "shared");
        CHECK(alias.value() == "shared");
        CHECK(shared.unique());
        CHECK(!unique.unique());
        CHECK(!alias.unique());

        doost::String moved(std::move(shared));
        CHECK(!shared.has_value());
        CHECK(moved.value() == "unique");
        CHECK(moved.unique());
    }

    void test_extraction_and_printing() {
        doost::String pointer("line\nvalue");
        doost::String alias(pointer);

        CHECK(pointer.value() == "line\nvalue");
        CHECK(pointer.view() == "line\nvalue");
        CHECK(!pointer.unique());

        std::ostringstream shared_output;
        shared_output << pointer;
        CHECK(shared_output.str() ==
            "String{unique=0, value=\"line\\nvalue\"}");

        alias.reset();
        std::ostringstream unique_output;
        unique_output << pointer;
        CHECK(unique_output.str() ==
            "String{unique=1, value=\"line\\nvalue\"}");
    }

    void test_debug_release_trace() {
        std::ostringstream trace;
        std::ostream* previous = doost::String::set_debug_trace(&trace);
        {
            doost::String scoped("traced");
            CHECK(trace.str().empty());
        }
        doost::String::set_debug_trace(previous);

        if constexpr (doost::string_debug_trace_enabled) {
            CHECK(trace.str() == "String release: \"traced\"\n");
        }
        else {
            CHECK(trace.str().empty());
        }
    }

    void test_bubble_sort_preserves_reference_bits() {
        std::vector<doost::String> values;
        values.reserve(6);

        values.emplace_back("kiwi");
        values.emplace_back("apple");
        values.emplace_back("pear");
        values.emplace_back("banana");
        values.emplace_back(values[1]);
        values.emplace_back("mango");

        doost::String outside_pear(values[2]);

        std::vector<StringState> expected = capture_states(values);
        std::sort(expected.begin(), expected.end(),
                  [](const StringState& lhs, const StringState& rhs) {
                      if (lhs.value == rhs.value) {
                          return lhs.unique < rhs.unique;
                      }
                      return lhs.value < rhs.value;
                  });

        bubble_sort(values);

        CHECK(values[0].value() == "apple");
        CHECK(values[1].value() == "apple");
        CHECK(values[2].value() == "banana");
        CHECK(values[3].value() == "kiwi");
        CHECK(values[4].value() == "mango");
        CHECK(values[5].value() == "pear");

        std::vector<StringState> actual = capture_states(values);
        CHECK(actual == expected);
        CHECK(outside_pear.value() == "pear");
        CHECK(!outside_pear.unique());
        CHECK(!values[5].unique());
    }

    void run_test(std::string_view name, void (*test)()) {
        const int failures_before = g_failures;
        try {
            test();
        }
        catch (const std::exception& exception) {
            std::cerr << name << ": unexpected exception: " << exception.what()
                << '\n';
            ++g_failures;
        }
        catch (...) {
            std::cerr << name << ": unexpected non-standard exception\n";
            ++g_failures;
        }

        if (g_failures == failures_before) {
            std::cout << "[PASS] " << name << '\n';
        }
        else {
            std::cout << "[FAIL] " << name << '\n';
        }
    }
} // namespace

int main() {
    run_test("default and string initialization",
             test_default_and_string_initialization);
    run_test("copy initialization and assignment",
             test_copy_initialization_and_assignment);
    run_test("move and swap preserve reference bits",
             test_move_and_swap_preserve_reference_bits);
    run_test("extraction and printing", test_extraction_and_printing);
    run_test("debug release trace", test_debug_release_trace);
    run_test("bubble sort preserves reference bits",
             test_bubble_sort_preserves_reference_bits);

    if (g_failures != 0) {
        std::cerr << g_failures << " test check(s) failed\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
