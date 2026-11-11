#include "app/shell/cards.hpp"

#include <nanovg.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "app/shell/downloads.hpp"
#include "app/shell/format.hpp"
#include "ui/animation.hpp"
#include "ui/context.hpp"
#include "ui/draw.hpp"
#include "ui/halo.hpp"
#include "ui/i18n.hpp"

namespace playfin::app {

ui::Size cardImageSize(CardShape shape) {
    switch (shape) {
    case CardShape::Wide:
        return {468, 264};
    case CardShape::Poster:
        return {323, 485};
    case CardShape::Square:
        return {323, 323};
    }
    return {323, 485};
}

ui::Size cardSize(CardShape shape) {
    const ui::Size image = cardImageSize(shape);
    return {image.width, image.height + 12 + 34 + 28};
}

ItemCard::ItemCard(CardShape shape) : ui::Box(ui::Axis::Column) {
    focusable = true;
    drawsOwnFocus = true;
    gap = 12;
    const ui::Size size = cardImageSize(shape);
    auto* picture = add<ui::Stack>();
    picture->width = ui::SizeSpec::fixed(size.width);
    picture->height = ui::SizeSpec::fixed(size.height);
    picture->vertical = ui::CrossAlign::End;
    picture->clip = true;
    picture->cornerRadius = ui::theme().radius;
    image_ = picture->add<ui::ImageView>();
    image_->width = ui::SizeSpec::fill();
    image_->height = ui::SizeSpec::fill();
    progress_ = picture->add<ui::ProgressBar>();
    progress_->width = ui::SizeSpec::fill();
    progress_->height = ui::SizeSpec::fixed(6);
    progress_->visible = false;
    title_ = add<ui::Label>("", ui::FontStyle{ui::theme().fontBody, ui::FontWeight::SemiBold});
    title_->width = ui::SizeSpec::fill();
    subtitle_ = add<ui::Label>("", ui::FontStyle{ui::theme().fontSmall, ui::FontWeight::Regular});
    subtitle_->width = ui::SizeSpec::fill();
    subtitle_->color = ui::theme().textSecondary;
    bind(platform::Action::Confirm, ui::tr("ui.select"), [this] {
        if (onPress) {
            onPress();
        }
        return true;
    });
}

ItemCard::~ItemCard() {
    if (ui::context().animator != nullptr) {
        ui::context().animator->cancel(this);
    }
}

void ItemCard::onFocusChanged(bool focused) {
    if (ui::context().animator != nullptr) {
        ui::context().animator->animate(&focus_, focused ? 1.0f : 0.0f, ui::theme().focusMs, ui::Easing::Smooth,
                                        this);
    } else {
        focus_ = focused ? 1.0f : 0.0f;
    }
}

namespace {

constexpr float kGlowFeather = 50;
constexpr float kRingWidth = 3;
constexpr float kRingGap = 4;
constexpr float kThemeRingWidth = 4;  // Ring and Frame

int sheenSprite(NVGcontext* vg, int width, int height) {
    struct Entry {
        NVGcontext* vg;
        int width;
        int height;
        int image;
    };
    static std::vector<Entry> cache;  // the band is white: only its strength comes from the theme
    for (const Entry& entry : cache) {
        if (entry.vg == vg && entry.width == width && entry.height == height) {
            return entry.image;
        }
    }
    const std::vector<std::uint8_t> pixels = ui::sheenImage(width, height, ui::theme().sheenStrength);
    const int image = nvgCreateImageRGBA(vg, width, height, NVG_IMAGE_PREMULTIPLIED, pixels.data());
    cache.push_back({vg, width, height, image});
    return image;
}

// The glow image of a picture size for the current accent, made once per canvas (a live box gradient costs
// ~200 cycles per pixel on the PS4).
int glowSprite(NVGcontext* vg, int width, int height) {
    struct Entry {
        NVGcontext* vg;
        int width;
        int height;
        unsigned generation;
        int image;
    };
    static std::vector<Entry> cache;
    for (Entry& entry : cache) {
        if (entry.vg == vg && entry.width == width && entry.height == height) {
            if (entry.generation == ui::themeGeneration()) {
                return entry.image;
            }
            nvgDeleteImage(vg, entry.image);
            entry = cache.back();
            cache.pop_back();
            break;
        }
    }
    const std::vector<std::uint8_t> pixels =
        ui::glowImage(width, height, ui::theme().radius, kGlowFeather, ui::theme().accent, ui::theme().glowStrength);
    const int image = nvgCreateImageRGBA(vg, width + 2 * static_cast<int>(kGlowFeather),
                                         height + 2 * static_cast<int>(kGlowFeather), NVG_IMAGE_PREMULTIPLIED,
                                         pixels.data());
    cache.push_back({vg, width, height, ui::themeGeneration(), image});
    return image;
}

}  // namespace

namespace {

// The border image of a picture size for the current theme, made once per canvas (see ui::ringImage).
int borderSprite(NVGcontext* vg, int width, int height) {
    struct Entry {
        NVGcontext* vg;
        int width;
        int height;
        unsigned generation;
        int image;
    };
    static std::vector<Entry> cache;
    for (Entry& entry : cache) {
        if (entry.vg == vg && entry.width == width && entry.height == height) {
            if (entry.generation == ui::themeGeneration()) {
                return entry.image;
            }
            nvgDeleteImage(vg, entry.image);
            entry = cache.back();
            cache.pop_back();
            break;
        }
    }
    const ui::Theme& t = ui::theme();
    const std::vector<std::uint8_t> pixels = ui::ringImage(width, height, t.radius, t.cardBorderWidth, t.cardBorder);
    const int image = nvgCreateImageRGBA(vg, width, height, NVG_IMAGE_PREMULTIPLIED, pixels.data());
    cache.push_back({vg, width, height, ui::themeGeneration(), image});
    return image;
}

}  // namespace

namespace {

constexpr float kShadowFeather = 14;
constexpr float kShadowDrop = 8;  // lower than the picture
constexpr float kShadowFade = 24;  // the sides fade out over this height above the picture's lower corners

// The shadow image of a picture size for the current theme, made once per canvas (black, soft, see glowImage).
int shadowSprite(NVGcontext* vg, int width, int height) {
    struct Entry {
        NVGcontext* vg;
        int width;
        int height;
        unsigned generation;
        int image;
    };
    static std::vector<Entry> cache;
    for (Entry& entry : cache) {
        if (entry.vg == vg && entry.width == width && entry.height == height) {
            if (entry.generation == ui::themeGeneration()) {
                return entry.image;
            }
            nvgDeleteImage(vg, entry.image);
            entry = cache.back();
            cache.pop_back();
            break;
        }
    }
    std::vector<std::uint8_t> pixels = ui::glowImage(width, height, ui::theme().radius, kShadowFeather,
                                                     ui::Color{0, 0, 0, 255}, ui::theme().cardShadow);
    // Only the bottom is kept: rows above the lower corners fade to nothing (drawShadow draws that part only).
    const int imageWidth = width + 2 * static_cast<int>(kShadowFeather);
    const int imageHeight = height + 2 * static_cast<int>(kShadowFeather);
    const float fadeEnd = static_cast<float>(height) + kShadowFeather - kShadowDrop - ui::theme().radius;
    const float fadeStart = fadeEnd - kShadowFade;
    for (int row = 0; row < imageHeight; ++row) {
        const float py = static_cast<float>(row) + 0.5f;
        const float keep = py <= fadeStart ? 0.0f : py >= fadeEnd ? 1.0f : (py - fadeStart) / kShadowFade;
        if (keep >= 1.0f) {
            continue;
        }
        std::uint8_t* line = pixels.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(imageWidth) * 4u;
        for (int i = 0; i < imageWidth * 4; ++i) {
            line[i] = static_cast<std::uint8_t>(static_cast<float>(line[i]) * keep + 0.5f);
        }
    }
    const int image = nvgCreateImageRGBA(vg, imageWidth, imageHeight, NVG_IMAGE_PREMULTIPLIED, pixels.data());
    cache.push_back({vg, width, height, ui::themeGeneration(), image});
    return image;
}

}  // namespace

// The theme's shadow under the picture: only the band below it and its lower corners, the sides fading upwards (the
// opaque picture hides the rest; a shadow all around cost the console about 5 ms a frame)
void ItemCard::drawShadow(NVGcontext* vg, const ui::Rect& picture) const {
    const ui::Theme& t = ui::theme();
    if (t.cardShadow <= 0) {
        return;
    }
    const int image = shadowSprite(vg, static_cast<int>(picture.width), static_cast<int>(picture.height));
    if (image <= 0) {
        return;
    }
    const float f = kShadowFeather;
    const float x = picture.x;
    const float y = picture.y + kShadowDrop;
    const float w = picture.width;
    const float h = picture.height;
    const NVGpaint paint = nvgImagePattern(vg, x - f, y - f, w + 2 * f, h + 2 * f, 0, image, 1);
    const float inset = t.radius;
    const float pictureBottom = picture.y + h;
    const float sideTop = pictureBottom - inset - kShadowFade;  // above it the image is clear (shadowSprite)
    const float rects[3][4] = {{x - f, pictureBottom, w + 2 * f, y + h + f - pictureBottom},
                               {x - f, sideTop, f + inset, pictureBottom - sideTop},
                               {x + w - inset, sideTop, f + inset, pictureBottom - sideTop}};
    nvgSave(vg);
    nvgShapeAntiAlias(vg, 0);
    for (const auto& r : rects) {
        if (r[2] <= 0 || r[3] <= 0) {
            continue;
        }
        nvgBeginPath(vg);
        nvgRect(vg, r[0], r[1], r[2], r[3]);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
    }
    nvgRestore(vg);
}

// The theme's border on the picture's edge: its image over the four corners and the four thin edges only.
void ItemCard::drawBorder(NVGcontext* vg, const ui::Rect& picture) const {
    const ui::Theme& t = ui::theme();
    if (t.cardBorderWidth <= 0) {
        return;
    }
    const int image = borderSprite(vg, static_cast<int>(picture.width), static_cast<int>(picture.height));
    if (image <= 0) {
        return;
    }
    const NVGpaint paint = nvgImagePattern(vg, picture.x, picture.y, picture.width, picture.height, 0, image, 1);
    const float r = static_cast<float>(std::ceil(std::min(t.radius, std::min(picture.width, picture.height) / 2)));
    const float b = static_cast<float>(std::ceil(t.cardBorderWidth)) + 1;
    const float x = picture.x;
    const float y = picture.y;
    const float w = picture.width;
    const float h = picture.height;
    const float rects[8][4] = {{x, y, r, r},         {x + w - r, y, r, r},         {x, y + h - r, r, r},
                               {x + w - r, y + h - r, r, r}, {x + r, y, w - 2 * r, b}, {x + r, y + h - b, w - 2 * r, b},
                               {x, y + r, b, h - 2 * r}, {x + w - b, y + r, b, h - 2 * r}};
    nvgSave(vg);
    nvgShapeAntiAlias(vg, 0);  // the image has its own soft edge
    for (const auto& rect : rects) {
        nvgBeginPath(vg);
        nvgRect(vg, rect[0], rect[1], rect[2], rect[3]);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
    }
    nvgRestore(vg);
}

void ItemCard::draw(NVGcontext* vg) const {
    const ui::Theme& t = ui::theme();
    const float p = focus_;
    const ui::Rect picture = children().front()->frame;
    if (p <= 0.001f) {
        drawShadow(vg, picture);
        ui::Box::draw(vg);
        drawBorder(vg, picture);
        return;
    }
    // the glow: accent at the theme's strength, 50 px soft, 18 px lower, under the grown picture
    const float grow = 0.07f * p;
    const float w = picture.width * (1 + grow);
    const float h = picture.height * (1 + grow);
    const float x = picture.x - (w - picture.width) / 2;
    const float y = picture.y - (h - picture.height) / 2 - 6 * p + 18 * p;
    const int image = t.focusStyle == ui::FocusStyle::Glow
                          ? glowSprite(vg, static_cast<int>(picture.width), static_cast<int>(picture.height))
                          : 0;
    if (image > 0) {
        const float margin = kGlowFeather * (1 + grow);
        const NVGpaint paint = nvgImagePattern(vg, x - margin, y - margin, w + 2 * margin, h + 2 * margin, 0, image, p);
        // the glow around the grown picture only: under it, the opaque picture hides it (its rounded corners need
        // the glow, hence the inset); four rectangles instead of one
        const float left = x - margin;
        const float top = y - margin;
        const float right = x + w + margin;
        const float bottom = y + h + margin;
        const float inset = ui::theme().radius * (1 + grow);
        const float holeLeft = x + inset;
        const float holeRight = x + w - inset;
        const float holeTop = y - 18 * p + inset;  // the picture sits 18 px above its glow
        const float holeBottom = y - 18 * p + h - inset;
        const float rects[4][4] = {{left, top, right - left, holeTop - top},
                                   {left, holeTop, holeLeft - left, holeBottom - holeTop},
                                   {holeRight, holeTop, right - holeRight, holeBottom - holeTop},
                                   {left, holeBottom, right - left, bottom - holeBottom}};
        nvgSave(vg);
        nvgShapeAntiAlias(vg, 0);  // no edge fringes: the four pieces meet without seams (the glow is soft anyway)
        for (const auto& r : rects) {
            nvgBeginPath(vg);  // one convex fill each (several rectangles in one path need the stencil)
            nvgRect(vg, r[0], r[1], r[2], r[3]);
            nvgFillPaint(vg, paint);
            nvgFill(vg);
        }
        nvgRestore(vg);
    }
    // the whole card grows around its picture's centre and rises
    const float cx = picture.x + picture.width / 2;
    const float cy = picture.y + picture.height / 2;
    nvgSave(vg);
    nvgTranslate(vg, cx, cy - 6 * p);
    nvgScale(vg, 1 + grow, 1 + grow);
    nvgTranslate(vg, -cx, -cy);
    drawShadow(vg, picture);
    ui::Box::draw(vg);
    drawBorder(vg, picture);
    if (t.focusSheen) {
        const int sheen = sheenSprite(vg, static_cast<int>(picture.width), static_cast<int>(picture.height));
        if (sheen > 0) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, picture.x, picture.y, picture.width, picture.height, t.radius);
            nvgFillPaint(vg, nvgImagePattern(vg, picture.x, picture.y, picture.width, picture.height, 0, sheen, p));
            nvgFill(vg);
        }
    }
    // Glow: 3 px of the accent, 4 px away; Ring: 4 px of the accent (or the theme's colour), 4 px away; Frame: 4 px
    // of the theme's colour on the picture's edge. Fading in with the focus.
    const bool frame = t.focusStyle == ui::FocusStyle::Frame;
    const ui::Color colour = t.focusStyle == ui::FocusStyle::Glow || t.focusInAccent ? t.accent : t.focusColor;
    const float width = t.focusStyle == ui::FocusStyle::Glow ? kRingWidth : kThemeRingWidth;
    const float gap = frame ? -width / 2 : kRingGap + width / 2;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, picture.x - gap, picture.y - gap, picture.width + 2 * gap, picture.height + 2 * gap,
                   std::max(0.0f, t.radius + gap));
    nvgStrokeColor(vg, nvgRGBA(colour.r, colour.g, colour.b, static_cast<unsigned char>(255 * p)));
    nvgStrokeWidth(vg, width);
    nvgStroke(vg);
    nvgRestore(vg);
}

