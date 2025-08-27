#include <complex>
#include <lbd/intp/interpreter.h>
#include <lbd/error.h>
#include <utility>

namespace intp::interp {
    std::ostream &operator<<(std::ostream &os, const Closure &closure) {
        // TODO: Improve this
        return os << "<closure " << closure.param << ">";
    }

    std::ostream &operator<<(std::ostream &os, const NativeFunction &native_fn) {
        // TODO: Improve this
        return os << "<native_fn " << native_fn.arity << ">";
    }

    std::ostream &operator<<(std::ostream &os, const Value &value) {
        std::visit([&]<typename T0>(T0 &&arg) {
            using T = std::decay_t<T0>;
            if constexpr (std::is_same_v<T, std::string>
                          || std::is_same_v<T, double>
                          || std::is_same_v<T, Closure>
                          || std::is_same_v<T, NativeFunction>) {
                os << arg;
            } else {
                STATIC_ASSERT_UNREACHABLE_T(T, "unhandled runtime value");
            }
        }, value);
        return os;
    }

    Thunk::Thunk(const fe::ast::Expression *expr, std::shared_ptr<Env> env) : expr(expr), env(std::move(env)) {
    }

    const Value &Thunk::force() const {
        if (cached.has_value()) {
            return cached.value();
        }
        // Expression is not initialized
        if (!expr) {
            std::cerr << "runtime error: forcing empty thunk";
            exit(EXIT_FAILURE);
        }
        cached = eval_expr(*expr, env);
        return cached.value();
    }

    void Thunk::set(const fe::ast::Expression *expr_, std::shared_ptr<Env> env_) {
        expr = expr_;
        env = std::move(env_);
        cached.reset();
    }

    Env::Env(std::shared_ptr<Env> parent) : parent(std::move(parent)) {
    }

    std::shared_ptr<Thunk> Env::lookup(const std::string &name) const {
        // Precedence: Local Environment > Global Environment
        if (const auto it = table.find(name); it != table.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookup(name);
        }
        return nullptr;
    }

    void Env::bind(const std::string &name, std::shared_ptr<Thunk> thunk) {
        table[name] = std::move(thunk);
    }

    static Value eval_iden_ast_node(const fe::ast::IdenAstNode &iden_ast_node, const std::shared_ptr<Env> &env) {
        const auto thunk = env->lookup(iden_ast_node.value);
        if (!thunk) {
            std::cerr << iden_ast_node.loc << ": runtime error: "
                    << "undefined identifier "
                    << iden_ast_node.value
                    << std::endl;
            exit(EXIT_FAILURE);
        }
        return thunk->force();
    }

    static Value eval_lambda_expr(const fe::ast::LambdaExpression &l_expr, const std::shared_ptr<Env> &env) {
        return Value(Closure{l_expr.arg.value, l_expr.expr.get(), env});
    }

    static Value eval_fn_apl(const fe::ast::FunctionApplication &fn_apl, const std::shared_ptr<Env> &env) {
        // Lookup the callee lazily
        const auto callee_thunk = env->lookup(fn_apl.fn_name.value);
        if (!callee_thunk) {
            std::cerr << fn_apl.loc << ": runtime error: "
                    << "undefined function "
                    << fn_apl.fn_name.value
                    << std::endl;
            exit(EXIT_FAILURE);
        }
        const Value fn_value = callee_thunk->force();
        std::vector<std::shared_ptr<Thunk> > arg_thunks;
        arg_thunks.reserve(fn_apl.args.size());
        for (const auto &arg: fn_apl.args) {
            arg_thunks.push_back(std::make_shared<Thunk>(arg.get(), env));
        }
        return apply_fn_apl(fn_value, arg_thunks, env);
    }

    Value eval_expr(const fe::ast::Expression &expr, std::shared_ptr<Env> env) {
        return std::visit([&]<typename T0>(T0 &&arg) {
            using T = std::decay_t<T0>;
            if constexpr (std::is_same_v<T, fe::ast::IdenAstNode>) {
                return eval_iden_ast_node(arg, env);
            } else if constexpr (std::is_same_v<T, fe::ast::StringAstNode> || std::is_same_v<T,
                                     fe::ast::FloatAstNode>) {
                return Value(arg.value);
            } else if constexpr (std::is_same_v<T, fe::ast::LambdaExpression>) {
                return eval_lambda_expr(arg, env);
            } else if constexpr (std::is_same_v<T, fe::ast::FunctionApplication>) {
                return eval_fn_apl(arg, env);
            } else {
                STATIC_ASSERT_UNREACHABLE_T(T, "unhandled expression");
            }
        }, expr.value);
    }

