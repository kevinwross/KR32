#include "kr32/dap/dap.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "kr32/container/container.h"
#include "kr32/debug/debug.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace kr32::dap {
namespace {

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    std::int64_t number = 0;
    std::string text;
    std::vector<Json> array;
    std::map<std::string, Json> object;

    static Json boolean_value(bool value) {
        Json json;
        json.type = Type::Bool;
        json.boolean = value;
        return json;
    }
    static Json number_value(std::int64_t value) {
        Json json;
        json.type = Type::Number;
        json.number = value;
        return json;
    }
    static Json string_value(std::string value) {
        Json json;
        json.type = Type::String;
        json.text = std::move(value);
        return json;
    }
    static Json object_value() {
        Json json;
        json.type = Type::Object;
        return json;
    }
    static Json array_value() {
        Json json;
        json.type = Type::Array;
        return json;
    }

    Json& set(const std::string& key, Json value) {
        object[key] = std::move(value);
        return *this;
    }
    [[nodiscard]] const Json* get(const std::string& key) const {
        const auto found = object.find(key);
        return found == object.end() ? nullptr : &found->second;
    }
    [[nodiscard]] std::string as_string() const {
        return type == Type::String ? text : std::string{};
    }
    [[nodiscard]] std::int64_t as_number() const {
        return type == Type::Number ? number : 0;
    }
    [[nodiscard]] bool as_bool() const {
        return type == Type::Bool && boolean;
    }
};

void append_escaped(std::string& output, const std::string& value) {
    output.push_back('"');
    for (char c : value) {
        switch (c) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8] = {};
                std::snprintf(buffer, sizeof(buffer), "\\u%04X",
                              static_cast<unsigned>(static_cast<unsigned char>(c)));
                output += buffer;
            } else {
                output.push_back(c);
            }
            break;
        }
    }
    output.push_back('"');
}

std::string serialize(const Json& json) {
    std::string output;
    switch (json.type) {
    case Json::Type::Null: output = "null"; break;
    case Json::Type::Bool: output = json.boolean ? "true" : "false"; break;
    case Json::Type::Number: output = std::to_string(json.number); break;
    case Json::Type::String: append_escaped(output, json.text); break;
    case Json::Type::Array:
        output.push_back('[');
        for (std::size_t index = 0; index < json.array.size(); ++index) {
            if (index != 0) output.push_back(',');
            output += serialize(json.array[index]);
        }
        output.push_back(']');
        break;
    case Json::Type::Object:
        output.push_back('{');
        {
            bool first = true;
            for (const auto& [key, value] : json.object) {
                if (!first) output.push_back(',');
                first = false;
                append_escaped(output, key);
                output.push_back(':');
                output += serialize(value);
            }
        }
        output.push_back('}');
        break;
    }
    return output;
}

bool parse_string(const std::string& input, std::size_t& position, std::string& output) {
    if (position >= input.size() || input[position] != '"') {
        return false;
    }
    ++position;
    while (position < input.size()) {
        const char c = input[position++];
        if (c == '"') {
            return true;
        }
        if (c != '\\') {
            output.push_back(c);
            continue;
        }
        if (position >= input.size()) {
            return false;
        }
        const char escaped = input[position++];
        switch (escaped) {
        case '"': output.push_back('"'); break;
        case '\\': output.push_back('\\'); break;
        case '/': output.push_back('/'); break;
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        case 'b': output.push_back('\b'); break;
        case 'f': output.push_back('\f'); break;
        case 'u': {
            if (position + 4 > input.size()) return false;
            const std::string code = input.substr(position, 4);
            position += 4;
            const long value = std::strtol(code.c_str(), nullptr, 16);
            output.push_back(value < 0x80 ? static_cast<char>(value) : '?');
            break;
        }
        default: return false;
        }
    }
    return false;
}

bool parse_value(const std::string& input, std::size_t& position, Json& output);