void ItemCard::show(const jellyfin::Item& item, const std::string& imageUrl, int utcOffsetMinutes) {
    item_ = item;
    // a programme without its own picture shows its channel's logo whole
    image_->fit = item.type == jellyfin::ItemType::Program && item.images.primary.empty() ? ui::ImageView::Fit::Contain
                                                                                         : ui::ImageView::Fit::Cover;
    image_->setUrl(imageUrl);
    title_->setText(cardTitle(item));
    subtitle_->setText(cardSubtitle(item, utcOffsetMinutes));
    const double percent = item.userData.playedPercentage;
    progress_->visible = percent > 0 && percent < 100;
    progress_->value = static_cast<float>(percent / 100.0);
    markDirty();
}

namespace {
constexpr float kProfileSize = 180;
}  // namespace

ProfileCard::ProfileCard(const std::string& name, const std::string& imageUrl, std::string symbol,
                         std::function<void()> onPress)
    : ui::Box(ui::Axis::Column) {
    focusable = true;
    drawsOwnFocus = true;
    gap = ui::theme().spacingM;
    crossAlign = ui::CrossAlign::Center;
    width = ui::SizeSpec::fixed(kProfileSize + 60);
    circle_ = add<ui::Stack>();
    circle_->width = ui::SizeSpec::fixed(kProfileSize);
    circle_->height = ui::SizeSpec::fixed(kProfileSize);
    circle_->cornerRadius = kProfileSize / 2;
    circle_->horizontal = ui::CrossAlign::Center;
    circle_->vertical = ui::CrossAlign::Center;
    initial_ = circle_->add<ui::Label>(std::move(symbol), ui::FontStyle{72, ui::FontWeight::Bold});
    if (!imageUrl.empty()) {
        auto* image = circle_->add<ui::ImageView>();
        image->width = ui::SizeSpec::fill();
        image->height = ui::SizeSpec::fill();
        image->cornerRadius = kProfileSize / 2;
        image->placeholder = ui::Color{0, 0, 0, 0};  // the initial shows until the picture arrives (or if it fails)
        image->setUrl(imageUrl);
    }
    auto* label = add<ui::Label>(name, ui::FontStyle{ui::theme().fontBody, ui::FontWeight::SemiBold});
    label->width = ui::SizeSpec::fill();
    label->align = ui::TextAlign::Center;
    label->maxLines = 2;
    onThemeChanged();
    bind(platform::Action::Confirm, ui::tr("ui.select"), [onPress = std::move(onPress)] {
        if (onPress) {
            onPress();
        }
        return true;
    });
}

