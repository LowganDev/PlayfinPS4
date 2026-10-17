#include "testing/script.hpp"

#include <cctype>

namespace playfin::testing {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

bool validName(std::string_view name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool parseFrames(std::string_view text, int& out) {
    if (text.empty() || text.size() > 4) {
        return false;
    }
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    if (value < 1) {
        return false;
    }
    out = value;
    return true;
}

}  // namespace

Script parseScript(std::string_view text) {
    Script script;
    int lineNumber = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        ++lineNumber;
        const std::string_view line = trim(text.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t space = line.find(' ');
        const std::string_view command = line.substr(0, space);
        const std::string_view argument = space == std::string_view::npos ? "" : trim(line.substr(space + 1));
        Step step;
        bool valid = false;
        if (command == "wait") {
            step.kind = Step::Kind::Wait;
            valid = parseFrames(argument, step.frames);
        } else if (command == "press") {
            step.kind = Step::Kind::Press;
            const auto action = platform::actionFromName(argument);
            valid = action.has_value();
            if (valid) {
                step.action = *action;
            }
        } else if (command == "screenshot") {
            step.kind = Step::Kind::Screenshot;
            valid = validName(argument);
            step.name = std::string(argument);
        } else if (command == "type") {
            step.kind = Step::Kind::Type;
            step.name = std::string(argument);
            valid = !argument.empty();
        } else if (command == "quit") {
            step.kind = Step::Kind::Quit;
            valid = argument.empty();
        }
        if (!valid) {
            script.error = "invalid line: " + std::string(line);
            script.errorLine = lineNumber;
            script.steps.clear();
            return script;
        }
        script.steps.push_back(step);
    }
    return script;
}

}  // namespace playfin::testing
