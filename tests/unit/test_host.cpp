#include "doctest.h"

#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/container/container.h"
#include "kr32/core/machine.h"
#include "kr32/link/linker.h"
#include "kr32/vm/host.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

using namespace kr32;

namespace {

struct RunResult {
    std::uint32_t result = 0;
    bool halted = false;
    std::string console;
    std::string host_error;
};

container::Image compile_image(std::string_view source) {
    const cc::Result compiled = cc::compile(source, "host-test.c");
    const std::string diagnostic =
        compiled.diagnostics.empty() ? "compile failed" : compiled.diagnostics.front().message;
    INFO(diagnostic);
    REQUIRE(compiled.ok);
    container::Image user;
    assembler::Error assembly_error;
    REQUIRE_MESSAGE(assembler::assemble_object(compiled.assembly, user, assembly_error),
                    assembly_error.message.c_str());
    container::Image runtime;
    REQUIRE_MESSAGE(assembler::assemble_object(cc::runtime_assembly(), runtime, assembly_error),
                    assembly_error.message.c_str());
    container::Image image;
    linker::Map map;
    linker::Error link_error;
    REQUIRE_MESSAGE(linker::link({user, runtime}, linker::Options{}, image, map, link_error),
                    link_error.message.c_str());
    return image;
}

RunResult run_image(const container::Image& image,
                    const vm::HostOptions& host_options,
                    std::string& error) {
    RunResult result;
    core::Machine machine(core::Machine::Options{core::kDefaultRamSize});
    for (const container::Section& section : image.sections) {
        if ((section.flags & container::Alloc) == 0 ||
            section.type == container::SectionType::Bss) {
            continue;
        }
        REQUIRE(machine.load_image(section.address, section.data));
    }
    machine.reset(image.entry);
    vm::Host host(host_options);
    host.install(machine);
    for (int step = 0; step < 100000; ++step) {
        const core::StepResult step_result = machine.step();
        if (step_result.kind == core::StepResult::Kind::Halted) {
            result.halted = true;
            result.result = machine.exit_code_set() ? machine.exit_code() : machine.reg(0);
            result.console = host.console();
            result.host_error = host.last_error();
            return result;
        }
        if (step_result.kind == core::StepResult::Kind::Raised) {
            error = "machine raised a fault";
            result.console = host.console();
            return result;
        }
    }
    error = "step limit";
    result.console = host.console();
    return result;
}

std::filesystem::path temporary_sandbox() {
    const std::filesystem::path path =
        std::filesystem::current_path() / "host-test-sandbox";
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path, error);
    return path;
}

} // namespace

TEST_CASE("Host file I/O stays inside the sandbox") {
    const container::Image image = compile_image(
        "int open(const char *path, int flags);\n"
        "int close(int fd);\n"
        "int read(int fd, void *buffer, int count);\n"
        "int write(int fd, const void *buffer, int count);\n"
        "int main(void) {\n"
        "    int fd = open(\"data.txt\", 1 | 4 | 8);\n"
        "    if (fd < 0) return 1;\n"
        "    if (write(fd, \"abc\", 3) != 3) return 2;\n"
        "    close(fd);\n"
        "    fd = open(\"data.txt\", 0);\n"
        "    if (fd < 0) return 3;\n"
        "    char buffer[4];\n"
        "    int count = read(fd, buffer, 4);\n"
        "    close(fd);\n"
        "    return count;\n"
        "}\n");

    const std::filesystem::path sandbox = temporary_sandbox();
    vm::HostOptions options;
    options.fs_root = sandbox.string();
    options.echo_console = false;
    std::string error;
    const RunResult result = run_image(image, options, error);
    INFO(result.host_error);
    CHECK(result.halted);
    CHECK(result.result == 3);
    std::ifstream data(sandbox / "data.txt", std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(data)), std::istreambuf_iterator<char>());
    CHECK(content == "abc");
    std::error_code cleanup;
    std::filesystem::remove_all(sandbox, cleanup);
}

