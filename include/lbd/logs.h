#pragma once

#include <optional>
#include <lbd/fe/loc.h>
#include <lbd/utils/term.h>
#include <lbd/exceptions.h>

namespace logs {
    class Logger {
        bool exit_on_error = true;
        bool use_color = false;
        bool show_loc = true;

    public:
        Logger() = default;

        Logger(const bool exit_on_error, const bool use_color, const bool show_loc) : exit_on_error(exit_on_error),
            use_color(use_color), show_loc(show_loc) {
            if (use_color) {
                enable_virtual_terminal();
            }
        }

        template<typename... Args>
        [[noreturn]] void error(const std::optional<fe::loc::Loc> &loc, Args &&... args) const {
            if (use_color) {
                std::cerr << colors::RED;
            }
            if (show_loc && loc.has_value()) {
                std::cerr << loc.value() << ": ";
            }
            (std::cerr << ... << std::forward<Args>(args));
            if (use_color) {
                std::cerr << colors::RESET;
            }
            std::cerr << std::endl;
            if (exit_on_error) {
                exit(EXIT_FAILURE);
            }
            throw ControlledExit{};
        }
    };
}
