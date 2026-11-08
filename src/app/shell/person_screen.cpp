#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/shell/cards.hpp"
#include "app/shell/chrome.hpp"
#include "app/shell/format.hpp"
#include "app/shell/screen.hpp"
#include "app/shell/screens.hpp"
#include "core/log.hpp"
#include "ui/i18n.hpp"
#include "ui/image_view.hpp"
#include "ui/reveal.hpp"
#include "ui/scroll.hpp"

namespace playfin::app {

namespace {

constexpr int kFilmography = 200;

struct PersonData {
    core::Result<jellyfin::Item> person = jellyfin::Item{};
    core::Result<jellyfin::ItemPage> films = jellyfin::ItemPage{};
    core::Result<jellyfin::ItemPage> series = jellyfin::ItemPage{};
};

// A person: portrait, name and biography, then their films and series on this server, newest first.
class PersonScreen : public ShellScreen {
public:
    PersonScreen(AppSession& session, jellyfin::Item person) : ShellScreen(session), person_(std::move(person)) {
        auto* scroll = add<ui::VScroll>();
        auto content = std::make_unique<ui::Box>(ui::Axis::Column);
        content->padding = pagePadding();
        content->gap = ui::theme().spacingL;
        column_ = content.get();
        auto* header = column_->add<ui::Box>(ui::Axis::Row);
        header->gap = ui::theme().spacingXl;
        portrait_ = header->add<ui::ImageView>();
        portrait_->width = ui::SizeSpec::fixed(240);
        portrait_->height = ui::SizeSpec::fixed(360);
        portrait_->cornerRadius = ui::theme().radius;
        portrait_->clip = true;
        portrait_->background = ui::theme().surface;
        auto* texts = header->add<ui::Box>(ui::Axis::Column);
        texts->gap = ui::theme().spacingM;
        auto* name = texts->add<ui::Label>(person_.name, ui::FontStyle{ui::theme().fontDisplay, ui::FontWeight::Bold});
        name->width = ui::SizeSpec::fixed(1300);
        name->maxLines = 2;
        overview_ = texts->add<ui::Label>("", ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        overview_->width = ui::SizeSpec::fixed(1300);
        overview_->maxLines = 8;
        overview_->color = ui::theme().textSecondary;
        overview_->visible = false;
        message_ = column_->add<ui::Label>("", ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        message_->visible = false;
        scroll->setContent(std::move(content));
        if (const auto client = session_.client()) {
            portrait_->setUrl(imageUrlFor(*client, person_, false, 240, 360));
        }
    }

    void onShow() override {
        if (!loaded_) {
            loaded_ = true;
            later([this] { load(); });
        }
    }

private:
    void load() {
        const auto client = session_.client();
        if (!client) {
            return;
        }
        const std::string id = person_.id;
        const core::CancelToken token = token_;
        run<PersonData>(
            [client, id, token] {
                PersonData data;
                data.person = client->item(id, token);
                jellyfin::ItemQuery query;
                query.personId = id;
                query.sortBy = "ProductionYear,SortName";
                query.descending = true;
                query.limit = kFilmography;
                query.includeTypes = {"Movie"};
                data.films = client->items(query, token);
                query.includeTypes = {"Series"};
                data.series = client->items(query, token);
                return data;
            },
            [this](PersonData data) { show(std::move(data)); });
    }

    void show(PersonData data) {
        if (data.person.ok()) {
            overview_->setText(data.person.value().overview);
            overview_->visible = !data.person.value().overview.empty();
        }
        const std::size_t firstRow = column_->children().size();
        AppSession* session = &session_;
        const auto open = [session](const jellyfin::Item& item) { openItem(*session, item); };
        bool any = false;
        if (data.films.ok() && !data.films.value().items.empty()) {
            addItemRow(*column_, ui::tr("shell.search.movies"), session_, data.films.value().items, false, open);
            any = true;
        }
        if (data.series.ok() && !data.series.value().items.empty()) {
            addItemRow(*column_, ui::tr("shell.search.series"), session_, data.series.value().items, false, open);
            any = true;
        }
        if (!any) {
            const bool failed = !data.films.ok() || !data.series.ok();
            if (failed) {
                PF_LOG_WARN("person: %s", (!data.films.ok() ? data.films.error() : data.series.error()).message.c_str());
            }
            message_->setText(failed ? errorText(!data.films.ok() ? data.films.error() : data.series.error())
                                     : ui::tr("shell.person.empty"));
            message_->visible = true;
        }
        column_->markDirty();
        ui::revealChildren(*column_, firstRow);
        focusIfEmpty();
    }

    jellyfin::Item person_;
    ui::Box* column_;
    ui::ImageView* portrait_;
    ui::Label* overview_;
    ui::Label* message_;
    bool loaded_ = false;
};

}  // namespace

std::unique_ptr<ui::Screen> makePerson(AppSession& session, jellyfin::Item person) {
    return std::make_unique<PersonScreen>(session, std::move(person));
}

}  // namespace playfin::app
