#pragma once

#include <memory>
#include <string>

#include "core/result.hpp"
#include "ui/application.hpp"

namespace playfin::app {

// Toolkit showcase used by the screenshot tests (developer screen, not translated).
std::unique_ptr<ui::Screen> makeGallery();
// Images of the gallery: "gallery://<n>" gives a generated PNG (no network).
core::Result<std::string> galleryImage(const std::string& url);

}  // namespace playfin::app