ProfileCard::~ProfileCard() = default;

void ProfileCard::onFocusChanged(bool focused) {
    if (ui::context().animator != nullptr) {
        ui::context().animator->animate(&focus_, focused ? 1.0f : 0.0f, ui::theme().focusMs, ui::Easing::Smooth, this);
    } else {
        focus_ = focused ? 1.0f : 0.0f;
    }
}

void ProfileCard::onThemeChanged() {
    circle_->background = ui::theme().accent;
    initial_->color = ui::theme().onAccent;
    ui::Box::onThemeChanged();
}

void ProfileCard::draw(NVGcontext* vg) const {
    const float p = focus_;
    if (p <= 0) {
        ui::Box::draw(vg);
        return;
    }
    const ui::Rect c = circle_->frame;
    const float cx = c.x + c.width / 2;
    const float cy = c.y + c.height / 2;
    const float grow = 0.07f * p;
    nvgSave(vg);
    nvgTranslate(vg, cx, cy - 6 * p);
    nvgScale(vg, 1 + grow, 1 + grow);
    nvgTranslate(vg, -cx, -cy);
    ui::Box::draw(vg);
    const ui::Color accent = ui::theme().accent;
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, c.width / 2 + kRingGap + kRingWidth / 2);
    nvgStrokeColor(vg, nvgRGBA(accent.r, accent.g, accent.b, static_cast<unsigned char>(255 * p)));
    nvgStrokeWidth(vg, kRingWidth);
    nvgStroke(vg);
    nvgRestore(vg);
}

