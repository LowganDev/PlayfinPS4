#pragma once

#include <string_view>

#include "core/utf.hpp"
#include "ui/text.hpp"

namespace playfin::test {

// Each code point is size * 0.5 wide; lines are size * 1.25 high.
class FixedMetrics : public ui::TextMetrics {
public:
    float measure(std::string_view text, const ui::FontStyle& style) override {
        std::size_t count = 0;
        std::size_t pos = 0;
        while (pos < text.size()) {
            core::nextCodePoint(text, pos);
            ++count;
        }
        return static_cast<float>(count) * style.size * 0.5f;
    }
    float lineHeight(const ui::FontStyle& style) override { return style.size * 1.25f; }
};

}  // namespace playfin::test