bool parse_object(const std::string& input, std::size_t& position, Json& output) {
    if (position >= input.size() || input[position] != '{') return false;
    ++position;
    output = Json::object_value();
    while (true) {
        while (position < input.size() &&
               std::isspace(static_cast<unsigned char>(input[position])) != 0) {
            ++position;
        }
        if (position < input.size() && input[position] == '}') {
            ++position;
            return true;
        }
        std::string key;
        if (!parse_string(input, position, key)) return false;
        while (position < input.size() &&
               std::isspace(static_cast<unsigned char>(input[position])) != 0) {
            ++position;
        }
        if (position >= input.size() || input[position] != ':') return false;
        ++position;
        Json value;
        if (!parse_value(input, position, value)) return false;
        output.object[key] = std::move(value);
        while (position < input.size() &&
               std::isspace(static_cast<unsigned char>(input[position])) != 0) {
            ++position;
        }
        if (position < input.size() && input[position] == ',') {
            ++position;
            continue;
        }
        if (position < input.size() && input[position] == '}') {
            ++position;
            return true;
        }
        return false;
    }
}

bool parse_value(const std::string& input, std::size_t& position, Json& output) {
    while (position < input.size() &&
           std::isspace(static_cast<unsigned char>(input[position])) != 0) {
        ++position;
    }
    if (position >= input.size()) return false;
    const char c = input[position];
    if (c == '{') return parse_object(input, position, output);
    if (c == '[') {
        ++position;
        output = Json::array_value();
        while (true) {
            while (position < input.size() &&
                   std::isspace(static_cast<unsigned char>(input[position])) != 0) {
                ++position;
            }
            if (position < input.size() && input[position] == ']') {
                ++position;
                return true;
            }
            Json value;
            if (!parse_value(input, position, value)) return false;
            output.array.push_back(std::move(value));
            while (position < input.size() &&
                   std::isspace(static_cast<unsigned char>(input[position])) != 0) {
                ++position;
            }
            if (position < input.size() && input[position] == ',') {
                ++position;
                continue;
            }
            if (position < input.size() && input[position] == ']') {
                ++position;
                return true;
            }
            return false;
        }
    }
    if (c == '"') {
        output.type = Json::Type::String;
        return parse_string(input, position, output.text);
    }
    if (input.compare(position, 4, "true") == 0) {
        position += 4;
        output = Json::boolean_value(true);
        return true;
    }
    if (input.compare(position, 5, "false") == 0) {
        position += 5;
        output = Json::boolean_value(false);
        return true;
    }
    if (input.compare(position, 4, "null") == 0) {
        position += 4;
        output = Json{};
        return true;
    }
    std::size_t end = position;
    if (end < input.size() && input[end] == '-') ++end;
    while (end < input.size() && std::isdigit(static_cast<unsigned char>(input[end])) != 0) {
        ++end;
    }
    if (end == position) return false;
    output = Json::number_value(std::strtoll(input.substr(position, end - position).c_str(),
                                             nullptr, 10));
    position = end;
    return true;
}

bool parse_json(const std::string& text, Json& output) {
    std::size_t position = 0;
    if (!parse_value(text, position, output)) {
        return false;
    }
    while (position < text.size() &&
           std::isspace(static_cast<unsigned char>(text[position])) != 0) {
        ++position;
    }
    return position == text.size();
}

std::string base64(const std::vector<std::uint8_t>& bytes) {
    static const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const std::uint32_t a = bytes[index];
        const std::uint32_t b = index + 1 < bytes.size() ? bytes[index + 1] : 0;
        const std::uint32_t c = index + 2 < bytes.size() ? bytes[index + 2] : 0;
        const std::uint32_t value = (a << 16) | (b << 8) | c;
        output.push_back(alphabet[(value >> 18) & 63]);
        output.push_back(alphabet[(value >> 12) & 63]);
        output.push_back(index + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=');
        output.push_back(index + 2 < bytes.size() ? alphabet[value & 63] : '=');
    }
    return output;
}

class Channel {
public:
    virtual ~Channel() = default;
    virtual bool read(Json& message, std::string& error) = 0;
    virtual bool write(const Json& message, std::string& error) = 0;
};

class StdioChannel : public Channel {
public:
    bool read(Json& message, std::string& error) override {
        std::string line;
        std::size_t content_length = 0;
        bool found = false;
        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) {
                found = true;
                break;
            }
            if (line.rfind("Content-Length:", 0) == 0) {
                content_length = static_cast<std::size_t>(
                    std::strtoul(line.c_str() + 15, nullptr, 10));
            }
        }
        if (!found) return false;
        std::string payload(content_length, '\0');
        if (content_length != 0) {
            std::cin.read(payload.data(), static_cast<std::streamsize>(content_length));
            if (static_cast<std::size_t>(std::cin.gcount()) != content_length) return false;
        }
        if (!parse_json(payload, message)) {
            error = "invalid JSON";
            return false;
        }
        return true;
    }

    bool write(const Json& message, std::string& error) override {
        const std::string payload = serialize(message);
        std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
        std::cout.flush();
        if (!std::cout) {
            error = "stdout write failed";
            return false;
        }
        return true;
    }
};

