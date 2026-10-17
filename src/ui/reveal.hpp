#pragma once

#include <cstddef>

#include "ui/view.hpp"

namespace playfin::ui {

// The children of `parent` from index `first` arrive one after the other: each fades in and rises 30 px, 35 ms
// after the previous one (the first 12; later ones show at once). Nothing when transitions are off.
void revealChildren(View& parent, std::size_t first);

}  // namespace playfin::ui
