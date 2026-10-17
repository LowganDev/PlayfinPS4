#pragma once

#include "jellyfin/client.hpp"
#include "jellyfin/models.hpp"

namespace playfin::app {

enum class LibrarySort { Name, DateAdded, Year, Rating };
enum class LibraryFilter { All, Unplayed, Favorites };

// How a library is listed; kept per library for the session.
struct LibraryChoice {
    LibrarySort sort = LibrarySort::Name;
    bool descending = false;
    LibraryFilter filter = LibraryFilter::All;
};

// One page (100 items from `startIndex`) of `library` listed as chosen.
jellyfin::ItemQuery libraryQuery(const jellyfin::Item& library, const LibraryChoice& choice, int startIndex);

// The following sort (Name, Date added, Year, Rating, then Name again) in its natural order: names ascending, the
// others newest or best first. The filter is kept.
LibraryChoice nextSort(LibraryChoice choice);
LibraryFilter nextFilter(LibraryFilter filter);

}  // namespace playfin::app