TEST_CASE("Host handles the remaining guest syscall vectors") {
    const container::Image image = compile_image(
        "int getchar(void);\n"
        "int sys_random(void);\n"
        "int sys_time(void);\n"
        "int open(const char *path, int flags);\n"
        "int close(int fd);\n"
        "int read(int fd, void *buffer, int count);\n"
        "int write(int fd, const void *buffer, int count);\n"
        "int seek(int fd, int offset, int whence);\n"
        "int main(void) {\n"
        "    if (getchar() != 'Q') return 1;\n"
        "    if (sys_random() == 0) return 2;\n"
        "    if (sys_time() == 0) return 3;\n"
        "    int fd = open(\"seek.txt\", 2 | 4 | 8);\n"
        "    if (fd < 0) return 4;\n"
        "    if (write(fd, \"abcdef\", 6) != 6) return 5;\n"
        "    if (seek(fd, 2, 0) != 2) return 6;\n"
        "    char buffer[2];\n"
        "    if (read(fd, buffer, 1) != 1) return 7;\n"
        "    close(fd);\n"
        "    return buffer[0] == 'c' ? 0 : 8;\n"
        "}\n");
    const std::filesystem::path sandbox = temporary_sandbox();
    vm::HostOptions options;
    options.fs_root = sandbox.string();
    options.echo_console = false;
    options.input = "Q";
    std::string error;
    const RunResult result = run_image(image, options, error);
    INFO(result.host_error);
    CHECK(result.halted);
    CHECK(result.result == 0);
    std::error_code cleanup;
    std::filesystem::remove_all(sandbox, cleanup);
}

TEST_CASE("Host file dialogs return an authorized guest path") {
    const container::Image image = compile_image(
        "int sys_open_dialog(char *path, int capacity);\n"
        "int open(const char *path, int flags);\n"
        "int close(int fd);\n"
        "int read(int fd, void *buffer, int count);\n"
        "int main(void) {\n"
        "    char path[512];\n"
        "    int length = sys_open_dialog(path, 512);\n"
        "    if (length <= 0) return 1;\n"
        "    int fd = open(path, 0);\n"
        "    if (fd < 0) return 2;\n"
        "    char value = 0;\n"
        "    if (read(fd, &value, 1) != 1) return 3;\n"
        "    close(fd);\n"
        "    return value == 'Z' ? 0 : 4;\n"
        "}\n");

    const std::filesystem::path sandbox = temporary_sandbox();
    const std::filesystem::path selected =
        std::filesystem::current_path() / "host-dialog-selected.txt";
    {
        std::ofstream output(selected, std::ios::binary);
        output << 'Z';
    }
    vm::HostOptions options;
    options.fs_root = sandbox.string();
    options.echo_console = false;
    options.file_dialog = [&](const FileDialogOptions& request) {
        CHECK(request.kind == FileDialogKind::Open);
        CHECK(request.must_exist);
        return FileDialogResult{FileDialogStatus::Selected, selected.string(), {}};
    };
    std::string error;
    const RunResult result = run_image(image, options, error);
    INFO(result.host_error);
    CHECK(result.halted);
    CHECK(result.result == 0);
    std::error_code cleanup;
    std::filesystem::remove(selected, cleanup);
    std::filesystem::remove_all(sandbox, cleanup);
}

TEST_CASE("Host file dialog reports buffer capacity and save mode") {
    const container::Image image = compile_image(
        "int sys_file_dialog(int mode, int flags, char *path, int capacity);\n"
        "int sys_save_dialog(char *path, int capacity);\n"
        "int main(void) {\n"
        "    char small[4];\n"
        "    if (sys_file_dialog(1, 2, small, 4) != -28) return 1;\n"
        "    char path[64];\n"
        "    return sys_save_dialog(path, 64) > 0 ? 0 : 2;\n"
        "}\n");
    vm::HostOptions options;
    options.echo_console = false;
    options.file_dialog = [](const FileDialogOptions& request) {
        CHECK(request.kind == FileDialogKind::Save);
        CHECK(request.overwrite_prompt);
        return FileDialogResult{FileDialogStatus::Selected,
                                "/tmp/a-very-long-selected-file-name.k32", {}};
    };
    std::string error;
    const RunResult result = run_image(image, options, error);
    CHECK(result.halted);
    CHECK(result.result == 0);
}

