#pragma once

#include <string>
#include <vector>

#include "ui/view.hpp"

namespace playfin::ui {

enum class TextAlign { Left, Center, Right };

// Text with wrapping, a line limit and an ellipsis. maxLines 0 = unlimited.
class Label : public View {
public:
    explicit Label(std::string text = {}, FontStyle styleValue = {});

    void setText(std::string text);
    const std::string& text() const { return text_; }
    const std::vector<std::string>& lines() const { return lines_; }

    FontStyle style;
    Color color;
    int maxLines = 1;
    TextAlign align = TextAlign::Left;

    Size measure(Size available, TextMetrics& metrics) override;
    void draw(NVGcontext* vg) const override;

protected:
    void layout(TextMetrics& metrics) override;

private:
    std::string text_;
    std::vector<std::string> lines_;
    float lineHeight_ = 0;
    float shift_ = 0;  // capitals in the middle of each line (topAlignedShift)
    float linesWidth_ = -1;  // width the lines were broken for (cache)
};

}  // namespace playfin::ui
