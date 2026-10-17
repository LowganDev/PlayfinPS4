#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "platform/text_input.hpp"
#include "ui/box.hpp"
#include "ui/label.hpp"

namespace playfin::ui {

// Surface fill; focused (or primary): accent fill, text in onAccent, SemiBold.
class Button : public Box {
public:
    Button(std::string text, std::function<void()> onPress);
    void setText(std::string text);
    void setPrimary(bool primary);
    void onFocusChanged(bool focused) override;
    void onThemeChanged() override;

private:
    void refresh();
    Label* label_;
    bool focused_ = false;
    bool primary_ = false;
};

class ProgressBar : public View {
public:
    ProgressBar();
    float value = 0;  // 0..1
    void draw(NVGcontext* vg) const override;
};

class Spinner : public View {
public:
    Spinner();
    void draw(NVGcontext* vg) const override;
};

// Shows its value (dots for passwords) and opens the text input on Confirm.
class TextField : public Box {
public:
    TextField(std::string placeholder, platform::TextInputRequest request,
              std::function<void(const std::string&)> onChange);
    const std::string& value() const { return value_; }
    void setValue(std::string value);
    void onFocusChanged(bool focused) override;
    void onThemeChanged() override;

private:
    void refresh();
    bool focused_ = false;

    std::string placeholder_;
    platform::TextInputRequest request_;
    std::function<void(const std::string&)> onChange_;
    std::string value_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);  // the keyboard may answer after we are gone
    Label* label_;
};

// A settings line: its label on the left, its current value on the right; surface fill, accent fill when focused.
class ValueRow : public Box {
public:
    ValueRow(std::string label, std::string value, std::function<void()> onPress);
    void setValue(std::string value);
    const std::string& value() const;
    void onFocusChanged(bool focused) override;
    void onThemeChanged() override;

private:
    void refresh();
    bool focused_ = false;
    Label* label_;
    Label* value_;
};

// Modal card listing `choices` (scrolling when long), the focus on `selected`; choosing one closes the dialog,
// then calls `chosen` with its index. ○ closes it without a choice.
std::unique_ptr<View> makeChoiceDialog(std::string title, std::vector<std::string> choices, int selected,
                                       std::function<void(int)> chosen);

// Modal card: title, message, one button per entry (pressing one closes the dialog, then runs its action).
std::unique_ptr<View> makeDialog(std::string title, std::string message,
                                 std::vector<std::pair<std::string, std::function<void()>>> buttons);

}  // namespace playfin::ui