ItemsSource::ItemsSource(AppSession& session, std::vector<jellyfin::Item> items, CardShape shape,
                         std::function<void(const jellyfin::Item&)> onPress)
    : session_(session), items_(std::move(items)), shape_(shape), onPress_(std::move(onPress)) {}

std::unique_ptr<ui::View> ItemsSource::create() { return std::make_unique<ItemCard>(shape_); }

void ItemsSource::bind(ui::View& view, int index) {
    auto& card = static_cast<ItemCard&>(view);
    const jellyfin::Item& item = items_[static_cast<std::size_t>(index)];
    const ui::Size size = cardImageSize(shape_);
    const auto client = session_.client();
    std::string url = client ? imageUrlFor(*client, item, shape_ == CardShape::Wide, static_cast<int>(size.width),
                                           static_cast<int>(size.height))
                             : std::string();
    if (url.empty()) {  // a download's own picture (kept with it)
        url = session_.downloads().localImageUrl(item.id);
    }
    card.show(item, url, session_.utcOffsetMinutes(item.startUtc));
    card.onPress = [this, index] {
        if (onPress_ && index < count()) {
            onPress_(items_[static_cast<std::size_t>(index)]);
        }
    };
}

ui::HList* addItemRow(ui::Box& column, const std::string& title, AppSession& session,
                      std::vector<jellyfin::Item> items, CardShape shape,
                      std::function<void(const jellyfin::Item&)> onPress) {
    auto* section = column.add<ui::Box>(ui::Axis::Column);
    section->width = ui::SizeSpec::fill();
    section->gap = 18;
    section->add<ui::Label>(title, ui::FontStyle{ui::theme().fontHeading, ui::FontWeight::SemiBold});
    auto* row = section->add<ui::HList>(cardSize(shape), ui::theme().cardGap);
    // room around the cards for the grown focused card, taken from the margins: cards stay aligned with the title
    row->padding = ui::Insets::all(56);  // the grown card and its 50 px glow
    row->margin = ui::Insets::all(-56);
    row->setSource(std::make_unique<ItemsSource>(session, std::move(items), shape, std::move(onPress)));
    return row;
}

ui::Box* addPage(ui::View& screen, const std::string& title) {
    auto* column = screen.add<ui::Box>(ui::Axis::Column);
    column->width = ui::SizeSpec::fill();
    column->height = ui::SizeSpec::fill();
    column->padding = {ui::theme().spacingXxl, 96, 0, 96};
    column->gap = ui::theme().spacingL;
    column->add<ui::Label>(title, ui::FontStyle{ui::theme().fontLogo, ui::FontWeight::Bold});
    return column;
}

}  // namespace playfin::app
