#include "cxx_test_support.h"


TEST_CASE("algorithm copies rotated ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int source[5] = { 1, 2, 3, 4, 5 };\n"
        "    int target[5] = { 0, 0, 0, 0, 0 };\n"
        "    int* end = std::rotate_copy(source, source + 2, source + 5, target);\n"
        "    if (end != target + 5) return 1;\n"
        "    if (target[0] != 3 || target[2] != 5 || target[3] != 1) return 2;\n"
        "    char text[5] = \"abcde\";\n"
        "    char letters[5] = { 0, 0, 0, 0, 0 };\n"
        "    if (std::rotate_copy(text, text + 1, text + 5, letters) != letters + 5) return 3;\n"
        "    if (letters[0] != 'b' || letters[4] != 'a') return 4;\n"
        "    double reals[3] = { 1.5, 2.5, 3.5 };\n"
        "    double copied[3] = { 0.0, 0.0, 0.0 };\n"
        "    std::rotate_copy(reals, reals + 2, reals + 3, copied);\n"
        "    return copied[0] == 3.5 && copied[1] == 1.5 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm copies ranges in reverse") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int source[3] = { 1, 2, 3 };\n"
        "    int target[3] = { 0, 0, 0 };\n"
        "    int* end = std::reverse_copy(source, source + 3, target);\n"
        "    if (end != target + 3) return 1;\n"
        "    if (target[0] != 3 || target[2] != 1) return 2;\n"
        "    char text[4] = \"abc\";\n"
        "    char letters[4] = { 0, 0, 0, 0 };\n"
        "    if (std::reverse_copy(text, text + 3, letters) != letters + 3) return 3;\n"
        "    if (letters[0] != 'c' || letters[2] != 'a') return 4;\n"
        "    double reals[2] = { 1.5, 2.5 };\n"
        "    double copied[2] = { 0.0, 0.0 };\n"
        "    std::reverse_copy(reals, reals + 2, copied);\n"
        "    return copied[0] == 2.5 && copied[1] == 1.5 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm orders partial and nth elements") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 5, 1, 4, 2, 3 };\n"
        "    std::nth_element(values, values + 2, values + 5);\n"
        "    if (values[2] != 3) return 1;\n"
        "    int sorted[5] = { 5, 1, 4, 2, 3 };\n"
        "    std::partial_sort(sorted, sorted + 3, sorted + 5);\n"
        "    if (sorted[0] != 1 || sorted[1] != 2 || sorted[2] != 3) return 2;\n"
        "    char text[4] = \"dcba\";\n"
        "    std::nth_element(text, text + 2, text + 4);\n"
        "    if (text[2] != 'c') return 3;\n"
        "    double reals[3] = { 2.5, 0.5, 1.5 };\n"
        "    std::nth_element(reals, reals + 1, reals + 3);\n"
        "    return reals[1] == 1.5 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm stable sorts arrays") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[4] = { 3, 1, 4, 2 };\n"
        "    std::stable_sort(values, values + 4);\n"
        "    if (values[0] != 1 || values[3] != 4) return 1;\n"
        "    char text[5] = \"dcba\";\n"
        "    std::stable_sort(text, text + 4);\n"
        "    if (text[0] != 'a' || text[3] != 'd') return 2;\n"
        "    double reals[3] = { 2.5, 0.5, 1.5 };\n"
        "    std::stable_sort(reals, reals + 3);\n"
        "    return reals[0] == 0.5 && reals[2] == 2.5 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm merges ranges in place") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[6] = { 1, 3, 5, 2, 4, 6 };\n"
        "    std::inplace_merge(values, values + 3, values + 6);\n"
        "    if (values[0] != 1 || values[1] != 2 || values[2] != 3 || values[5] != 6) return 1;\n"
        "    char text[6] = \"acebdf\";\n"
        "    std::inplace_merge(text, text + 3, text + 6);\n"
        "    if (text[0] != 'a' || text[1] != 'b' || text[5] != 'f') return 2;\n"
        "    double reals[4] = { 1.5, 3.5, 2.5, 4.5 };\n"
        "    std::inplace_merge(reals, reals + 2, reals + 4);\n"
        "    return reals[0] == 1.5 && reals[1] == 2.5 && reals[3] == 4.5 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm merges sorted ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 3, 5 };\n"
        "    int right[3] = { 2, 4, 6 };\n"
        "    int output[6] = { 0, 0, 0, 0, 0, 0 };\n"
        "    int* end = std::merge(left, left + 3, right, right + 3, output);\n"
        "    if (end != output + 6) return 1;\n"
        "    if (output[0] != 1 || output[1] != 2 || output[2] != 3 || output[5] != 6) return 2;\n"
        "    char a[3] = \"ace\";\n"
        "    char b[3] = \"bdf\";\n"
        "    char letters[6] = { 0, 0, 0, 0, 0, 0 };\n"
        "    if (std::merge(a, a + 3, b, b + 3, letters) != letters + 6) return 3;\n"
        "    if (letters[0] != 'a' || letters[1] != 'b' || letters[5] != 'f') return 4;\n"
        "    double x[2] = { 1.5, 3.5 };\n"
        "    double y[2] = { 2.5, 4.5 };\n"
        "    double z[4] = { 0.0, 0.0, 0.0, 0.0 };\n"
        "    return std::merge(x, x + 2, y, y + 2, z) == z + 4 && z[1] == 2.5 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm computes symmetric difference") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 3, 5 };\n"
        "    int right[3] = { 2, 3, 6 };\n"
        "    int output[4] = { 0, 0, 0, 0 };\n"
        "    int* end = std::set_symmetric_difference(left, left + 3, right, right + 3, output);\n"
        "    if (end != output + 4) return 1;\n"
        "    if (output[0] != 1 || output[1] != 2 || output[2] != 5 || output[3] != 6) return 2;\n"
        "    char a[3] = \"ace\";\n"
        "    char b[3] = \"bcd\";\n"
        "    char letters[6] = { 0, 0, 0, 0, 0, 0 };\n"
        "    if (std::set_symmetric_difference(a, a + 3, b, b + 3, letters) != letters + 4) return 3;\n"
        "    if (letters[0] != 'a' || letters[1] != 'b' || letters[2] != 'd' || letters[3] != 'e') return 4;\n"
        "    double x[2] = { 1.5, 3.5 };\n"
        "    double y[2] = { 2.5, 3.5 };\n"
        "    double z[4] = { 0.0, 0.0, 0.0, 0.0 };\n"
        "    return std::set_symmetric_difference(x, x + 2, y, y + 2, z) == z + 2 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm computes sorted difference") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 3, 5 };\n"
        "    int right[2] = { 3, 6 };\n"
        "    int output[3] = { 0, 0, 0 };\n"
        "    int* end = std::set_difference(left, left + 3, right, right + 2, output);\n"
        "    if (end != output + 2) return 1;\n"
        "    if (output[0] != 1 || output[1] != 5) return 2;\n"
        "    char a[3] = \"ace\";\n"
        "    char b[3] = \"bcd\";\n"
        "    char letters[3] = { 0, 0, 0 };\n"
        "    if (std::set_difference(a, a + 3, b, b + 3, letters) != letters + 2) return 3;\n"
        "    if (letters[0] != 'a' || letters[1] != 'e') return 4;\n"
        "    double x[2] = { 1.5, 2.5 };\n"
        "    double y[2] = { 1.5, 3.5 };\n"
        "    double z[2] = { 0.0, 0.0 };\n"
        "    return std::set_difference(x, x + 2, y, y + 2, z) == z + 1 && z[0] == 2.5 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm computes sorted intersection") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 3, 5 };\n"
        "    int right[3] = { 2, 3, 6 };\n"
        "    int output[3] = { 0, 0, 0 };\n"
        "    int* end = std::set_intersection(left, left + 3, right, right + 3, output);\n"
        "    if (end != output + 1 || output[0] != 3) return 1;\n"
        "    char a[3] = \"ace\";\n"
        "    char b[3] = \"bcd\";\n"
        "    char letters[3] = { 0, 0, 0 };\n"
        "    if (std::set_intersection(a, a + 3, b, b + 3, letters) != letters + 1) return 2;\n"
        "    if (letters[0] != 'c') return 3;\n"
        "    double x[2] = { 1.5, 3.5 };\n"
        "    double y[2] = { 2.5, 3.5 };\n"
        "    double z[2] = { 0.0, 0.0 };\n"
        "    return std::set_intersection(x, x + 2, y, y + 2, z) == z + 1 && z[0] == 3.5 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm computes sorted union") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 3, 5 };\n"
        "    int right[3] = { 2, 3, 6 };\n"
        "    int output[6] = { 0, 0, 0, 0, 0, 0 };\n"
        "    int* end = std::set_union(left, left + 3, right, right + 3, output);\n"
        "    if (end != output + 5) return 1;\n"
        "    if (output[0] != 1 || output[1] != 2 || output[2] != 3 || output[3] != 5 || output[4] != 6) return 2;\n"
        "    char a[3] = \"ace\";\n"
        "    char b[3] = \"bcd\";\n"
        "    char letters[6] = { 0, 0, 0, 0, 0, 0 };\n"
        "    if (std::set_union(a, a + 3, b, b + 3, letters) != letters + 5) return 3;\n"
        "    if (letters[0] != 'a' || letters[2] != 'c' || letters[4] != 'e') return 4;\n"
        "    double x[2] = { 1.5, 3.5 };\n"
        "    double y[2] = { 2.5, 3.5 };\n"
        "    double z[4] = { 0.0, 0.0, 0.0, 0.0 };\n"
        "    return std::set_union(x, x + 2, y, y + 2, z) == z + 3 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm checks sorted inclusion") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int big[5] = { 1, 2, 3, 4, 5 };\n"
        "    int small[3] = { 2, 3, 5 };\n"
        "    if (!std::includes(big, big + 5, small, small + 3)) return 1;\n"
        "    int missing[2] = { 2, 6 };\n"
        "    if (std::includes(big, big + 5, missing, missing + 2)) return 2;\n"
        "    char text[7] = \"abcdef\";\n"
        "    char part[4] = \"bde\";\n"
        "    if (!std::includes(text, text + 6, part, part + 3)) return 3;\n"
        "    double all[4] = { 1.5, 2.5, 3.5, 4.5 };\n"
        "    double some[2] = { 2.5, 4.5 };\n"
        "    if (!std::includes(all, all + 4, some, some + 2)) return 4;\n"
        "    return std::includes(text, text + 6, part, part) ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm compares ranges lexicographically") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    char left[3] = \"abc\";\n"
        "    char right[3] = \"abd\";\n"
        "    if (!std::lexicographical_compare(left, left + 3, right, right + 3)) return 1;\n"
        "    if (std::lexicographical_compare(right, right + 3, left, left + 3)) return 2;\n"
        "    if (std::lexicographical_compare(left, left + 3, left, left + 3)) return 3;\n"
        "    int shorter[2] = { 1, 2 };\n"
        "    int longer[3] = { 1, 2, 3 };\n"
        "    if (!std::lexicographical_compare(shorter, shorter + 2, longer, longer + 3)) return 4;\n"
        "    double a[2] = { 1.5, 2.5 };\n"
        "    double b[2] = { 1.5, 3.5 };\n"
        "    return std::lexicographical_compare(a, a + 2, b, b + 2) ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm finds first matching element") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int text[6] = { 1, 2, 3, 4, 5, 6 };\n"
        "    int set[2] = { 4, 6 };\n"
        "    if (std::find_first_of(text, text + 6, set, set + 2) != text + 3) return 1;\n"
        "    int missing[2] = { 8, 9 };\n"
        "    if (std::find_first_of(text, text + 6, missing, missing + 2) != text + 6) return 2;\n"
        "    char letters[7] = \"abcdef\";\n"
        "    char chars[3] = \"xd\";\n"
        "    if (std::find_first_of(letters, letters + 6, chars, chars + 2) != letters + 3) return 3;\n"
        "    double reals[3] = { 1.5, 2.5, 3.5 };\n"
        "    double wanted[2] = { 9.0, 2.5 };\n"
        "    if (std::find_first_of(reals, reals + 3, wanted, wanted + 2) != reals + 1) return 4;\n"
        "    return std::find_first_of(letters, letters + 6, chars, chars) == letters + 6 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm reports sorted prefixes") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 1, 2, 4, 3, 5 };\n"
        "    if (std::is_sorted_until(values, values + 5) != values + 3) return 1;\n"
        "    char text[5] = \"abdc\";\n"
        "    if (std::is_sorted_until(text, text + 4) != text + 3) return 2;\n"
        "    double reals[3] = { 1.5, 3.5, 2.5 };\n"
        "    if (std::is_sorted_until(reals, reals + 3) != reals + 2) return 3;\n"
        "    int sorted[3] = { 1, 2, 3 };\n"
        "    if (std::is_sorted_until(sorted, sorted + 3) != sorted + 3) return 4;\n"
        "    return std::is_sorted_until(sorted, sorted) == sorted ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm detects sorted ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int sorted[4] = { 1, 2, 3, 4 };\n"
        "    int unsorted[4] = { 1, 3, 2, 4 };\n"
        "    if (!std::is_sorted(sorted, sorted + 4)) return 1;\n"
        "    if (std::is_sorted(unsorted, unsorted + 4)) return 2;\n"
        "    char text[5] = \"abcd\";\n"
        "    if (!std::is_sorted(text, text + 4)) return 3;\n"
        "    double reals[3] = { 1.5, 2.5, 3.5 };\n"
        "    if (!std::is_sorted(reals, reals + 3)) return 4;\n"
        "    int equal[3] = { 2, 2, 2 };\n"
        "    if (!std::is_sorted(equal, equal + 3)) return 5;\n"
        "    return std::is_sorted(text, text) ? 0 : 6;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm equal compares two bounded ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 2, 3 };\n"
        "    int right[3] = { 1, 2, 3 };\n"
        "    if (!std::equal(left, left + 3, right, right + 3)) return 1;\n"
        "    int shorter[2] = { 1, 2 };\n"
        "    if (std::equal(left, left + 3, shorter, shorter + 2)) return 2;\n"
        "    char a[3] = \"ab\";\n"
        "    char b[2] = \"ab\";\n"
        "    if (!std::equal(a, a + 2, b, b + 2)) return 3;\n"
        "    double x[2] = { 1.5, 2.5 };\n"
        "    double y[2] = { 1.5, 2.5 };\n"
        "    return std::equal(x, x + 2, y, y + 2) ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm finds the last subsequence") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int text[7] = { 1, 2, 3, 1, 2, 3, 4 };\n"
        "    int needle[2] = { 1, 2 };\n"
        "    if (std::find_end(text, text + 7, needle, needle + 2) != text + 3) return 1;\n"
        "    char letters[8] = \"abcabcx\";\n"
        "    char part[3] = \"abc\";\n"
        "    if (std::find_end(letters, letters + 7, part, part + 3) != letters + 3) return 2;\n"
        "    char missing[2] = \"zz\";\n"
        "    if (std::find_end(letters, letters + 7, missing, missing + 2) != letters + 7) return 3;\n"
        "    double reals[4] = { 1.5, 2.5, 1.5, 2.5 };\n"
        "    double pair[2] = { 1.5, 2.5 };\n"
        "    if (std::find_end(reals, reals + 4, pair, pair + 2) != reals + 2) return 4;\n"
        "    return std::find_end(letters, letters + 7, part, part) == letters + 7 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm searches repeated values") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[6] = { 1, 2, 2, 2, 3, 4 };\n"
        "    if (std::search_n(values, values + 6, 3, 2) != values + 1) return 1;\n"
        "    if (std::search_n(values, values + 6, 4, 2) != values + 6) return 2;\n"
        "    char text[8] = \"abbcccd\";\n"
        "    if (std::search_n(text, text + 7, 3, 'c') != text + 3) return 3;\n"
        "    double reals[4] = { 1.5, 2.5, 2.5, 3.5 };\n"
        "    if (std::search_n(reals, reals + 4, 2, 2.5) != reals + 1) return 4;\n"
        "    return std::search_n(text, text + 7, 0, 'z') == text ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm finds adjacent duplicates") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 1, 2, 2, 3, 3 };\n"
        "    if (std::adjacent_find(values, values + 5) != values + 1) return 1;\n"
        "    char text[6] = \"abcde\";\n"
        "    if (std::adjacent_find(text, text + 5) != text + 5) return 2;\n"
        "    char doubled[6] = \"abbcd\";\n"
        "    if (std::adjacent_find(doubled, doubled + 5) != doubled + 1) return 3;\n"
        "    double reals[3] = { 1.5, 2.5, 2.5 };\n"
        "    if (std::adjacent_find(reals, reals + 3) != reals + 1) return 4;\n"
        "    return std::adjacent_find(text, text) == text ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm searches subsequences") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int text[6] = { 1, 2, 3, 4, 5, 6 };\n"
        "    int needle[2] = { 3, 4 };\n"
        "    if (std::search(text, text + 6, needle, needle + 2) != text + 2) return 1;\n"
        "    int missing[2] = { 8, 9 };\n"
        "    if (std::search(text, text + 6, missing, missing + 2) != text + 6) return 2;\n"
        "    char letters[7] = \"abcdef\";\n"
        "    char pair[2] = \"cd\";\n"
        "    if (std::search(letters, letters + 6, pair, pair + 2) != letters + 2) return 3;\n"
        "    double reals[4] = { 1.5, 2.5, 3.5, 4.5 };\n"
        "    double tail[2] = { 3.5, 4.5 };\n"
        "    if (std::search(reals, reals + 4, tail, tail + 2) != reals + 2) return 4;\n"
        "    return std::search(letters, letters + 6, pair, pair) == letters ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm rotates ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 1, 2, 3, 4, 5 };\n"
        "    int* middle = std::rotate(values, values + 2, values + 5);\n"
        "    if (middle != values + 3) return 1;\n"
        "    if (values[0] != 3 || values[2] != 5 || values[3] != 1) return 2;\n"
        "    char text[5] = \"abcde\";\n"
        "    if (std::rotate(text, text + 1, text + 5) != text + 4) return 3;\n"
        "    if (text[0] != 'b' || text[4] != 'a') return 4;\n"
        "    double reals[3] = { 1.5, 2.5, 3.5 };\n"
        "    std::rotate(reals, reals + 2, reals + 3);\n"
        "    if (reals[0] != 3.5 || reals[1] != 1.5) return 5;\n"
        "    if (std::rotate(values, values, values + 3) != values + 3) return 6;\n"
        "    return std::rotate(values, values + 3, values + 3) == values ? 0 : 7;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm swaps ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 2, 3 };\n"
        "    int right[3] = { 4, 5, 6 };\n"
        "    int* end = std::swap_ranges(left, left + 3, right);\n"
        "    if (end != right + 3) return 1;\n"
        "    if (left[0] != 4 || right[2] != 3) return 2;\n"
        "    char a[3] = \"abc\";\n"
        "    char b[3] = \"xyz\";\n"
        "    std::swap_ranges(a, a + 3, b);\n"
        "    if (a[0] != 'x' || b[2] != 'c') return 3;\n"
        "    double x[2] = { 1.5, 2.5 };\n"
        "    double y[2] = { 3.5, 4.5 };\n"
        "    std::swap_ranges(x, x + 2, y);\n"
        "    return x[0] == 3.5 && y[1] == 2.5 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm unique compacts duplicates") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[6] = { 1, 1, 2, 3, 3, 3 };\n"
        "    int* end = std::unique(values, values + 6);\n"
        "    if (end != values + 3) return 1;\n"
        "    if (values[0] != 1 || values[1] != 2 || values[2] != 3) return 2;\n"
        "    char text[7] = \"aabbcc\";\n"
        "    char* tail = std::unique(text, text + 6);\n"
        "    if (tail != text + 3) return 3;\n"
        "    if (text[0] != 'a' || text[1] != 'b' || text[2] != 'c') return 4;\n"
        "    double reals[4] = { 1.5, 1.5, 2.5, 2.5 };\n"
        "    if (std::unique(reals, reals + 4) != reals + 2) return 5;\n"
        "    return std::unique(values, values) == values ? 0 : 6;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm binary searches sorted ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 1, 3, 3, 7, 9 };\n"
        "    if (!std::binary_search(values, values + 5, 3)) return 1;\n"
        "    if (std::binary_search(values, values + 5, 4)) return 2;\n"
        "    if (std::lower_bound(values, values + 5, 3) != values + 1) return 3;\n"
        "    if (std::upper_bound(values, values + 5, 3) != values + 3) return 4;\n"
        "    char text[5] = \"acegi\";\n"
        "    if (!std::binary_search(text, text + 5, 'e')) return 5;\n"
        "    double reals[4] = { 0.5, 1.5, 2.5, 3.5 };\n"
        "    return std::binary_search(reals, reals + 4, 2.5) ? 0 : 6;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("template value initialization") {
    const ProgramResult program = compile_and_run(
        "#include <utility>\n"
        "template<typename T> T make_value() { return T(); }\n"
        "int main() {\n"
        "    if (make_value<int>() != 0) return 1;\n"
        "    if (make_value<double>() != 0.0) return 2;\n"
        "    std::pair<int, int> pair = make_value<std::pair<int, int> >();\n"
        "    return pair.first == 0 && pair.second == 0 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("indirect calls pass floating point arguments") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "double scale(int factor, double value) { return value * factor; }\n"
        "int main() {\n"
        "    double reals[3] = { 1.5, 3.5, 2.5 };\n"
        "    std::sort(reals, reals + 3,\n"
        "              [](double left, double right) { return right < left; });\n"
        "    if (reals[0] != 3.5 || reals[2] != 1.5) return 1;\n"
        "    double (*operation)(int, double) = scale;\n"
        "    if (operation(3, 1.5) != 4.5) return 2;\n"
        "    double (*square)(double) = [](double value) { return value * value; };\n"
        "    return square(2.5) == 6.25 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("class methods bind const reference temporaries") {
    const ProgramResult program = compile_and_run(
        "struct Accumulator {\n"
        "    int add(const int& value) { return value + 1; }\n"
        "    double scale(const double& value) { return value * 2.0; }\n"
        "};\n"
        "template<typename T> struct Holder {\n"
        "    T echo(const T& value) const { return value; }\n"
        "};\n"
        "int main() {\n"
        "    Accumulator accumulator;\n"
        "    if (accumulator.add(3) != 4) return 1;\n"
        "    if (accumulator.scale(1.5) != 3.0) return 2;\n"
        "    Holder<int> holder;\n"
        "    return holder.echo(5) == 5 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("class methods accept string temporaries") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "struct Reader {\n"
        "    int length(const std::string& text) const { return text.size(); }\n"
        "};\n"
        "int main() {\n"
        "    Reader reader;\n"
        "    return reader.length(\"hello\") == 5 ? 0 : 1;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std map inserts updates and counts") {
    const ProgramResult program = compile_and_run(
        "#include <map>\n"
        "int main() {\n"
        "    std::map<int, int> values;\n"
        "    values[3] = 30;\n"
        "    values[1] = 10;\n"
        "    values[3] = 33;\n"
        "    if (values.size() != 2) return 1;\n"
        "    if (values[1] != 10 || values[3] != 33) return 2;\n"
        "    if (values.count(3) != 1 || values.count(2) != 0) return 3;\n"
        "    values[2] = 20;\n"
        "    if (values.size() != 3) return 4;\n"
        "    values.clear();\n"
        "    return values.empty() ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std vector copies and assigns") {
    const ProgramResult program = compile_and_run(
        "#include <vector>\n"
        "#include <utility>\n"
        "int main() {\n"
        "    std::vector<int> first;\n"
        "    first.push_back(1);\n"
        "    first.push_back(2);\n"
        "    std::vector<int> second = first;\n"
        "    if (second.size() != 2 || second[1] != 2) return 1;\n"
        "    std::vector<int> third;\n"
        "    third = first;\n"
        "    if (third.size() != 2 || third[0] != 1) return 2;\n"
        "    third.push_back(3);\n"
        "    if (third.size() != 3 || second.size() != 2) return 3;\n"
        "    std::vector<std::pair<int, int> > pairs;\n"
        "    std::pair<int, int> value;\n"
        "    value.first = 4;\n"
        "    value.second = 5;\n"
        "    pairs.push_back(value);\n"
        "    std::vector<std::pair<int, int> > copied = pairs;\n"
        "    return copied[0].first == 4 && copied[0].second == 5 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std vector resize preserves and zero fills") {
    const ProgramResult program = compile_and_run(
        "#include <vector>\n"
        "#include <utility>\n"
        "int main() {\n"
        "    std::vector<int> values;\n"
        "    values.push_back(4);\n"
        "    values.resize(3);\n"
        "    if (values.size() != 3 || values[0] != 4) return 1;\n"
        "    if (values[2] != 0) return 2;\n"
        "    values.resize(1);\n"
        "    if (values.size() != 1 || values[0] != 4) return 3;\n"
        "    std::vector<std::pair<int, int> > pairs;\n"
        "    pairs.resize(2);\n"
        "    if (pairs.size() != 2) return 4;\n"
        "    return pairs[1].first == 0 && pairs[1].second == 0 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std vector resize insert and erase") {
    const ProgramResult program = compile_and_run(
        "#include <vector>\n"
        "int main() {\n"
        "    std::vector<int> values;\n"
        "    values.push_back(1);\n"
        "    values.push_back(3);\n"
        "    values.insert(1, 2);\n"
        "    if (values.size() != 3 || values[1] != 2) return 1;\n"
        "    values.erase(1);\n"
        "    return values.size() == 2 && values[0] == 1 && values[1] == 3 ? 0 : 2;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std optional and string view") {
    const ProgramResult program = compile_and_run(
        "#include <optional>\n"
        "#include <string_view>\n"
        "#include <utility>\n"
        "int main() {\n"
        "    std::optional<int> item;\n"
        "    if (item.has_value() || item) return 1;\n"
        "    item.set(7);\n"
        "    if (!item.has_value() || item.value() != 7) return 2;\n"
        "    if (*item != 7 || item.value_or(3) != 7) return 3;\n"
        "    *item = 9;\n"
        "    if (item.value() != 9) return 4;\n"
        "    item.reset();\n"
        "    if (item.value_or(3) != 3) return 5;\n"
        "    item = 11;\n"
        "    if (item.value() != 11) return 6;\n"
        "    item.emplace();\n"
        "    if (item.value() != 0) return 7;\n"
        "    item.emplace(13);\n"
        "    if (item.value() != 13) return 8;\n"
        "    std::optional<std::pair<int, int> > pair;\n"
        "    std::pair<int, int> value;\n"
        "    value.first = 4;\n"
        "    value.second = 5;\n"
        "    pair = value;\n"
        "    if (pair->first != 4 || pair->second != 5) return 9;\n"
        "    pair.emplace();\n"
        "    if (pair.value().second != 0) return 10;\n"
        "    std::string_view view(\"hello\");\n"
        "    if (view.size() != 5 || view.empty()) return 5;\n"
        "    if (view[0] != 'h' || view.back() != 'o') return 6;\n"
        "    if (!view.starts_with(\"hell\") || !view.starts_with('h')) return 7;\n"
        "    if (!view.ends_with(\"llo\") || !view.ends_with('o')) return 8;\n"
        "    std::string_view tail = view.substr(1, 3);\n"
        "    if (tail.size() != 3 || tail[0] != 'e') return 7;\n"
        "    view.remove_prefix(1);\n"
        "    view.remove_suffix(1);\n"
        "    return view.size() == 3 &&\n"
        "                   view.compare(std::string_view(\"ell\")) == 0 ? 0 : 8;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std vector grows and preserves values") {
    const ProgramResult program = compile_and_run(
        "#include <vector>\n"
        "#include <utility>\n"
        "int main() {\n"
        "    std::vector<int> values;\n"
        "    values.push_back(3);\n"
        "    values.push_back(5);\n"
        "    values.push_back(7);\n"
        "    if (values.size() != 3 || values.empty()) return 1;\n"
        "    if (values[0] != 3 || values.back() != 7) return 2;\n"
        "    if (values.end() - values.begin() != 3) return 3;\n"
        "    values.pop_back();\n"
        "    if (values.size() != 2 || values.back() != 5) return 4;\n"
        "    values.clear();\n"
        "    if (!values.empty()) return 5;\n"
        "    values.reserve(8);\n"
        "    values.push_back(11);\n"
        "    if (values.capacity() < 8 || values[0] != 11) return 6;\n"
        "    std::vector<double> reals;\n"
        "    reals.push_back(1.5);\n"
        "    reals.push_back(2.5);\n"
        "    if (reals[0] != 1.5 || reals.back() != 2.5) return 7;\n"
        "    std::vector<std::pair<int, int> > pairs;\n"
        "    std::pair<int, int> first;\n"
        "    first.first = 1;\n"
        "    first.second = 2;\n"
        "    pairs.push_back(first);\n"
        "    return pairs[0].first == 1 && pairs[0].second == 2 ? 0 : 8;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std atomic uses ll/sc operations") {
    const ProgramResult program = compile_and_run(
        "#include <atomic>\n"
        "int main() {\n"
        "    std::atomic<int> counter;\n"
        "    counter.store(5);\n"
        "    if (counter.load() != 5) return 1;\n"
        "    counter = 7;\n"
        "    if (counter.load() != 7) return 2;\n"
        "    if (counter.fetch_add(3) != 7) return 3;\n"
        "    if (counter.load() != 10) return 4;\n"
        "    int expected = 10;\n"
        "    if (!counter.compare_exchange_weak(expected, 20)) return 5;\n"
        "    if (counter.load() != 20) return 6;\n"
        "    if (counter.exchange(30) != 20) return 7;\n"
        "    return counter.load() == 30 ? 0 : 8;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("qualified class templates instantiate") {
    const ProgramResult program = compile_and_run(
        "namespace N {\n"
        "template<typename A, typename B> class Box {\n"
        "public:\n"
        "    A first;\n"
        "    B second;\n"
        "};\n"
        "}\n"
        "int main() {\n"
        "    N::Box<int, char> box;\n"
        "    box.first = 7;\n"
        "    box.second = 'x';\n"
        "    return box.first == 7 && box.second == 'x' ? 0 : 1;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std array and pair containers") {
    const ProgramResult program = compile_and_run(
        "#include <array>\n"
        "#include <utility>\n"
        "int main() {\n"
        "    std::array<int, 3> values;\n"
        "    values[0] = 1;\n"
        "    values[1] = 2;\n"
        "    values[2] = 3;\n"
        "    if (values.size() != 3 || values.front() != 1) return 1;\n"
        "    if (values.back() != 3) return 2;\n"
        "    if (values.end() - values.begin() != 3) return 3;\n"
        "    std::array<double, 2> reals;\n"
        "    reals[0] = 1.5;\n"
        "    reals[1] = 2.5;\n"
        "    if (reals[0] != 1.5 || reals.back() != 2.5) return 4;\n"
        "    std::pair<int, int> pair;\n"
        "    pair.first = 4;\n"
        "    pair.second = 5;\n"
        "    return pair.first + pair.second == 9 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("function templates overload non-template names") {
    const ProgramResult program = compile_and_run(
        "namespace std {\n"
        "inline void arrange(int* first, int* last) { }\n"
        "template<typename Compare>\n"
        "inline void arrange(int* first, int* last, Compare compare) {\n"
        "    int* current = first;\n"
        "    if (current != last) current = current + 1;\n"
        "    while (current < last) {\n"
        "        int value = *current;\n"
        "        int* hole = current;\n"
        "        while (hole > first && compare(value, *(hole - 1))) {\n"
        "            *hole = *(hole - 1);\n"
        "            hole = hole - 1;\n"
        "        }\n"
        "        *hole = value;\n"
        "        current = current + 1;\n"
        "    }\n"
        "}\n"
        "}\n"
        "int main() {\n"
        "    int values[4] = { 3, 1, 4, 2 };\n"
        "    std::arrange(values, values + 4,\n"
        "                 [](int left, int right) { return right < left; });\n"
        "    return values[0] == 4 && values[3] == 1 ? 0 : 1;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("function templates substitute pointer element types") {
    const ProgramResult program = compile_and_run(
        "template<typename T> void sort_values(T* first, T* last) {\n"
        "    T* current = first;\n"
        "    if (current != last) current = current + 1;\n"
        "    while (current < last) {\n"
        "        T value = *current;\n"
        "        T* hole = current;\n"
        "        while (hole > first && value < *(hole - 1)) {\n"
        "            *hole = *(hole - 1);\n"
        "            hole = hole - 1;\n"
        "        }\n"
        "        *hole = value;\n"
        "        current = current + 1;\n"
        "    }\n"
        "}\n"
        "int main() {\n"
        "    char text[4] = \"dcba\";\n"
        "    sort_values(text, text + 4);\n"
        "    if (text[0] != 'a' || text[3] != 'd') return 1;\n"
        "    double reals[3] = { 2.5, 0.5, 1.5 };\n"
        "    sort_values(reals, reals + 3);\n"
        "    return reals[0] == 0.5 && reals[2] == 2.5 ? 0 : 2;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm predicate callbacks") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "bool positive(int value) { return value > 0; }\n"
        "int main() {\n"
        "    int values[5] = { -1, 2, -3, 4, 5 };\n"
        "    if (!std::any_of(values, values + 5, positive)) return 1;\n"
        "    if (std::all_of(values, values + 5, positive)) return 2;\n"
        "    if (!std::none_of(values, values + 5,\n"
        "                      [](int value) { return value == 0; })) return 3;\n"
        "    if (std::find_if(values, values + 5, positive) != values + 1) return 4;\n"
        "    if (std::find_if_not(values, values + 5, positive) != values) return 5;\n"
        "    if (std::count_if(values, values + 5, positive) != 3) return 6;\n"
        "    int output[5] = { 0, 0, 0, 0, 0 };\n"
        "    std::transform(values, values + 5, output,\n"
        "                   [](int value) { return value * 2; });\n"
        "    if (output[1] != 4 || output[4] != 10) return 7;\n"
        "    int* end = std::remove_if(values, values + 5, positive);\n"
        "    if (end != values + 2 || values[0] != -1 || values[1] != -3) return 8;\n"
        "    std::replace_if(values, values + 5,\n"
        "                    [](int value) { return value < 0; }, 0);\n"
        "    if (values[0] != 0 || values[1] != 0) return 9;\n"
        "    std::sort(values, values + 5,\n"
        "              [](int left, int right) { return right < left; });\n"
        "    return values[0] == 5 && values[4] == 0 ? 0 : 10;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm finds unsigned and float extremes") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    unsigned int values[3] = { 9, 2, 7 };\n"
        "    if (std::min_element(values, values + 3) != values + 1) return 1;\n"
        "    if (std::max_element(values, values + 3) != values) return 2;\n"
        "    float reals[3] = { 1.5f, 3.5f, 2.5f };\n"
        "    if (std::min_element(reals, reals + 3) != reals) return 3;\n"
        "    return std::max_element(reals, reals + 3) == reals + 1 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm finds minimum and maximum elements") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[4] = { 3, 9, 2, 7 };\n"
        "    if (std::min_element(values, values + 4) != values + 2) return 1;\n"
        "    if (std::max_element(values, values + 4) != values + 1) return 2;\n"
        "    char text[4] = \"dcba\";\n"
        "    if (std::min_element(text, text + 4) != text + 3) return 3;\n"
        "    double reals[3] = { 1.5, 0.5, 2.5 };\n"
        "    if (std::max_element(reals, reals + 3) != reals + 2) return 4;\n"
        "    return std::max_element(values, values) == values ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("pointer differences count elements") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    int values[5] = { 1, 2, 3, 4, 5 };\n"
        "    int* end = values + 3;\n"
        "    if (end - values != 3) return 1;\n"
        "    char text[6] = \"abcde\";\n"
        "    char* pointer = text + 4;\n"
        "    if (pointer - text != 4) return 2;\n"
        "    double reals[3] = { 0.0, 0.0, 0.0 };\n"
        "    double* last = reals + 2;\n"
        "    return last - reals == 2 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm remove compacts ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 1, 2, 1, 3, 1 };\n"
        "    int* end = std::remove(values, values + 5, 1);\n"
        "    if (end != values + 2) return 1;\n"
        "    if (values[0] != 2 || values[1] != 3) return 2;\n"
        "    char text[6] = \"abaca\";\n"
        "    char* tail = std::remove(text, text + 5, 'a');\n"
        "    if (tail != text + 2) return 3;\n"
        "    if (text[0] != 'b' || text[1] != 'c') return 4;\n"
        "    double reals[3] = { 1.5, 2.5, 1.5 };\n"
        "    return std::remove(reals, reals + 3, 1.5) == reals + 1 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm replace substitutes values") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[4] = { 1, 2, 1, 3 };\n"
        "    std::replace(values, values + 4, 1, 9);\n"
        "    if (values[0] != 9 || values[1] != 2 || values[2] != 9) return 1;\n"
        "    char text[5] = \"aabaa\";\n"
        "    std::replace(text, text + 5, 'a', 'z');\n"
        "    if (text[0] != 'z' || text[2] != 'b' || text[4] != 'z') return 2;\n"
        "    double reals[3] = { 1.5, 2.5, 1.5 };\n"
        "    std::replace(reals, reals + 3, 1.5, 9.0);\n"
        "    return reals[0] == 9.0 && reals[1] == 2.5 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm copy_n copies counted elements") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int source[3] = { 1, 2, 3 };\n"
        "    int target[3] = { 0, 0, 0 };\n"
        "    int* end = std::copy_n(source, 2, target);\n"
        "    if (end != target + 2) return 1;\n"
        "    if (target[1] != 2 || target[2] != 0) return 2;\n"
        "    double reals[2] = { 1.5, 2.5 };\n"
        "    double out[2] = { 0.0, 0.0 };\n"
        "    std::copy_n(reals, 2, out);\n"
        "    if (out[0] != 1.5 || out[1] != 2.5) return 3;\n"
        "    return std::copy_n(source, 0, target) == target ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm fill_n writes counted elements") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[4] = { 0, 0, 0, 0 };\n"
        "    int* end = std::fill_n(values, 3, 5);\n"
        "    if (end != values + 3) return 1;\n"
        "    if (values[2] != 5 || values[3] != 0) return 2;\n"
        "    double reals[2] = { 0.0, 0.0 };\n"
        "    std::fill_n(reals, 2, 1.5);\n"
        "    if (reals[0] != 1.5 || reals[1] != 1.5) return 3;\n"
        "    return std::fill_n(values, 0, 9) == values ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("numeric accumulates and fills sequences") {
    const ProgramResult program = compile_and_run(
        "#include <numeric>\n"
        "int main() {\n"
        "    int values[4] = { 1, 2, 3, 4 };\n"
        "    if (std::accumulate(values, values + 4, 0) != 10) return 1;\n"
        "    double reals[3] = { 1.5, 2.5, 3.0 };\n"
        "    if (std::accumulate(reals, reals + 3, 0.0) != 7.0) return 2;\n"
        "    int sequence[4] = { 0, 0, 0, 0 };\n"
        "    std::iota(sequence, sequence + 4, 5);\n"
        "    return sequence[0] == 5 && sequence[3] == 8 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm equal compares ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 2, 3 };\n"
        "    int right[3] = { 1, 2, 3 };\n"
        "    if (!std::equal(left, left + 3, right)) return 1;\n"
        "    right[1] = 9;\n"
        "    if (std::equal(left, left + 3, right)) return 2;\n"
        "    char a[4] = \"abc\";\n"
        "    char b[4] = \"abc\";\n"
        "    if (!std::equal(a, a + 3, b)) return 3;\n"
        "    double x[2] = { 1.5, 2.5 };\n"
        "    double y[2] = { 1.5, 2.5 };\n"
        "    return std::equal(x, x + 2, y) ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm count tallies elements") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 1, 2, 2, 3, 2 };\n"
        "    if (std::count(values, values + 5, 2) != 3) return 1;\n"
        "    char text[6] = \"abaca\";\n"
        "    if (std::count(text, text + 5, 'a') != 3) return 2;\n"
        "    double reals[3] = { 1.5, 2.5, 1.5 };\n"
        "    if (std::count(reals, reals + 3, 1.5) != 2) return 3;\n"
        "    return std::count(reals, reals + 3, 9.0) == 0 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