TEST_CASE("Host file dialog cancellation and unavailable status are distinct") {
    const container::Image canceled_image = compile_image(
        "int sys_open_dialog(char *path, int capacity);\n"
        "int main(void) { char path[32]; return sys_open_dialog(path, 32) == -1 ? 0 : 1; }\n");
    vm::HostOptions canceled_options;
    canceled_options.echo_console = false;
    canceled_options.file_dialog = [](const FileDialogOptions&) {
        return FileDialogResult{FileDialogStatus::Canceled, {}, {}};
    };
    std::string error;
    const RunResult canceled = run_image(canceled_image, canceled_options, error);
    CHECK(canceled.halted);
    CHECK(canceled.result == 0);

    const container::Image unavailable_image = compile_image(
        "int sys_open_dialog(char *path, int capacity);\n"
        "int main(void) { char path[32]; return sys_open_dialog(path, 32) == -2 ? 0 : 1; }\n");
    vm::HostOptions unavailable_options;
    unavailable_options.echo_console = false;
    const RunResult unavailable =
        run_image(unavailable_image, unavailable_options, error);
    CHECK(unavailable.halted);
    CHECK(unavailable.result == 0);
}

TEST_CASE("Host FILE errors set errno and perror") {
    const container::Image image = compile_image(
        "#include <errno.h>\n"
        "#include <stdio.h>\n"
        "int main(void) {\n"
        "    errno = 0;\n"
        "    FILE *stream = fopen(\"missing-file.txt\", \"r\");\n"
        "    if (stream != 0) return 1;\n"
        "    if (errno == 0) return 2;\n"
        "    if (strerror(errno) == 0) return 3;\n"
        "    perror(\"open failed\");\n"
        "    return 0;\n"
        "}\n");
    const std::filesystem::path sandbox = temporary_sandbox();
    vm::HostOptions options;
    options.fs_root = sandbox.string();
    options.echo_console = false;
    std::string error;
    const RunResult result = run_image(image, options, error);
    CHECK(result.halted);
    CHECK(result.result == 0);
    CHECK(result.console.find("open failed: ") == 0);
    std::error_code cleanup;
    std::filesystem::remove_all(sandbox, cleanup);
}

TEST_CASE("Host supports C FILE streams") {
    const container::Image image = compile_image(
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        "int main(void) {\n"
        "    FILE *stream = fopen(\"stdio.txt\", \"w\");\n"
        "    if (stream == 0) return 1;\n"
        "    if (fprintf(stream, \"%s %d %.2f\\n\", \"value\", 42, 3.5) < 0) return 2;\n"
        "    if (fclose(stream) != 0) return 3;\n"
        "    stream = fopen(\"stdio.txt\", \"r\");\n"
        "    if (stream == 0) return 4;\n"
        "    char word[16];\n"
        "    int number = 0;\n"
        "    double real = 0.0;\n"
        "    if (fscanf(stream, \"%s %d %lf\", word, &number, &real) != 3) return 5;\n"
        "    if (fclose(stream) != 0) return 6;\n"
        "    char buffer[32];\n"
        "    if (snprintf(buffer, 32, \"%s %d %.2f\", word, number, real) != 13) return 7;\n"
        "    return strcmp(buffer, \"value 42 3.50\");\n"
        "}\n");
    const std::filesystem::path sandbox = temporary_sandbox();
    vm::HostOptions options;
    options.fs_root = sandbox.string();
    options.echo_console = false;
    std::string error;
    const RunResult result = run_image(image, options, error);
    CHECK(result.halted);
    CHECK(result.result == 0);
    std::error_code cleanup;
    std::filesystem::remove_all(sandbox, cleanup);
}