class TcpChannel : public Channel {
public:
    ~TcpChannel() override { close_channel(); }

    bool listen(int port, std::string& error) {
#if defined(_WIN32)
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            error = "WSAStartup failed";
            return false;
        }
#endif
        socket_ = static_cast<std::intptr_t>(::socket(AF_INET, SOCK_STREAM, 0));
        if (socket_ < 0) {
            error = "socket failed";
            return false;
        }
        int reuse = 1;
        ::setsockopt(static_cast<int>(socket_), SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<unsigned short>(port));
        if (::bind(static_cast<int>(socket_), reinterpret_cast<sockaddr*>(&address),
                   sizeof(address)) != 0) {
            error = "bind failed";
            close_channel();
            return false;
        }
        if (::listen(static_cast<int>(socket_), 1) != 0) {
            error = "listen failed";
            close_channel();
            return false;
        }
        client_ = static_cast<std::intptr_t>(::accept(static_cast<int>(socket_), nullptr, nullptr));
        if (client_ < 0) {
            error = "accept failed";
            close_channel();
            return false;
        }
        return true;
    }

    bool read(Json& message, std::string& error) override {
        std::string header;
        while (true) {
            char c = 0;
            if (::recv(static_cast<int>(client_), &c, 1, 0) != 1) return false;
            header.push_back(c);
            if (header.size() >= 4 && header.compare(header.size() - 4, 4, "\r\n\r\n") == 0) {
                break;
            }
        }
        std::size_t content_length = 0;
        const std::size_t content = header.find("Content-Length:");
        if (content != std::string::npos) {
            content_length = static_cast<std::size_t>(
                std::strtoul(header.c_str() + content + 15, nullptr, 10));
        }
        std::string payload(content_length, '\0');
        std::size_t received = 0;
        while (received < content_length) {
            const int count = ::recv(static_cast<int>(client_), payload.data() + received,
                                     static_cast<int>(content_length - received), 0);
            if (count <= 0) return false;
            received += static_cast<std::size_t>(count);
        }
        if (!parse_json(payload, message)) {
            error = "invalid JSON";
            return false;
        }
        return true;
    }

    bool write(const Json& message, std::string& error) override {
        const std::string payload = serialize(message);
        const std::string frame =
            "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n" + payload;
        std::size_t sent = 0;
        while (sent < frame.size()) {
            const int count = ::send(static_cast<int>(client_), frame.data() + sent,
                                     static_cast<int>(frame.size() - sent), 0);
            if (count <= 0) {
                error = "socket write failed";
                return false;
            }
            sent += static_cast<std::size_t>(count);
        }
        return true;
    }

private:
    void close_channel() {
        if (client_ >= 0) {
#if defined(_WIN32)
            ::closesocket(static_cast<SOCKET>(client_));
#else
            ::close(static_cast<int>(client_));
#endif
            client_ = -1;
        }
        if (socket_ >= 0) {
#if defined(_WIN32)
            ::closesocket(static_cast<SOCKET>(socket_));
            WSACleanup();
#else
            ::close(static_cast<int>(socket_));
#endif
            socket_ = -1;
        }
    }

    std::intptr_t socket_ = -1;
    std::intptr_t client_ = -1;
};

class Server {
public:
    Server(Channel& channel, std::string image_path)
        : channel_(channel), image_path_(std::move(image_path)) {}

    void run() {
        Json request;
        std::string error;
        while (!quit_ && channel_.read(request, error)) {
            handle(request);
        }
    }

private:
    void send(Json message) {
        (void)channel_.write(message, error_);
    }

