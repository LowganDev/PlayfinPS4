#include "ui/context.hpp"

namespace playfin::ui {

Context& context() {
    static Context instance;
    return instance;
}

}  // namespace playfin::ui
