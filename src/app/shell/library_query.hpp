#pragma once

#include <string>
#include <vector>

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

// The libraries in the user's order (ids in `order`), the ones it does not name after them in the server's order.
std::vector<jellyfin::Item> orderLibraries(std::vector<jellyfin::Item> libraries, const std::vector<std::string>& order);

}  // namespace playfin::app
