#include <filesystem>
#include <fstream>
#include <iostream>
#include <lbd/repl.h>
#include <lbd/fe/lexer.h>
#include <lbd/fe/parser.h>
#include <lbd/intp/interpreter.h>

namespace repl {
    static bool force_on_env_dump = false;
    static bool debug_enabled = false;

    static std::optional<std::string> read_file(const std::string &filepath) {
        std::ifstream file(filepath);
        if (!file) {
            // Could not open file
            return std::nullopt;
        }
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    static void process_load_command(const std::string &arg,
                                     std::optional<std::shared_ptr<intp::interp::Env> > &shared_env) {
        try {
            const std::string filepath = arg;
            if (!std::filesystem::exists(filepath)) {
                std::cerr << "error: filepath "
                        << filepath << " does not exist"
                        << std::endl;
                return;
            }
            fe::lexer::Lexer lexer(filepath, fe::lexer::FromFile{});
            const auto tokens = lexer.lex_all();
            // TODO: Accept Options and Add a debug flag for printing Lexed Tokens
            fe::parser::Parser parser(tokens);
            const std::optional<std::shared_ptr<intp::interp::Env> > temp_env = shared_env;
            // Merge loaded_env into shared_env
            if (const auto [loaded_env, _] = intp::interp::interpret(parser.program, temp_env, {.own_expr = true});
                loaded_env) {
                if (!shared_env) {
                    shared_env = loaded_env;
                } else {
                    // Merge all Bindings from loaded_env into shared_env
                    for (const auto &[fst, snd]: loaded_env->table) {
                        (*shared_env)->bind(fst, snd);
                    }
                }
            }
            std::cout << "info: file loaded: " << filepath << std::endl;
        } catch (const std::exception &ex) {
            std::cerr << "error: could not load file: " << ex.what() << std::endl;
        }
    }

    static void help() {
        std::cout
                << "available REPL commands:\n"
                << "  general:\n"
                << "    :q, :quit, :exit                  Exit the REPL\n"
                << "    :c, :clear, :cls                  Clear the screen\n"
                << "    :h, :help, :?                     Show this help message\n"
                << "    :l <filepath>, :load <filepath>   Load file into REPL\n"
                << "    :r, :reload                       Reset environment (forget all bindings)\n"
                << "    :d, :debug                        Toggle debug mode\n"
                << "  inspection:\n"
                << "    :e, :env                          Show all variables in current environment\n"
                << "    :force                            Toggle forcing evaluation when dumping env\n"
                << "current options:\n"
                << "    debug                             [" << (debug_enabled ? "on " : "off") <<
                "]   (use :debug to toggle)\n"
                << "    force-on-env-dump                 [" << (force_on_env_dump ? "on " : "off") <<
                "]   (use :force to toggle)"
                << std::endl;
    }

    void loop(const bool debug) {
        debug_enabled = debug;
        std::cout << "lambda-discipline REPL.\ntype :quit to exit." << std::endl;
        std::string line;
        std::optional<std::shared_ptr<intp::interp::Env> > shared_global_env = std::nullopt;
        while (true) {
            std::cout << "lbd> ";
            if (!std::getline(std::cin, line)) {
                break; // EOF (C-d / C-z)
            }
            if (line == ":q" || line == ":quit" || line == ":exit") {
                std::cout << "exiting REPL" << std::endl;
                break;
            }
            if (line == ":c" || line == ":clear" || line == ":cls") {
#ifdef _WIN32
                std::system("cls");
#else
                std::system("clear");
#endif
                continue;
            }
            if (line == ":h" || line == ":help" || line == ":?") {
                help();
                continue;
            }
            if (line == ":env" || line == ":e") {
                if (shared_global_env) {
                    std::cout << "info: environment bindings:" << std::endl;
                    (*shared_global_env)->dump(std::cout, force_on_env_dump);
                } else {
                    std::cout << "info: environment is empty" << std::endl;
                }
                continue;
            }
            if (line == ":reload" || line == ":r") {
                shared_global_env.reset();
                std::cout << "info: environment reloaded" << std::endl;
                continue;
            }
            if (line == ":debug") {
                debug_enabled = !debug_enabled;
                continue;
            }
            if (line == ":force") {
                force_on_env_dump = !force_on_env_dump;
                continue;
            }
            if (line.rfind(":load ", 0) == 0) {
                process_load_command(line.substr(6), shared_global_env);
                continue;
            }
            if (line.rfind(":l ", 0) == 0) {
                process_load_command(line.substr(3), shared_global_env);
                continue;
            }
            if (line.empty()) {
                continue;
            }
            try {
                // TODO: In case of error do not EXIT from the program
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
                const auto [global_env, value] = intp::interp::interpret(
                    parser.program, shared_global_env, {.own_expr = true});
                std::cout << "===> " << value << "\n";
                shared_global_env = global_env;
            } catch (const std::exception &ex) {
                std::cerr << "error: " << ex.what() << "\n";
            }
        }
    }
}
