#pragma once

#include <algorithm>

namespace playfin::ui {

struct Point {
    float x = 0;
    float y = 0;
};

struct Size {
    float width = 0;
    float height = 0;
};

struct Insets {
    float top = 0;
    float right = 0;
    float bottom = 0;
    float left = 0;
    static Insets all(float value) { return {value, value, value, value}; }
    static Insets symmetric(float vertical, float horizontal) { return {vertical, horizontal, vertical, horizontal}; }
    float horizontal() const { return left + right; }
    float vertical() const { return top + bottom; }
};

struct Rect {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;

    float right() const { return x + width; }
    float bottom() const { return y + height; }
    float centerX() const { return x + width / 2; }
    float centerY() const { return y + height / 2; }
    bool contains(Point p) const { return p.x >= x && p.x < right() && p.y >= y && p.y < bottom(); }
    bool intersects(const Rect& other) const {
        return x < other.right() && other.x < right() && y < other.bottom() && other.y < bottom();
    }
    Rect translated(float dx, float dy) const { return {x + dx, y + dy, width, height}; }
};

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline Rect lerp(const Rect& a, const Rect& b, float t) {
    return {lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.width, b.width, t), lerp(a.height, b.height, t)};
}

}  // namespace playfin::ui
