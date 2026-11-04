#include "baselines.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <numeric>

#include "adaptogen/analysis.hpp"

namespace adaptogen::bench {

namespace {

using clock_type = std::chrono::steady_clock;
double since(clock_type::time_point t0) { return std::chrono::duration<double>(clock_type::now() - t0).count(); }

AlgoResult finish_program(const Problem& problem, const MachineSpec& spec, const Program& p, long evals,
                          clock_type::time_point t0) {
    AlgoResult r;
    const auto te = problem.metrics(spec, p, Split::Test);
    r.test_primary = te.empty() ? 0 : te[0].value;
    r.solved_test = problem.solved(spec, p, Split::Test);
    r.program_evaluations = evals;
    r.seconds = since(t0);
    r.size = static_cast<double>(effective_length(spec, p, problem.num_outputs()));
    return r;
}

AlgoResult finish_predictions(const FormulaProblem& problem, const std::function<double(const std::vector<double>&)>& f,
                              long evals, double size, clock_type::time_point t0) {
    AlgoResult r;
    const Dataset& d = problem.data(Split::Test);
    std::vector<double> pred(d.size());
    for (std::size_t i = 0; i < d.size(); ++i) pred[i] = f(d.x[i]);
    const auto m = regression_metrics(d.y, pred);
    r.test_primary = m.r2;
    r.solved_test = m.nrmse < FormulaProblem::kSolvedNrmse;
    r.program_evaluations = evals;
    r.size = size;
    r.seconds = since(t0);
    return r;
}

}  // namespace

AlgoResult run_adaptogen(const Problem& problem, const EngineConfig& cfg) {
    const auto t0 = clock_type::now();
    Engine engine(problem, cfg);
    const auto res = engine.run();
    return finish_program(problem, engine.machine(), res.best, res.program_evaluations, t0);
}

AlgoResult run_random_search(const Problem& problem, const EngineConfig& cfg) {
    const auto t0 = clock_type::now();
    const MachineSpec spec = make_machine(problem, cfg);
    Rng rng(cfg.seed);
    const long budget = static_cast<long>(cfg.population) * cfg.generations;
    Program best;
    double best_fit = -1e300;
    long evals = 0;
    VariationConfig v = cfg.variation;
    v.init_max_length = std::max(v.init_max_length, 40);
    while (evals < budget) {
        Program p = random_program(spec, v, rng);
        const double f = problem.mean_score(spec, p, Split::Train);
        ++evals;
        if (f > best_fit) {
            best_fit = f;
            best = p;
            if (cfg.stop_when_solved && problem.solved(spec, best, Split::Train)) break;
        }
    }
    return finish_program(problem, spec, best, evals, t0);
}

AlgoResult run_hill_climber(const Problem& problem, const EngineConfig& cfg) {
    const auto t0 = clock_type::now();
    const MachineSpec spec = make_machine(problem, cfg);
    Rng rng(cfg.seed);
    const long budget = static_cast<long>(cfg.population) * cfg.generations;
    const long patience = 2000;
    Program best, cur;
    double best_fit = -1e300, cur_fit = -1e300;
    long evals = 0, stale = patience;
    while (evals < budget) {
        if (stale >= patience) {  // restart
            cur = random_program(spec, cfg.variation, rng);
            cur_fit = problem.mean_score(spec, cur, Split::Train);
            ++evals;
            stale = 0;
        }
        Program child = cur;
        const int k = rng.range(1, 3);
        for (int i = 0; i < k; ++i) mutate(child, spec, cfg.variation, rng, problem.num_outputs());
        const double f = problem.mean_score(spec, child, Split::Train);
        ++evals;
        ++stale;
        if (f >= cur_fit) {
            if (f > cur_fit) stale = 0;
            cur = std::move(child);
            cur_fit = f;
        }
        if (cur_fit > best_fit) {
            best_fit = cur_fit;
            best = cur;
            if (cfg.stop_when_solved && problem.solved(spec, best, Split::Train)) break;
        }
    }
    return finish_program(problem, spec, best, evals, t0);
}

// ---------------------------------------------------------------- tree GP

namespace {

enum class T : std::uint8_t { Add, Sub, Mul, Div, Sin, Cos, Exp, Log, Var, Const };
struct Node {
    T op;
    std::uint8_t var = 0;
    double c = 0;
};
using Tree = std::vector<Node>;  // prefix order

int arity(T op) {
    switch (op) {
        case T::Add: case T::Sub: case T::Mul: case T::Div: return 2;
        case T::Sin: case T::Cos: case T::Exp: case T::Log: return 1;
        default: return 0;
    }
}

std::size_t subtree_end(const Tree& t, std::size_t i) {
    int need = 1;
    while (need > 0) need += arity(t[i++].op) - 1;
    return i;
}

int depth_at(const Tree& t, std::size_t& i) {
    const int a = arity(t[i++].op);
    int d = 0;
    for (int k = 0; k < a; ++k) d = std::max(d, depth_at(t, i));
    return d + 1;
}

int depth(const Tree& t) {
    std::size_t i = 0;
    return depth_at(t, i);
}

double clean(double v) { return std::isnan(v) ? 0.0 : std::clamp(v, -1e15, 1e15); }

double eval(const Tree& t, std::size_t& i, const std::vector<double>& x) {
    const Node& n = t[i++];
    switch (n.op) {
        case T::Var: return x[n.var];
        case T::Const: return n.c;
        case T::Add: { double a = eval(t, i, x); return clean(a + eval(t, i, x)); }
        case T::Sub: { double a = eval(t, i, x); return clean(a - eval(t, i, x)); }
        case T::Mul: { double a = eval(t, i, x); return clean(a * eval(t, i, x)); }
        case T::Div: { double a = eval(t, i, x), b = eval(t, i, x); return std::fabs(b) < 1e-9 ? 1.0 : clean(a / b); }
        case T::Sin: return std::sin(eval(t, i, x));
        case T::Cos: return std::cos(eval(t, i, x));
        case T::Exp: return clean(std::exp(std::min(eval(t, i, x), 50.0)));
        case T::Log: { double a = eval(t, i, x); return std::fabs(a) < 1e-9 ? 0.0 : std::log(std::fabs(a)); }
    }
    return 0;
}

double predict(const Tree& t, const std::vector<double>& x) {
    std::size_t i = 0;
    return eval(t, i, x);
}

struct TreeGen {
    Rng& rng;
    int nvars;
    double cmin, cmax;
    Node terminal() {
        if (rng.chance(0.5)) return {T::Var, static_cast<std::uint8_t>(rng.below(static_cast<std::uint64_t>(nvars))), 0};
        return {T::Const, 0, rng.uniform(cmin, cmax)};
    }
    Node function() { return {static_cast<T>(rng.below(8)), 0, 0}; }
    void build(Tree& t, int d, int max_d, bool full) {
        const bool leaf = d >= max_d || (!full && d > 0 && rng.chance(0.3));
        if (leaf) {
            t.push_back(terminal());
            return;
        }
        const Node f = function();
        t.push_back(f);
        for (int k = 0; k < arity(f.op); ++k) build(t, d + 1, max_d, full);
    }
};

std::size_t pick_point(const Tree& t, Rng& rng) {  // Koza: 90% internal nodes
    std::vector<std::size_t> internal, leaves;
    for (std::size_t i = 0; i < t.size(); ++i) (arity(t[i].op) ? internal : leaves).push_back(i);
    if (!internal.empty() && rng.chance(0.9)) return internal[rng.below(internal.size())];
    return leaves[rng.below(leaves.size())];
}

}  // namespace

AlgoResult run_tree_gp(const FormulaProblem& problem, const EngineConfig& cfg) {
    const auto t0 = clock_type::now();
    Rng rng(cfg.seed);
    const Dataset& train = problem.data(Split::Train);
    const int nvars = static_cast<int>(train.variables.size());
    TreeGen gen{rng, nvars, cfg.const_min, cfg.const_max};
    const std::size_t P = static_cast<std::size_t>(cfg.population);
    const int max_depth = 17, tournament = 7;

    auto fitness = [&](const Tree& t) {
        double s = 0;
        for (std::size_t i = 0; i < train.size(); ++i) {
            const double e = std::fabs(train.y[i] - predict(t, train.x[i]));
            s -= std::isfinite(e) ? std::min(e, 1e12) : 1e12;
        }
        return s / static_cast<double>(train.size());
    };
    auto train_nrmse = [&](const Tree& t) {
        std::vector<double> pred(train.size());
        for (std::size_t i = 0; i < train.size(); ++i) pred[i] = predict(t, train.x[i]);
        return regression_metrics(train.y, pred).nrmse;
    };

    std::vector<Tree> pop(P);
    std::vector<double> fit(P);
    for (std::size_t i = 0; i < P; ++i) {  // ramped half-and-half, depths 2..6
        const int d = 2 + static_cast<int>(i % 5);
        gen.build(pop[i], 0, d, i % 2 == 0);
    }
    long evals = 0;
    Tree best;
    for (int g = 0;; ++g) {
        std::size_t bi = 0;
        for (std::size_t i = 0; i < P; ++i) {
            fit[i] = fitness(pop[i]);
            if (fit[i] > fit[bi]) bi = i;
        }
        evals += static_cast<long>(g == 0 ? P : P - 1);
        best = pop[bi];
        if ((cfg.stop_when_solved && train_nrmse(best) < FormulaProblem::kSolvedNrmse) || g + 1 >= cfg.generations) break;
        auto select = [&]() -> const Tree& {
            std::size_t b = rng.below(P);
            for (int k = 1; k < tournament; ++k) {
                std::size_t j = rng.below(P);
                if (fit[j] > fit[b]) b = j;
            }
            return pop[b];
        };
        std::vector<Tree> next;
        next.reserve(P);
        next.push_back(best);  // elitism 1
        while (next.size() < P) {
            const Tree& a = select();
            Tree child;
            const std::size_t pa = pick_point(a, rng), ea = subtree_end(a, pa);
            if (rng.chance(0.9)) {  // subtree crossover
                const Tree& b = select();
                const std::size_t pb = pick_point(b, rng), eb = subtree_end(b, pb);
                child.assign(a.begin(), a.begin() + static_cast<long>(pa));
                child.insert(child.end(), b.begin() + static_cast<long>(pb), b.begin() + static_cast<long>(eb));
                child.insert(child.end(), a.begin() + static_cast<long>(ea), a.end());
            } else {  // subtree mutation
                Tree sub;
                gen.build(sub, 0, rng.range(1, 4), false);
                child.assign(a.begin(), a.begin() + static_cast<long>(pa));
                child.insert(child.end(), sub.begin(), sub.end());
                child.insert(child.end(), a.begin() + static_cast<long>(ea), a.end());
            }
            if (depth(child) > max_depth) child = a;
            next.push_back(std::move(child));
        }
        pop.swap(next);
    }
    return finish_predictions(problem, [&](const std::vector<double>& x) { return predict(best, x); }, evals,
                              static_cast<double>(best.size()), t0);
}

// ---------------------------------------------------------------- polynomial least squares

namespace {

struct Poly {
    std::vector<std::vector<int>> powers;  // monomial exponents per variable
    std::vector<double> lo, hi, w;
    std::vector<double> features(const std::vector<double>& x) const {
        std::vector<double> f(powers.size(), 1.0);
        for (std::size_t m = 0; m < powers.size(); ++m)
            for (std::size_t v = 0; v < x.size(); ++v) {
                const double s = hi[v] > lo[v] ? 2 * (x[v] - lo[v]) / (hi[v] - lo[v]) - 1 : 0;
                f[m] *= std::pow(s, powers[m][v]);
            }
        return f;
    }
    double operator()(const std::vector<double>& x) const {
        const auto f = features(x);
        double s = 0;
        for (std::size_t m = 0; m < f.size(); ++m) s += w[m] * f[m];
        return s;
    }
};

void monomials(int nvars, int degree, std::vector<int>& cur, std::size_t v, int left, std::vector<std::vector<int>>& out) {
    if (v == static_cast<std::size_t>(nvars)) {
        out.push_back(cur);
        return;
    }
    for (int p = 0; p <= left; ++p) {
        cur[v] = p;
        monomials(nvars, degree, cur, v + 1, left - p, out);
    }
}

bool fit(Poly& poly, const std::vector<std::vector<double>>& X, const std::vector<double>& y) {
    const std::size_t m = poly.powers.size();
    std::vector<double> A(m * m, 0.0), b(m, 0.0);
    for (std::size_t i = 0; i < X.size(); ++i) {
        const auto f = poly.features(X[i]);
        for (std::size_t r = 0; r < m; ++r) {
            b[r] += f[r] * y[i];
            for (std::size_t c = 0; c < m; ++c) A[r * m + c] += f[r] * f[c];
        }
    }
    for (std::size_t r = 0; r < m; ++r) A[r * m + r] += 1e-10 * (1 + A[r * m + r]);
    for (std::size_t c = 0; c < m; ++c) {  // Gaussian elimination, partial pivoting
        std::size_t piv = c;
        for (std::size_t r = c + 1; r < m; ++r)
            if (std::fabs(A[r * m + c]) > std::fabs(A[piv * m + c])) piv = r;
        if (std::fabs(A[piv * m + c]) < 1e-300) return false;
        for (std::size_t k = 0; k < m; ++k) std::swap(A[c * m + k], A[piv * m + k]);
        std::swap(b[c], b[piv]);
        for (std::size_t r = c + 1; r < m; ++r) {
            const double f = A[r * m + c] / A[c * m + c];
            for (std::size_t k = c; k < m; ++k) A[r * m + k] -= f * A[c * m + k];
            b[r] -= f * b[c];
        }
    }
    poly.w.assign(m, 0.0);
    for (std::size_t c = m; c-- > 0;) {
        double s = b[c];
        for (std::size_t k = c + 1; k < m; ++k) s -= A[c * m + k] * poly.w[k];
        poly.w[c] = s / A[c * m + c];
    }
    return true;
}

}  // namespace

AlgoResult run_polyfit(const FormulaProblem& problem, const EngineConfig& cfg) {
    const auto t0 = clock_type::now();
    const Dataset& train = problem.data(Split::Train);
    const int nvars = static_cast<int>(train.variables.size());
    Rng rng(cfg.seed);
    std::vector<std::size_t> idx(train.size());
    std::iota(idx.begin(), idx.end(), 0);
    for (std::size_t i = idx.size(); i > 1; --i) std::swap(idx[i - 1], idx[rng.below(i)]);
    const std::size_t n_val = std::max<std::size_t>(1, train.size() / 4);
    std::vector<std::vector<double>> Xf, Xv;
    std::vector<double> yf, yv;
    for (std::size_t k = 0; k < idx.size(); ++k) {
        (k < n_val ? Xv : Xf).push_back(train.x[idx[k]]);
        (k < n_val ? yv : yf).push_back(train.y[idx[k]]);
    }
    auto make = [&](int degree) {
        Poly p;
        std::vector<int> cur(static_cast<std::size_t>(nvars));
        monomials(nvars, degree, cur, 0, degree, p.powers);
        p.lo.assign(static_cast<std::size_t>(nvars), 1e300);
        p.hi.assign(static_cast<std::size_t>(nvars), -1e300);
        for (const auto& x : train.x)
            for (int v = 0; v < nvars; ++v) {
                p.lo[v] = std::min(p.lo[v], x[v]);
                p.hi[v] = std::max(p.hi[v], x[v]);
            }
        return p;
    };
    int best_deg = 1;
    double best_err = 1e300;
    for (int d = 1; d <= (nvars == 1 ? 12 : 10); ++d) {
        Poly p = make(d);
        if (p.powers.size() >= yf.size() || !fit(p, Xf, yf)) break;
        std::vector<double> pred;
        for (const auto& x : Xv) pred.push_back(p(x));
        const double err = regression_metrics(yv, pred).rmse;
        if (err < best_err) {
            best_err = err;
            best_deg = d;
        }
    }
    Poly p = make(best_deg);
    fit(p, train.x, train.y);
    return finish_predictions(problem, [&](const std::vector<double>& x) { return p(x); }, 1,
                              static_cast<double>(p.powers.size()), t0);
}

}  // namespace adaptogen::bench
