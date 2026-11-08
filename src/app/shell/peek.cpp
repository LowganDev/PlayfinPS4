#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/shell/format.hpp"
#include "app/shell/screen.hpp"
#include "app/shell/screens.hpp"
#include "ui/i18n.hpp"
#include "ui/image_view.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

namespace {

constexpr int kActors = 6;

std::string joined(const std::vector<std::string>& parts, const char* separator) {
    std::string out;
    for (const std::string& part : parts) {
        out += (out.empty() ? "" : separator) + part;
    }
    return out;
}

// the parts of the peek that change when the item's details arrive
struct PeekViews {
    ui::Label* meta = nullptr;
    ui::Label* chips = nullptr;
    ui::Label* overview = nullptr;
    ui::Label* cast = nullptr;
};

void fill(const PeekViews& views, const jellyfin::Item& item) {
    views.meta->setText(joined({metaLine(item), joined(item.genres, ", ")}, " \xC2\xB7 "));
    views.meta->visible = !views.meta->text().empty();
    std::vector<std::string> chips;
    if (!item.mediaSources.empty()) {
        chips = qualityChips(item.mediaSources.front());
    }
    views.chips->setText(joined(chips, "  \xC2\xB7  "));
    views.chips->visible = !chips.empty();
    views.overview->setText(item.overview);
    views.overview->visible = !item.overview.empty();
    std::vector<std::string> actors;
    for (const jellyfin::Person& person : item.people) {
        if (person.type == "Actor" && static_cast<int>(actors.size()) < kActors) {
            actors.push_back(person.name);
        }
    }
    views.cast->setText(actors.empty() ? std::string() : ui::tr("shell.peek.with", {joined(actors, ", ")}));
    views.cast->visible = !actors.empty();
}

}  // namespace

void openPeek(ShellScreen& screen, const jellyfin::Item& item) {
    ui::Application* app = ui::Application::current();
    const auto client = screen.session().client();
    if (app == nullptr || !client || item.id.empty()) {
        return;
    }
    auto overlay = std::make_unique<ui::Stack>();
    overlay->background = ui::theme().scrim;
    overlay->horizontal = ui::CrossAlign::Center;
    overlay->vertical = ui::CrossAlign::Center;
    overlay->name = "peek";
    auto* card = overlay->add<ui::Box>(ui::Axis::Row);
    card->padding = ui::Insets::all(ui::theme().spacingXl);
    card->gap = ui::theme().spacingXl;
    card->background = ui::theme().surfaceRaised;
    card->cornerRadius = ui::theme().radius;
    auto* poster = card->add<ui::ImageView>();
    poster->width = ui::SizeSpec::fixed(260);
    poster->height = ui::SizeSpec::fixed(390);
    poster->clip = true;
    poster->setUrl(imageUrlFor(*client, item, false, 260, 390));
    auto* texts = card->add<ui::Box>(ui::Axis::Column);
    texts->gap = ui::theme().spacingM;
    auto* title = texts->add<ui::Label>(cardTitle(item), ui::FontStyle{ui::theme().fontHeading, ui::FontWeight::SemiBold});
    title->width = ui::SizeSpec::fixed(820);
    title->maxLines = 2;
    PeekViews views;
    const auto small = ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular};
    views.meta = texts->add<ui::Label>("", small);
    views.meta->width = ui::SizeSpec::fixed(820);
    views.meta->color = ui::theme().textSecondary;
    views.chips = texts->add<ui::Label>("", ui::FontStyle{ui::theme().fontSmall, ui::FontWeight::SemiBold});
    views.overview = texts->add<ui::Label>("", small);
    views.overview->width = ui::SizeSpec::fixed(820);
    views.overview->maxLines = 6;
    views.cast = texts->add<ui::Label>("", small);
    views.cast->width = ui::SizeSpec::fixed(820);
    views.cast->maxLines = 2;
    views.cast->color = ui::theme().textSecondary;
    fill(views, item);
    AppSession* session = &screen.session();
    auto* open = texts->add<ui::Button>(ui::tr("shell.peek.open"), [session, item] {
        ui::Application::current()->closeModal();
        openItem(*session, item);
    });
    open->setPrimary(true);
    overlay->remembered = open;
    overlay->bind(platform::Action::Back, ui::tr("ui.close"), [] {
        ui::Application::current()->closeModal();
        return true;
    });
    overlay->bind(platform::Action::Options, "", [] {  // △ again closes it too
        ui::Application::current()->closeModal();
        return true;
    });
    // the details (synopsis, versions, cast) arrive after the modal opened; closed meanwhile, they are dropped
    auto alive = std::make_shared<bool>(true);
    struct Closer : ui::View {
        std::shared_ptr<bool> alive;
        ~Closer() override { *alive = false; }
    };
    overlay->add<Closer>()->alive = alive;
    app->showModal(std::move(overlay));
    const std::string id = item.id;
    const core::CancelToken token = screen.cancelToken();
    screen.run<core::Result<jellyfin::Item>>([client, id, token] { return client->item(id, token); },
                                             [alive, views](core::Result<jellyfin::Item> details) {
                                                 if (*alive && details.ok()) {
                                                     fill(views, details.value());
                                                 }
                                             });
}

}  // namespace playfin::app
