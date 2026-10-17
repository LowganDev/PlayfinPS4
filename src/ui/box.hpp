#pragma once

#include "ui/view.hpp"

namespace playfin::ui {

enum class Axis { Row, Column };
enum class Align { Start, Center, End, SpaceBetween };  // main axis
enum class CrossAlign { Start, Center, End, Stretch };

// Children in a row or a column: Fixed / Fit sizes first, Fill children share what remains by weight.
class Box : public View {
public:
    explicit Box(Axis axisValue = Axis::Column) : axis(axisValue) {}

    Axis axis;
    float gap = 0;
    Align align = Align::Start;
    CrossAlign crossAlign = CrossAlign::Start;

    Size measure(Size available, TextMetrics& metrics) override;

protected:
    void layout(TextMetrics& metrics) override;
};

// Children overlaid, aligned in the content area (Fill children cover it).
class Stack : public View {
public:
    CrossAlign horizontal = CrossAlign::Start;
    CrossAlign vertical = CrossAlign::Start;

protected:
    void layout(TextMetrics& metrics) override;
};

}  // namespace playfin::ui
