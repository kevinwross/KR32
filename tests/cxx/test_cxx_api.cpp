#include "doctest.h"

#include "kr32/container/container.h"
#include "kr32/cxx/abi.h"
#include "kr32/cxx/cxx.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace abi = kr32::cxx::abi;

// Public API shape. These checks intentionally do not require the front end to
// compile anything: the compiler is still a diagnostic stub, and the same test
// must stay valid once it is implemented.

static_assert(std::is_enum_v<kr32::cxx::Emit>);
static_assert(static_cast<int>(kr32::cxx::Emit::Assembly) !=
              static_cast<int>(kr32::cxx::Emit::Object));
static_assert(std::is_same_v<decltype(kr32::cxx::Options::emit), kr32::cxx::Emit>);
static_assert(std::is_same_v<decltype(kr32::cxx::Options::source_name),
                             std::string>);
static_assert(std::is_same_v<decltype(kr32::cxx::Options::exceptions), bool>);
static_assert(std::is_same_v<decltype(kr32::cxx::Options::threads), bool>);
static_assert(std::is_same_v<decltype(kr32::cxx::Result::ok), bool>);
static_assert(std::is_same_v<decltype(kr32::cxx::Result::assembly),
                             std::string>);
static_assert(std::is_same_v<decltype(kr32::cxx::Result::object),
                             std::vector<std::uint8_t>>);
static_assert(std::is_same_v<decltype(kr32::cxx::Result::diagnostics),
                             std::vector<kr32::cxx::Diagnostic>>);
static_assert(std::is_same_v<decltype(kr32::cxx::Diagnostic::line), int>);
static_assert(std::is_same_v<decltype(kr32::cxx::Diagnostic::column), int>);
static_assert(std::is_same_v<decltype(kr32::cxx::Diagnostic::message),
                             std::string>);

TEST_CASE("default Options match the documented API") {
    const kr32::cxx::Options options;
    CHECK(options.emit == kr32::cxx::Emit::Assembly);
    CHECK(options.source_name == "<input>");
    CHECK(options.exceptions);
    CHECK(options.threads);
}

TEST_CASE("default Result is an empty failure") {
    const kr32::cxx::Result result;
    CHECK_FALSE(result.ok);
    CHECK(result.assembly.empty());
    CHECK(result.object.empty());
    CHECK(result.diagnostics.empty());
}

TEST_CASE("default Diagnostic is an empty location") {
    const kr32::cxx::Diagnostic diagnostic;
    CHECK(diagnostic.line == 0);
    CHECK(diagnostic.column == 0);
    CHECK(diagnostic.message.empty());
}

TEST_CASE("compile is callable and a failure explains itself") {
    const kr32::cxx::Result result =
        kr32::cxx::compile("int main() { return 0; }");
    // Whichever implementation is linked in, a rejected translation unit must
    // carry at least one diagnostic. A successful compile is allowed now and
    // after the front end lands.
    CHECK((result.ok || !result.diagnostics.empty()));
}

TEST_CASE("object emission returns a readable relocatable image") {
    kr32::cxx::Options options;
    options.emit = kr32::cxx::Emit::Object;
    const kr32::cxx::Result result =
        kr32::cxx::compile("int answer() { return 42; }", options);
    REQUIRE(result.ok);
    REQUIRE_FALSE(result.object.empty());
    CHECK(result.assembly.empty());
    kr32::container::Image image;
    kr32::container::Error error;
    REQUIRE_MESSAGE(kr32::container::read(result.object, image, error),
                    error.message);
    CHECK(image.type == kr32::container::ImageType::Object);
}

TEST_CASE("API smoke test sees the frozen ABI constants") {
    CHECK(abi::kAbiVersion == 1);
    CHECK(abi::kPointerSize == 4);
    CHECK(abi::kDoubleAlign == 8);
    CHECK(abi::kPublicCallAlignment == 16);
    CHECK(abi::kThreadPointerRegister == 11);
    CHECK(abi::kTileBytes == 64);
}