TEST_CASE("Host supports sscanf, scansets, and getline") {
    const container::Image image = compile_image(
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "int main(void) {\n"
        "    char word[16];\n"
        "    int number = 0;\n"
        "    double real = 0.0;\n"
        "    if (sscanf(\"value 42 3.5\", \"%s %d %lf\", word, &number, &real) != 3) return 1;\n"
        "    char letters[16];\n"
        "    int digits = 0;\n"
        "    if (sscanf(\"abc123\", \"%[a-z]%d\", letters, &digits) != 2) return 2;\n"
        "    FILE *stream = fopen(\"lines.txt\", \"w\");\n"
        "    if (stream == 0) return 3;\n"
        "    fputs(\"one\\ntwo\\n\", stream);\n"
        "    fclose(stream);\n"
        "    stream = fopen(\"lines.txt\", \"r\");\n"
        "    if (stream == 0) return 4;\n"
        "    char *line = 0;\n"
        "    int capacity = 0;\n"
        "    int length = getline(&line, &capacity, stream);\n"
        "    if (length != 4) return 5;\n"
        "    free(line);\n"
        "    fclose(stream);\n"
        "    return 0;\n"
        "}\n");
    const std::filesystem::path sandbox = temporary_sandbox();
    vm::HostOptions options;
    options.fs_root = sandbox.string();
    options.echo_console = false;
    std::string error;
    const RunResult result = run_image(image, options, error);
    CHECK(result.halted);
    CHECK(result.result == 0);
    std::error_code cleanup;
    std::filesystem::remove_all(sandbox, cleanup);
}

TEST_CASE("Host exposes stat, fstat, rename, and remove") {
    const container::Image image = compile_image(
        "#include <stdio.h>\n"
        "#include <sys/stat.h>\n"
        "int main(void) {\n"
        "    FILE *stream = fopen(\"stat-test.txt\", \"w\");\n"
        "    if (stream == 0) return 1;\n"
        "    fputs(\"hello\", stream);\n"
        "    fclose(stream);\n"
        "    struct stat info;\n"
        "    if (stat(\"stat-test.txt\", &info) != 0) return 2;\n"
        "    if (info.st_size != 5) return 3;\n"
        "    stream = fopen(\"stat-test.txt\", \"r\");\n"
        "    if (stream == 0) return 4;\n"
        "    if (fstat(fileno(stream), &info) != 0) return 5;\n"
        "    if (info.st_size != 5) return 6;\n"
        "    fclose(stream);\n"
        "    if (rename(\"stat-test.txt\", \"renamed.txt\") != 0) return 7;\n"
        "    if (stat(\"stat-test.txt\", &info) == 0) return 8;\n"
        "    if (remove(\"renamed.txt\") != 0) return 9;\n"
        "    return 0;\n"
        "}\n");
    const std::filesystem::path sandbox = temporary_sandbox();
    vm::HostOptions options;
    options.fs_root = sandbox.string();
    options.echo_console = false;
    std::string error;
    const RunResult result = run_image(image, options, error);
    CHECK(result.halted);
    CHECK(result.result == 0);
    std::error_code cleanup;
    std::filesystem::remove_all(sandbox, cleanup);
}