    Value apply_fn_apl(Value fn_value, const std::vector<std::shared_ptr<Thunk> > &args,
                       const std::shared_ptr<Env> &call_site_env) {
        Value current_fn = std::move(fn_value);
        size_t idx = 0;
        while (idx < args.size()) {
            // Apply Argument to Closure (Lambda Expression)
            if (std::holds_alternative<Closure>(current_fn)) {
                const auto [param, body, env] = std::get<Closure>(current_fn);
                auto child_env = std::make_shared<Env>(env);
                child_env->bind(param, args[idx]);
                // Evaluate body in child Environment
                Value result = eval_expr(*body, child_env);
                current_fn = std::move(result);
                ++idx;
                continue;
            }
            // Apply Argument to Native Function
            if (std::holds_alternative<NativeFunction>(current_fn)) {
                // Using by reference and not creating a copy of Native Function
                const auto &[arity, impl] = std::get<NativeFunction>(current_fn);
                if (args.size() - idx < arity) {
                    // TODO: Add Loc to one of the structures for better diagnostics
                    std::cerr << "runtime error: native function expects "
                            << arity << " argument(s), found " << args.size() - idx
                            << std::endl;
                    exit(EXIT_FAILURE);
                }
                std::vector<std::shared_ptr<Thunk> > slice;
                slice.reserve(arity);
                for (size_t i = 0; i < arity; ++i) {
                    slice.push_back(args[idx + i]);
                }
                Value result = impl(slice, call_site_env);
                current_fn = std::move(result);
                idx += arity;
                continue;
            }
            // TODO: Add Loc for better diagnostics
            std::cerr << "runtime error: trying to apply non-function value"
                    << std::endl;
            exit(EXIT_FAILURE);
        }
        return current_fn;
    }

    // Creates placeholder Thunk then set body so recursion can refer to it during lazy evaluation
    static void bind_def_ast_node_lazy(const fe::ast::DefAstNode &def_ast_node, const std::shared_ptr<Env> &env) {
        const auto thunk = std::make_shared<Thunk>();
        env->bind(def_ast_node.def_name.value, thunk);
        thunk->set(&def_ast_node.expr, env);
    }

    Result interpret(const fe::ast::Program &program) {
        const auto global_env = std::make_shared<Env>();
        install_builtins(global_env);
        for (const auto &[value]: program.nodes) {
            std::visit([&]<typename T0>(T0 &&arg) {
                using T = std::decay_t<T0>;
                if constexpr (std::is_same_v<T, fe::ast::Expression>) {
                    eval_expr(arg, global_env);
                    // The top level Value returned by this is not needed
                } else if constexpr (std::is_same_v<T, fe::ast::DefAstNode>) {
                    bind_def_ast_node_lazy(arg, global_env);
                } else {
                    STATIC_ASSERT_UNREACHABLE_T(T, "unhandled program node");
                }
            }, value);
        }
        return {global_env};
    }

    void install_builtins(const std::shared_ptr<Env> &env) {
        // Prints Argument to stdout and returns the same Argument as Value
        NativeFunction print = {
            1, [](const std::vector<std::shared_ptr<Thunk> > &args, const std::shared_ptr<Env> &) -> Value {
                const Value &value = args[0]->force();
                std::cout << value << std::endl;
                return value;
            }
        };
        NativeFunction add = {
            2, [](const std::vector<std::shared_ptr<Thunk> > &args, const std::shared_ptr<Env> &) -> Value {
                const Value &value1 = args[0]->force();
                const Value &value2 = args[1]->force();
                if (!std::holds_alternative<double>(value1) || !std::holds_alternative<double>(value2)) {
                    std::cerr << "runtime error: wrong arguments provided to native function add" << std::endl;
                    std::cerr << "add signature: Float -> Float -> Float" << std::endl;
                    exit(EXIT_FAILURE);
                }
                const double result = std::get<double>(value1) + std::get<double>(value2);
                return Value{result};
            }
        };
        NativeFunction mul = {
            2, [](const std::vector<std::shared_ptr<Thunk> > &args, const std::shared_ptr<Env> &) -> Value {
                const Value &value1 = args[0]->force();
                const Value &value2 = args[1]->force();
                if (!std::holds_alternative<double>(value1) || !std::holds_alternative<double>(value2)) {
                    std::cerr << "runtime error: wrong arguments provided to native function mul" << std::endl;
                    std::cerr << "mul signature: Float -> Float -> Float" << std::endl;
                    exit(EXIT_FAILURE);
                }
                const double result = std::get<double>(value1) * std::get<double>(value2);
                return Value{result};
            }
        };
        {
            const auto thunk = std::make_shared<Thunk>();
            thunk->cached = Value{print};
            env->bind("print", thunk);
        }
        {
            const auto thunk = std::make_shared<Thunk>();
            thunk->cached = Value{add};
            env->bind("add", thunk);
        }
        {
            const auto thunk = std::make_shared<Thunk>();
            thunk->cached = Value{mul};
            env->bind("mul", thunk);
        }
    }
}
