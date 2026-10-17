#include <doctest/doctest.h>

#include "app/shell/library_query.hpp"

using namespace playfin;
using app::LibraryChoice;
using app::LibraryFilter;
using app::LibrarySort;

namespace {
jellyfin::Item movies() {
    jellyfin::Item library;
    library.id = "lib-movies";
    library.collectionType = "movies";
    return library;
}
}  // namespace

TEST_CASE("library sorts map to Jellyfin's sortBy") {
    LibraryChoice choice;
    CHECK(app::libraryQuery(movies(), choice, 0).sortBy == "SortName");
    choice.sort = LibrarySort::DateAdded;
    CHECK(app::libraryQuery(movies(), choice, 0).sortBy == "DateCreated,SortName");
    choice.sort = LibrarySort::Year;
    CHECK(app::libraryQuery(movies(), choice, 0).sortBy == "ProductionYear,SortName");
    choice.sort = LibrarySort::Rating;
    choice.descending = true;
    const jellyfin::ItemQuery query = app::libraryQuery(movies(), choice, 200);
    CHECK(query.sortBy == "CommunityRating,SortName");
    CHECK(query.descending);
    CHECK(query.startIndex == 200);
    CHECK(query.limit == 100);
    CHECK(query.parentId == "lib-movies");
    CHECK(query.includeTypes == std::vector<std::string>{"Movie"});
    CHECK(query.recursive);
}

TEST_CASE("library filters map to Jellyfin's filters") {
    LibraryChoice choice;
    CHECK(app::libraryQuery(movies(), choice, 0).filters.empty());
    choice.filter = LibraryFilter::Unplayed;
    CHECK(app::libraryQuery(movies(), choice, 0).filters == std::vector<std::string>{"IsUnplayed"});
    choice.filter = LibraryFilter::Favorites;
    CHECK(app::libraryQuery(movies(), choice, 0).filters == std::vector<std::string>{"IsFavorite"});
}

TEST_CASE("a folder without a known collection type lists its direct children") {
    jellyfin::Item folder;
    folder.id = "f-1";
    const jellyfin::ItemQuery query = app::libraryQuery(folder, LibraryChoice{}, 0);
    CHECK(query.includeTypes.empty());
    CHECK_FALSE(query.recursive);
}

TEST_CASE("the next sort cycles and takes its natural order") {
    LibraryChoice choice;
    choice = app::nextSort(choice);
    CHECK(choice.sort == LibrarySort::DateAdded);
    CHECK(choice.descending);
    choice = app::nextSort(choice);
    CHECK(choice.sort == LibrarySort::Year);
    CHECK(choice.descending);
    choice = app::nextSort(choice);
    CHECK(choice.sort == LibrarySort::Rating);
    choice.filter = LibraryFilter::Favorites;
    choice = app::nextSort(choice);
    CHECK(choice.sort == LibrarySort::Name);
    CHECK_FALSE(choice.descending);
    CHECK(choice.filter == LibraryFilter::Favorites);  // kept
    CHECK(app::nextFilter(LibraryFilter::All) == LibraryFilter::Unplayed);
    CHECK(app::nextFilter(LibraryFilter::Unplayed) == LibraryFilter::Favorites);
    CHECK(app::nextFilter(LibraryFilter::Favorites) == LibraryFilter::All);
}