TEST_CASE("Host supports freopen and file positions") {
    const container::Image image = compile_image(
        "#include <stdio.h>\n"
        "int main(void) {\n"
        "    FILE *stream = fopen(\"positions.txt\", \"w+\");\n"
        "    if (stream == 0) return 1;\n"
        "    fputs(\"abcdef\", stream);\n"
        "    fpos_t position = 0;\n"
        "    if (fgetpos(stream, &position) != 0) return 2;\n"
        "    if (position != 6) return 3;\n"
        "    position = 2;\n"
        "    if (fsetpos(stream, &position) != 0) return 4;\n"
        "    char value = 0;\n"
        "    if (fread(&value, 1, 1, stream) != 1 || value != 'c') return 5;\n"
        "    stream = freopen(\"other.txt\", \"w\", stream);\n"
        "    if (stream == 0) return 6;\n"
        "    fputs(\"new\", stream);\n"
        "    fclose(stream);\n"
        "    return 0;\n"
        "}\n");
    const std::filesystem::path sandbox = temporary_sandbox();
    vm::HostOptions options;
    options.fs_root = sandbox.string();
    options.echo_console = false;
    std::string error;
    const RunResult result = run_image(image, options, error);
    CHECK(result.halted);
    CHECK(result.result == 0);
    std::error_code cleanup;
    std::filesystem::remove_all(sandbox, cleanup);
}

TEST_CASE("Host buffers FILE output and flushes on close") {
    const container::Image image = compile_image(
        "#include <stdio.h>\n"
        "int main(void) {\n"
        "    FILE *stream = fopen(\"buffered.txt\", \"w\");\n"
        "    if (stream == 0) return 1;\n"
        "    if (setvbuf(stream, 0, _IOFBF, 4) != 0) return 2;\n"
        "    fputs(\"abcdef\", stream);\n"
        "    if (ftell(stream) != 6) return 3;\n"
        "    fputs(\"ghi\", stream);\n"
        "    if (fflush(stream) != 0) return 4;\n"
        "    fclose(stream);\n"
        "    return 0;\n"
        "}\n");
    const std::filesystem::path sandbox = temporary_sandbox();
    vm::HostOptions options;
    options.fs_root = sandbox.string();
    options.echo_console = false;
    std::string error;
    const RunResult result = run_image(image, options, error);
    CHECK(result.halted);
    CHECK(result.result == 0);
    std::ifstream input(sandbox / "buffered.txt", std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    CHECK(content == "abcdefghi");
    std::error_code cleanup;
    std::filesystem::remove_all(sandbox, cleanup);
}

TEST_CASE("Host rejects sandbox escapes and read-only writes") {
    const container::Image escape = compile_image(
        "int open(const char *path, int flags);\n"
        "int main(void) {\n"
        "    if (open(\"../escape.txt\", 1 | 4) >= 0) return 9;\n"
        "    return 0;\n"
        "}\n");
    const std::filesystem::path sandbox = temporary_sandbox();
    vm::HostOptions options;
    options.fs_root = sandbox.string();
    options.echo_console = false;
    std::string error;
    const RunResult escaped = run_image(escape, options, error);
    CHECK(escaped.halted);
    CHECK(escaped.result == 0);

    const container::Image write_attempt = compile_image(
        "int open(const char *path, int flags);\n"
        "int main(void) {\n"
        "    if (open(\"data.txt\", 1 | 4 | 8) < 0) return 0;\n"
        "    return 9;\n"
        "}\n");
    options.read_only = true;
    const RunResult read_only = run_image(write_attempt, options, error);
    CHECK(read_only.halted);
    CHECK(read_only.result == 0);
    std::error_code cleanup;
    std::filesystem::remove_all(sandbox, cleanup);
}

TEST_CASE("Host console and arguments are visible to the guest") {
    const container::Image image = compile_image(
        "int putchar(int c);\n"
        "int sys_argc(void);\n"
        "int sys_arg(int index, char *buffer, int size);\n"
        "int main(void) {\n"
        "    char buffer[32];\n"
        "    int count = sys_argc();\n"
        "    if (count != 2) return 1;\n"
        "    sys_arg(1, buffer, 32);\n"
        "    putchar(buffer[0]);\n"
        "    putchar('\\n');\n"
        "    return 0;\n"
        "}\n");
    vm::HostOptions options;
    options.echo_console = false;
    options.args = {"first", "hello"};
    std::string error;
    const RunResult result = run_image(image, options, error);
    CHECK(result.halted);
    CHECK(result.result == 0);
    CHECK(result.console == "h\n");
}
