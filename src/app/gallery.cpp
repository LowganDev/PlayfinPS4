#include "app/gallery.hpp"

#include <stb/stb_image_write.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "ui/box.hpp"
#include "ui/image_view.hpp"
#include "ui/label.hpp"
#include "ui/list.hpp"
#include "ui/scroll.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

namespace {

void appendTo(void* context, void* data, int size) {
    static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<std::size_t>(size));
}

class Card : public ui::Box {
public:
    Card(ui::Size imageSize) : ui::Box(ui::Axis::Column) {
        focusable = true;
        cornerRadius = ui::theme().radius;
        gap = ui::theme().spacingS;
        image = add<ui::ImageView>();
        image->width = ui::SizeSpec::fixed(imageSize.width);
        image->height = ui::SizeSpec::fixed(imageSize.height);
        title = add<ui::Label>("", ui::FontStyle{ui::theme().fontSmall, ui::FontWeight::Regular});
        title->width = ui::SizeSpec::fill();
        bind(platform::Action::Confirm, "Open", [this] {
            if (ui::Application* app = ui::Application::current()) {
                app->toast(title->text());
            }
            return true;
        });
    }
    ui::ImageView* image;
    ui::Label* title;
};

class Cards : public ui::DataSource {
public:
    Cards(int count, ui::Size imageSize, std::string prefix)
        : count_(count), imageSize_(imageSize), prefix_(std::move(prefix)) {}
    int count() const override { return count_; }
    std::unique_ptr<ui::View> create() override { return std::make_unique<Card>(imageSize_); }
    void bind(ui::View& view, int index) override {
        auto& card = static_cast<Card&>(view);
        card.image->setUrl("gallery://" + std::to_string(index));
        card.title->setText(prefix_ + " " + std::to_string(index + 1));
    }

private:
    int count_;
    ui::Size imageSize_;
    std::string prefix_;
};

ui::View* section(ui::Box& column, const std::string& title) {
    auto* box = column.add<ui::Box>(ui::Axis::Column);
    box->width = ui::SizeSpec::fill();
    box->gap = ui::theme().spacingM;
    box->add<ui::Label>(title, ui::FontStyle{ui::theme().fontTitle, ui::FontWeight::SemiBold});
    return box;
}

class GridScreen : public ui::Screen {
public:
    GridScreen() {
        auto* column = add<ui::Box>(ui::Axis::Column);
        column->width = ui::SizeSpec::fill();
        column->height = ui::SizeSpec::fill();
        column->padding = {ui::theme().spacingXxl, 96, 0, 96};
        column->gap = ui::theme().spacingL;
        column->add<ui::Label>("Grid (500 items)", ui::FontStyle{ui::theme().fontHeading, ui::FontWeight::SemiBold});
        auto* grid = column->add<ui::Grid>(ui::Size{200, 340}, 24.0f, 7);
        grid->padding = ui::Insets::all(8);
        grid->setSource(std::make_unique<Cards>(500, ui::Size{200, 300}, "Item"));
    }
};

class Gallery : public ui::Screen {
public:
    Gallery() {
        auto* scroll = add<ui::VScroll>();
        scroll->leadingInset = ui::theme().spacingXxl;
        auto content = std::make_unique<ui::Box>(ui::Axis::Column);
        content->padding = {ui::theme().spacingXxl, 96, 120, 96};
        content->gap = ui::theme().spacingXl;
        content->add<ui::Label>("Playfin toolkit", ui::FontStyle{ui::theme().fontDisplay, ui::FontWeight::SemiBold});

        auto* posters = static_cast<ui::Box*>(section(*content, "Posters"));
        auto* posterRow = posters->add<ui::HList>(ui::Size{200, 340}, 24.0f);
        posterRow->padding = ui::Insets::all(8);
        posterRow->setSource(std::make_unique<Cards>(40, ui::Size{200, 300}, "Poster"));

        auto* wide = static_cast<ui::Box*>(section(*content, "Thumbnails"));
        auto* wideRow = wide->add<ui::HList>(ui::Size{320, 220}, 24.0f);
        wideRow->padding = ui::Insets::all(8);
        wideRow->setSource(std::make_unique<Cards>(20, ui::Size{320, 180}, "Episode"));

        auto* controls = static_cast<ui::Box*>(section(*content, "Controls"));
        auto* row = controls->add<ui::Box>(ui::Axis::Row);
        row->gap = ui::theme().spacingL;
        row->crossAlign = ui::CrossAlign::Center;
        row->add<ui::Button>("Open dialog", [] {
            if (ui::Application* app = ui::Application::current()) {
                app->showModal(ui::makeDialog("Dialog title", "A message that may be long enough to wrap on two lines "
                                                              "inside the dialog card.",
                                              {{"OK", {}}, {"Cancel", {}}}));
            }
        });
        row->add<ui::Button>("Show toast", [] {
            if (ui::Application* app = ui::Application::current()) {
                app->toast("Hello from the toast");
            }
        });
        platform::TextInputRequest request;
        request.title = "Your name";
        row->add<ui::TextField>("Your name", request, [](const std::string&) {});
        row->add<ui::Button>("Open grid", [] {
            if (ui::Application* app = ui::Application::current()) {
                app->push(std::make_unique<GridScreen>());
            }
        });
        auto* progress = controls->add<ui::ProgressBar>();
        progress->value = 0.42f;
        progress->width = ui::SizeSpec::fixed(600);
        controls->add<ui::Spinner>();

        auto* text = static_cast<ui::Box*>(section(*content, "Text"));
        auto* longText = text->add<ui::Label>(
            "A long description that keeps going to show how labels wrap and end with an ellipsis when the text is "
            "longer than the space it gets. It continues with more words so that a third line would be needed, and "
            "even a fourth one, which is cut.",
            ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        longText->width = ui::SizeSpec::fixed(900);
        longText->maxLines = 3;
        longText->color = ui::theme().textSecondary;
        text->add<ui::Label>("日本語のテキスト · 中文文本 · 한국어 텍스트 · Ελληνικά · Русский",
                             ui::FontStyle{ui::theme().fontTitle, ui::FontWeight::Regular});
        scroll->setContent(std::move(content));
    }
};

}  // namespace

std::unique_ptr<ui::Screen> makeGallery() { return std::make_unique<Gallery>(); }

core::Result<std::string> galleryImage(const std::string& url) {
    const std::string prefix = "gallery://";
    if (url.compare(0, prefix.size(), prefix) != 0) {
        return core::makeError(core::ErrorKind::NotFound, "not a gallery image");
    }
    const int index = std::atoi(url.c_str() + prefix.size());
    const int width = 160;
    const int height = 240;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 3));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned char* p = &pixels[static_cast<std::size_t>((y * width + x) * 3)];
            p[0] = static_cast<unsigned char>((index * 53 + x) % 256);
            p[1] = static_cast<unsigned char>((index * 97 + y) % 256);
            p[2] = static_cast<unsigned char>((index * 31 + 128) % 256);
        }
    }
    std::string out;
    stbi_write_png_to_func(appendTo, &out, width, height, 3, pixels.data(), width * 3);
    return out;
}

}  // namespace playfin::app
