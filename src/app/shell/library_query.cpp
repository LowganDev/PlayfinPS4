#include "app/shell/library_query.hpp"

namespace playfin::app {

namespace {

constexpr int kPage = 100;

std::vector<std::string> typesFor(const std::string& collectionType) {
    if (collectionType == "movies") {
        return {"Movie"};
    }
    if (collectionType == "tvshows") {
        return {"Series"};
    }
    if (collectionType == "boxsets") {
        return {"BoxSet"};
    }
    if (collectionType == "music") {
        return {"MusicAlbum"};
    }
    return {};
}

const char* sortByFor(LibrarySort sort) {
    switch (sort) {
    case LibrarySort::Name:
        return "SortName";
    case LibrarySort::DateAdded:
        return "DateCreated,SortName";
    case LibrarySort::Year:
        return "ProductionYear,SortName";
    case LibrarySort::Rating:
        return "CommunityRating,SortName";
    }
    return "SortName";
}

}  // namespace

jellyfin::ItemQuery libraryQuery(const jellyfin::Item& library, const LibraryChoice& choice, int startIndex) {
    jellyfin::ItemQuery query;
    query.parentId = library.id;
    query.includeTypes = typesFor(library.collectionType);
    query.recursive = !query.includeTypes.empty();
    query.sortBy = sortByFor(choice.sort);
    query.descending = choice.descending;
    if (choice.filter == LibraryFilter::Unplayed) {
        query.filters = {"IsUnplayed"};
    } else if (choice.filter == LibraryFilter::Favorites) {
        query.filters = {"IsFavorite"};
    }
    query.startIndex = startIndex;
    query.limit = kPage;
    return query;
}

LibraryChoice nextSort(LibraryChoice choice) {
    switch (choice.sort) {
    case LibrarySort::Name:
        choice.sort = LibrarySort::DateAdded;
        break;
    case LibrarySort::DateAdded:
        choice.sort = LibrarySort::Year;
        break;
    case LibrarySort::Year:
        choice.sort = LibrarySort::Rating;
        break;
    case LibrarySort::Rating:
        choice.sort = LibrarySort::Name;
        break;
    }
    choice.descending = choice.sort != LibrarySort::Name;
    return choice;
}

LibraryFilter nextFilter(LibraryFilter filter) {
    switch (filter) {
    case LibraryFilter::All:
        return LibraryFilter::Unplayed;
    case LibraryFilter::Unplayed:
        return LibraryFilter::Favorites;
    case LibraryFilter::Favorites:
        return LibraryFilter::All;
    }
    return LibraryFilter::All;
}

}  // namespace playfin::app