    Json response(const Json& request, bool success, Json body = Json::object_value()) {
        Json result = Json::object_value();
        result.set("seq", Json::number_value(seq_++));
        result.set("type", Json::string_value("response"));
        result.set("request_seq", Json::number_value(
            request.get("seq") != nullptr ? request.get("seq")->as_number() : 0));
        result.set("success", Json::boolean_value(success));
        if (const Json* command = request.get("command")) {
            result.set("command", *command);
        }
        if (success) {
            result.set("body", std::move(body));
        } else if (!error_.empty()) {
            result.set("message", Json::string_value(error_));
        }
        return result;
    }

    void event(const std::string& name, Json body = Json::object_value()) {
        Json message = Json::object_value();
        message.set("seq", Json::number_value(seq_++));
        message.set("type", Json::string_value("event"));
        message.set("event", Json::string_value(name));
        message.set("body", std::move(body));
        send(std::move(message));
    }

    void stopped(const std::string& reason) {
        Json body = Json::object_value();
        body.set("reason", Json::string_value(reason));
        body.set("threadId", Json::number_value(1));
        body.set("allThreadsStopped", Json::boolean_value(true));
        event("stopped", std::move(body));
    }

    bool read_image(const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            error_ = "cannot open " + path;
            return false;
        }
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                              std::istreambuf_iterator<char>());
        container::Error container_error;
        if (!container::read(bytes, image_, container_error)) {
            error_ = container_error.message;
            return false;
        }
        return true;
    }

    void prepare_stop() {
        const debug::Function* main_function = nullptr;
        for (const debug::Function& function : session_.info().functions) {
            if (function.name == "main") {
                main_function = &function;
                break;
            }
        }
        if (main_function == nullptr) return;
        std::uint32_t target = 0;
        bool found = false;
        for (const debug::Line& line : session_.info().lines) {
            if (line.address >= main_function->start && line.address < main_function->end) {
                target = line.address;
                found = true;
                break;
            }
        }
        if (found) {
            session_.set_breakpoint(target);
            (void)session_.run(10000);
            session_.clear_breakpoint(target);
        } else {
            session_.machine().set_pc(main_function->start);
        }
    }

    void build_frames() {
        frames_.clear();
        std::vector<debug::Frame> stack = session_.call_stack();
        if (stack.empty()) {
            debug::Frame frame;
            frame.function = "entry";
            frame.pc = session_.machine().pc();
            stack.push_back(frame);
        }
        for (const debug::Frame& frame : stack) {
            FrameInfo info;
            info.frame = frame;
            info.id = next_frame_id_++;
            frames_.push_back(info);
        }
    }

    void handle(const Json& request) {
        const Json* command_value = request.get("command");
        if (command_value == nullptr) return;
        const std::string command = command_value->as_string();
        const Json* arguments = request.get("arguments");
        if (command == "initialize") {
            Json body = Json::object_value();
            body.set("supportsConfigurationDoneRequest", Json::boolean_value(true));
            body.set("supportsEvaluateForHovers", Json::boolean_value(true));
            body.set("supportsReadMemoryRequest", Json::boolean_value(true));
            send(response(request, true, std::move(body)));
            event("initialized");
        } else if (command == "launch") {
            std::string path = image_path_;
            if (arguments != nullptr) {
                if (const Json* program = arguments->get("program")) path = program->as_string();
                if (path.empty()) {
                    if (const Json* image = arguments->get("image")) path = image->as_string();
                }
            }
            if (!read_image(path) || !session_.load(image_, error_)) {
                send(response(request, false));
                return;
            }
            prepare_stop();
            send(response(request, true));
        } else if (command == "setBreakpoints") {
            session_.clear_breakpoints();
            Json breakpoints = Json::array_value();
            if (arguments != nullptr) {
                if (const Json* requested = arguments->get("breakpoints")) {
                    for (const Json& breakpoint : requested->array) {
                        const int line = static_cast<int>(
                            breakpoint.get("line") != nullptr
                                ? breakpoint.get("line")->as_number()
                                : 0);
                        bool verified = false;
                        for (const debug::Line& candidate : session_.info().lines) {
                            if (candidate.line == line) {
                                session_.set_breakpoint(candidate.address);
                                verified = true;
                                break;
                            }
                        }
                        Json result = Json::object_value();
                        result.set("verified", Json::boolean_value(verified));
                        result.set("line", Json::number_value(line));
                        breakpoints.array.push_back(std::move(result));
                    }
                }
            }
            Json body = Json::object_value();
            body.set("breakpoints", std::move(breakpoints));
            send(response(request, true, std::move(body)));
        } else if (command == "configurationDone") {
            send(response(request, true));
            stopped(session_.at_breakpoint() ? "breakpoint" : "entry");
        } else if (command == "threads") {
            Json threads = Json::array_value();
            Json thread = Json::object_value();
            thread.set("id", Json::number_value(1));
            thread.set("name", Json::string_value("main"));
            threads.array.push_back(std::move(thread));
            Json body = Json::object_value();
            body.set("threads", std::move(threads));
            send(response(request, true, std::move(body)));
        } else if (command == "stackTrace") {
            build_frames();
            Json frames = Json::array_value();
            for (const FrameInfo& info : frames_) {
                const debug::Line* line = session_.line_at(info.frame.pc);
                Json frame = Json::object_value();
                frame.set("id", Json::number_value(info.id));
                frame.set("name", Json::string_value(info.frame.function));
                frame.set("line", Json::number_value(line == nullptr ? 1 : line->line));
                frame.set("column", Json::number_value(line == nullptr ? 1 : line->column));
                if (line != nullptr) {
                    Json source = Json::object_value();
                    source.set("name", Json::string_value("source"));
                    for (const debug::Source& candidate : session_.info().sources) {
                        if (candidate.id == line->source) {
                            source.set("path", Json::string_value(candidate.path));
                        }
                    }
                    frame.set("source", std::move(source));
                }
                frame.set("instructionPointerReference",
                          Json::string_value(std::to_string(info.frame.pc)));
                frames.array.push_back(std::move(frame));
            }
            Json body = Json::object_value();
            body.set("stackFrames", std::move(frames));
            body.set("totalFrames", Json::number_value(static_cast<std::int64_t>(frames_.size())));
            send(response(request, true, std::move(body)));
        } else if (command == "scopes") {
            if (arguments != nullptr && arguments->get("frameId") != nullptr) {
                const std::int64_t id = arguments->get("frameId")->as_number();
                for (const FrameInfo& frame : frames_) {
                    if (frame.id == id) selected_ = frame;
                }
            }
            Json scopes = Json::array_value();
            Json locals = Json::object_value();
            locals.set("name", Json::string_value("Locals"));
            locals.set("variablesReference", Json::number_value(1));
            scopes.array.push_back(std::move(locals));
            Json globals = Json::object_value();
            globals.set("name", Json::string_value("Globals"));
            globals.set("variablesReference", Json::number_value(2));
            scopes.array.push_back(std::move(globals));
            Json body = Json::object_value();
            body.set("scopes", std::move(scopes));
            send(response(request, true, std::move(body)));
        } else if (command == "variables") {
            const std::int64_t reference =
                arguments != nullptr && arguments->get("variablesReference") != nullptr
                    ? arguments->get("variablesReference")->as_number()
                    : 1;
            Json variables = Json::array_value();
            if (reference == 1) {
                const debug::Frame& frame = selected_.id == 0 && !frames_.empty()
                                                ? frames_.front().frame
                                                : selected_.frame;
                for (const debug::Variable& variable : session_.info().variables) {
                    if (variable.function != frame.function) continue;
                    std::uint32_t value = 0;
                    (void)session_.read_memory(frame.frame_pointer + variable.offset,
                                               variable.size, value);
                    Json entry = Json::object_value();
                    entry.set("name", Json::string_value(variable.name));
                    entry.set("value", Json::string_value("0x" + hex(value)));
                    entry.set("type", Json::string_value(variable.type));
                    entry.set("variablesReference", Json::number_value(0));
                    variables.array.push_back(std::move(entry));
                }
            } else {
                for (const debug::Global& global : session_.info().globals) {
                    std::uint32_t value = 0;
                    (void)session_.read_global(global.name, value, error_);                    Json entry = Json::object_value();
                    entry.set("name", Json::string_value(global.name));
                    entry.set("value", Json::string_value("0x" + hex(value)));
                    entry.set("type", Json::string_value(global.type));
                    entry.set("variablesReference", Json::number_value(0));
                    variables.array.push_back(std::move(entry));
                }
            }
            Json body = Json::object_value();
            body.set("variables", std::move(variables));
            send(response(request, true, std::move(body)));
        } else if (command == "continue" || command == "next" || command == "stepIn" ||
                   command == "stepOut") {
            core::StepResult result;
            if (command == "continue") {
                result = session_.run(200000);
            } else if (command == "next") {
                result = session_.step_over();
            } else if (command == "stepIn") {
                result = session_.step();
            } else {
                result = session_.step_out();
            }
            send(response(request, true));
            if (result.kind == core::StepResult::Kind::Halted) {
                Json exited = Json::object_value();
                exited.set("exitCode", Json::number_value(
                    static_cast<std::int64_t>(session_.machine().exit_code())));
                event("exited", std::move(exited));
                event("terminated");
            } else {
                stopped(session_.at_breakpoint() ? "breakpoint" : "step");
            }
        } else if (command == "evaluate") {
            std::string expression;
            if (arguments != nullptr && arguments->get("expression") != nullptr) {
                expression = arguments->get("expression")->as_string();
            }
            std::uint32_t value = 0;
            bool ok = false;
            if (expression.rfind("$r", 0) == 0) {
                const int index = std::atoi(expression.c_str() + 2);
                if (index >= 0 && index < 16) {
                    value = session_.machine().reg(static_cast<std::size_t>(index));
                    ok = true;
                }
            } else if (expression == "$pc") {
                value = session_.machine().pc();
                ok = true;
            } else if (expression == "$sp") {
                value = session_.machine().sp();
                ok = true;
            } else {
                char* end = nullptr;
                const unsigned long parsed = std::strtoul(expression.c_str(), &end, 0);
                if (end != nullptr && *end == '\0') {
                    value = static_cast<std::uint32_t>(parsed);
                    ok = true;
                } else if (session_.read_variable(expression, value, error_)) {
                    ok = true;
                } else {
                    ok = session_.read_global(expression, value, error_);
                }
            }
            Json body = Json::object_value();
            body.set("result", Json::string_value(ok ? "0x" + hex(value) : "<unavailable>"));
            body.set("type", Json::string_value("int"));
            body.set("variablesReference", Json::number_value(0));
            send(response(request, ok, std::move(body)));
        } else if (command == "readMemory") {
            std::uint32_t address = 0;
            std::uint32_t count = 0;
            if (arguments != nullptr) {
                if (const Json* reference = arguments->get("memoryReference")) {
                    address = static_cast<std::uint32_t>(
                        std::strtoul(reference->as_string().c_str(), nullptr, 0));
                }
                if (const Json* length = arguments->get("count")) {
                    count = static_cast<std::uint32_t>(length->as_number());
                }
            }
            std::vector<std::uint8_t> bytes;
            for (std::uint32_t index = 0; index < count; ++index) {
                std::uint8_t byte = 0;
                if (!session_.machine().read8(address + index, byte)) break;
                bytes.push_back(byte);
            }
            Json body = Json::object_value();
            body.set("address", Json::string_value("0x" + hex(address)));
            body.set("data", Json::string_value(base64(bytes)));
            send(response(request, true, std::move(body)));
        } else if (command == "disconnect" || command == "terminate") {
            send(response(request, true));
            quit_ = true;
        } else {
            error_ = "unsupported command: " + command;
            send(response(request, false));
        }
    }

    static std::string hex(std::uint32_t value) {
        char buffer[12] = {};
        std::snprintf(buffer, sizeof(buffer), "%08X", value);
        return buffer;
    }

    struct FrameInfo {
        debug::Frame frame;
        std::int64_t id = 0;
    };

    Channel& channel_;
    std::string image_path_;
    container::Image image_;
    debug::Session session_;
    std::vector<FrameInfo> frames_;
    FrameInfo selected_;
    std::int64_t next_frame_id_ = 1;
    std::int64_t seq_ = 1;
    std::string error_;
    bool quit_ = false;
};

} // namespace

int run(const Options& options) {
    std::string error;
    if (options.stdio || options.port == 0) {
        StdioChannel channel;
        Server server(channel, options.image_path);
        server.run();
        return 0;
    }
    TcpChannel channel;
    if (!channel.listen(options.port, error)) {
        std::fprintf(stderr, "kr32 dap: %s\n", error.c_str());
        return 1;
    }
    std::printf("DAP listening on 127.0.0.1:%d\n", options.port);
    std::fflush(stdout);
    Server server(channel, options.image_path);
    server.run();
    return 0;
}

} // namespace kr32::dap
