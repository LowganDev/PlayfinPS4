#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "platform/input.hpp"

namespace playfin::testing {

// One line of a UI test script.
//   wait <frames>        let 1..9999 frames go by
//   press <Action>       action down this frame, up the next one
//   screenshot <name>    save the frame as <output>/<name>.png ([a-z0-9_-] only)
//   quit                 end the run (exit code 0)
//   type <text>          send the text to the focused text editor (rest of the line)
struct Step {
    enum class Kind { Wait, Press, Screenshot, Quit, Type };
    Kind kind = Kind::Quit;
    int frames = 0;
    platform::Action action = platform::Action::Confirm;
    std::string name;
};

struct Script {
    std::vector<Step> steps;
    std::string error;  // empty when the script is valid
    int errorLine = 0;  // 1-based
    bool ok() const { return error.empty(); }
};

Script parseScript(std::string_view text);

}  // namespace playfin::testing
