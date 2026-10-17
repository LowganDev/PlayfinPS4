# Strict warnings for Playfin's own targets (third-party code is built without them).
function(playfin_set_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)
endfunction()
