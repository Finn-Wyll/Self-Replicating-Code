# adaptogen

**Evolve programs that solve problems, from very little prior knowledge.**

adaptogen is inspired by how bacteria become resistant to antibiotics: replication with copy
errors, selection by the environment, and (in later stages) gene duplication, horizontal gene
transfer and stress-induced mutation. Unlike most evolutionary algorithms, it does not just tune
parameters. It evolves actual **code**: programs for a small virtual machine that can grow new
instructions, branches and loops.

You give it only:
- **what can be observed** (sensor channels),
- **what can be done** (action channels),
- **how good an outcome was** (a score per test case).

It never gets told *how* to solve the problem. Evolution has to find that out.

> Status: **stage 2 of 8** (core engine + bacteria-inspired mechanisms). See [DESIGN.md](DESIGN.md) for the full plan:
> bacterial mechanisms, the strain bank where saved programs become base classes, generated
> C++ plug-ins, the MCP server for Claude, and more.

## Build

Requires a C++20 compiler (GCC 11+, Clang 14+, Apple Clang 15+) and CMake 3.20+. The first build
downloads [nlohmann/json](https://github.com/nlohmann/json).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

```bash
./build/adaptogen-tests
```

## Quick start

Rediscover Kepler's third law from real planet data (`examples/data/kepler.csv`):

```bash
./build/adaptogen evolve --problem formula --param data=examples/data/kepler.csv --population 500 --generations 150 --selection tournament --set length_cost=0.02 --seed 3
```

```
SOLVED after 21 generations (10480 program evaluations, 0.0s)
  train: r2=0.999999 ...
  test (unseen): r2=0.999998 ...  [solved]

evolved program (26 instructions, 3 effective):
# all registers start at 0; output: r0
r4 = planet_distance_au
r7 = sqrt(|r4|)
r0 = r4 * r7
```

That's `period = distance × √distance`, i.e. T² = a³. The other 23 instructions are
non-coding "junk DNA" that has no effect (shown with `--all`). It is raw material that later
mutations can turn into something new.

Other commands:

```bash
./build/adaptogen list                       # problems and their parameters
./build/adaptogen config                     # default engine settings as JSON
./build/adaptogen evolve --problem formula --param target=nguyen7 --out best.json --csv stats.csv
./build/adaptogen show --program best.json   # re-score and print a saved program
./build/adaptogen evolve --problem formula --param target=koza1 --json   # one JSON event per line
```

List algorithms (the program must discover iteration; it is tested on lists 4x longer than
it trained on):

```bash
./build/adaptogen evolve --problem list --param task=sum
```

Your own data: any CSV with a header row works (`--param data=file.csv`). The last column is the
target unless you pass `--param target_column=NAME`.

## Concepts

| Term | Meaning | Biology |
|---|---|---|
| Program | Linear list of VM instructions with real-number registers, `if`, bounded `repeat` loops | Genome |
| Channel | Named, typed thing a program can sense or do (`item.duration`, `place(i, t)`) | Receptors / effectors |
| Case | One independent evaluation (data point, scenario, episode) | One exposure |
| Train / test | Evolution only sees train cases; test cases measure generalisation | New environment |
| Effective code | Instructions that influence the output | Coding DNA |
| Introns | Instructions without effect | Non-coding DNA |
| `length_cost` / `time_cost` | Fitness penalty for size / runtime | Cost of resistance |

Every instruction sequence is a valid program: division, `log` and `exp` are protected, loops are
capped and every run has a step limit. So mutation can never "break" a program; it can only
change what it does.

**Selection.** `lexicase` (default) picks parents that excel on randomly ordered individual cases,
which keeps specialists alive as stepping stones. `tournament` compares overall average scores.

## Bacteria-inspired mechanisms

Each mechanism can be switched on independently in the config's `"mechanisms"` section
(`"stress": true` is shorthand for `{"enabled": true}`). [examples/bacterial.json](examples/bacterial.json)
turns them all on and disables crossover, giving asexual reproduction plus horizontal transfer
like real bacteria.

```bash
./build/adaptogen evolve --problem formula --param target=nguyen7 --config examples/bacterial.json --csv run.csv
```

| Mechanism | Biology | What the engine does |
|---|---|---|
| `stress` | SOS response | A lineage that has not improved for `patience` generations gets up to `max_multiplier`× more mutations |
| `hypermutation` | Mutator strains | Every program carries a heritable mutation rate that itself mutates (log-normal steps) |
| `transfer` | Plasmids / conjugation | Offspring receive a short code segment (1..`max_segment`) from a random neighbour |
| `duplication` | Gene duplication | A code segment is copied in place; the copies can then diverge |
| `spatial` | Colonies on a plate | Population on a torus; offspring compete with the cell they would replace |
| `curriculum` | MEGA-plate gradient | Grid columns form bands judged on progressively harder subsets of the cases (needs `spatial`) |

Per-generation statistics (mean mutation rate, mean stress multiplier, number of transfers and
duplications) appear in `--json` output under `population_stats`.

## Adding a problem (C++)

A problem subclasses one generic interface; there are no domain base classes.

```cpp
#include "adaptogen/problem.hpp"

class MyProblem : public adaptogen::Problem {
public:
    std::string name() const override { return "my-problem"; }
    std::string description() const override { return "..."; }
    std::vector<adaptogen::Channel> channels() const override {
        return {{"price", adaptogen::ChannelKind::Sensor, 0, "today's price", "EUR"}};
    }
    std::size_t num_cases(adaptogen::Split s) const override { return s == adaptogen::Split::Train ? 100 : 50; }
    double evaluate_case(adaptogen::Runner& runner, const adaptogen::Program& program,
                         adaptogen::Split split, std::size_t index) const override {
        // build an Environment that answers sense()/act(), run the program with runner.run(...),
        // read the output registers and return a score (higher is better)
    }
};
```

Register it with `adaptogen::register_problem(...)` and it is available from the CLI. Generated
plug-ins that don't need recompiling the engine come in stage 4.

## Comparison with other algorithms

`adaptogen-compare` runs fair comparisons with **the same evaluation budget** and scores
on unseen test data:

```bash
./build/adaptogen-compare --suite baselines --runs 20    # vs tree GP, hill climbing, random search, polyfit
```

```bash
./build/adaptogen-compare --suite mechanisms --runs 20   # ablation: what each bacterial mechanism adds
```

Current results and an honest discussion are in [benchmarks/RESULTS.md](benchmarks/RESULTS.md).

## Layout

```
include/adaptogen/   public API (vm, problem, engine, variation, analysis, serialize, registry)
src/                 engine implementation, CLI, built-in problems
bench/               comparison tool and baseline algorithms
tests/               unit tests (self-contained runner)
examples/            configs (quick.json, bacterial.json) and data
benchmarks/          comparison results
```
