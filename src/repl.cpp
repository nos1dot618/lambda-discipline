#include <iostream>
#include <lbd/repl.h>

#include "lbd/fe/lexer.h"
#include "lbd/fe/parser.h"
#include "lbd/intp/interpreter.h"

namespace repl {
    void loop(const bool debug) {
        std::cout << "Lambda Discipline REPL. Type :quit to exit." << std::endl;
        std::string line;
        std::optional<std::shared_ptr<intp::interp::Env> > shared_global_env = std::nullopt;
        while (true) {
            std::cout << "lbd> ";
            if (!std::getline(std::cin, line)) {
                break; // EOF (C-d / C-z)
            }
            if (line == ":quit" || line == ":q" || line == ":exit") {
                std::cout << "Exiting REPL...\n";
                break;
            }
            if (line.empty()) {
                continue;
            }
            try {
                // Lex
                fe::lexer::Lexer lexer(line, fe::lexer::FromRepl{});
                const std::vector<fe::token::Token> tokens = lexer.lex_all();
                if (debug) {
                    for (const auto &tok: tokens) {
                        std::cout << tok << "\n";
                    }
                }
                // Parse
                fe::parser::Parser parser(tokens);
                if (debug) {
                    std::cout << parser.program << "\n";
                }
                // Interpret
                const auto [global_env, value] = intp::interp::interpret(parser.program, shared_global_env);
                std::cout << "=> " << value << "\n";
                shared_global_env = global_env;
            } catch (const std::exception &ex) {
                std::cerr << "error: " << ex.what() << "\n";
            }
        }
    }
}
