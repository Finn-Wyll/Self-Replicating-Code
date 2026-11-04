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

> Status: **all 8 stages built**: core engine, bacteria-inspired mechanisms, strain bank, plug-ins,
> built-in problems, output tools, MCP server for Claude, experiment runner. See [DESIGN.md](DESIGN.md) for the full plan:
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
./build/adaptogen evolve --problem formula --param data=examples/data/kepler.csv --population 500 --generations 300 --selection tournament --set length_cost=0.02 --seed 1
```

```
SOLVED after 26 generations (13000 program evaluations, 0.0s)
  train: r2=0.999999 ...
  test (unseen): r2=0.999998 ...  [solved]

simplified from 32 to 3 instructions (same score on every training case)
evolved program (3 instructions, 3 effective):
# all registers start at 0; output: r0
r0 = planet_distance_au
r1 = sqrt(|r0|)
r0 = r0 * r1
```

That's `period = distance × √distance`, i.e. T² = a³. The evolved program had 32 instructions;
the other 29 were non-coding "junk DNA" with no effect. During evolution that junk is raw
material later mutations can turn into something new. In the final answer it is removed
(`--no-simplify` keeps it). With other seeds evolution sometimes finds only an approximation;
it is a search, not a guarantee.

Other commands:

```bash
./build/adaptogen list                       # problems and their parameters
./build/adaptogen config                     # default engine settings as JSON
./build/adaptogen evolve --problem formula --param target=nguyen7 --out best.json --csv stats.csv
./build/adaptogen show --program best.json   # re-score and print a saved program
./build/adaptogen evolve --problem formula --param target=koza1 --json   # one JSON event per line
```

List algorithms (the program must discover iteration; it is tested on lists 2.5x longer than
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

**Simplification.** By default the final program is simplified before it is printed or saved.
Statements are deleted, and ifs/loops unwrapped, as long as **no training case scores worse**,
checked case by case. Evolved programs typically shrink 3–25× (e.g. 128 → 5 instructions)
into readable code. Test data is never used for this. `--no-simplify` keeps the raw program;
`--out` files contain both.

Every instruction sequence is a valid program: division, `log` and `exp` are protected, loops are
capped and every run has a step limit. So mutation can never "break" a program; it can only
change what it does.

**Selection.** `lexicase` (default) picks parents that excel on randomly ordered individual cases,
which keeps specialists alive as stepping stones. `tournament` compares overall average scores.

**Down-sampling (on by default).** Each generation, programs are scored on a fresh random 10% of
the training cases (`"downsample": 0.1`, at least `"downsample_min_cases": 3`). The final champion
is always judged on all of them. The same compute then buys about ten times more generations.
In benchmarks this nearly **tripled** the solved runs and made adaptogen significantly better
than classic tree GP ([benchmarks/RESULTS.md](benchmarks/RESULTS.md) §6). Because each generation
is cheaper, the default generation limit is 1000. Set `"downsample": 1` to score every case.

Optional extras: `"constant_tuning": true` (local search on the constants of the best programs;
no significant gain in benchmarks) and `"cache_duplicates": true` (offspring identical to a parent
reuse its scores; same results, ~3% fewer evaluations, only without down-sampling).

## Built-in problems

| Problem | What evolves | Compared against |
|---|---|---|
| `formula` | a formula from data (benchmarks or your CSV) | tree GP, polynomial fit |
| `list` | sum / max / count of a list (must discover loops) | – |
| `scheduling` | a rule for ordering tasks with deadlines and priorities | earliest deadline first, weighted shortest job first |
| `errands` | a rule for visiting shops with opening hours | nearest shop first, earliest closing first |
| `packing` | a rule for packing a bag with a weight limit | best value per kg; scored against the exact optimum |
| `meals` | a rule for cheap meals meeting calorie and protein targets | most calories per euro |
| `bills` | a rule for paying bills, keeping a buffer and saving | pay by due date, save half |
| `energy` | a thermostat program with changing electricity prices | a standard thermostat |
| `foraging` | a controller for an agent collecting food on a grid | – |

`scheduling` through `bills` evolve **priority rules**: at each decision the program scores every
candidate (task, shop, item, food, bill) and the best one is chosen. The result is a reusable
rule that works on any new instance. `energy` and `foraging` are controllers that run every time
step and keep their memory. These seven are plug-ins in [problems/](problems/), so they double as
examples for writing your own. They share channel names (`item.*`, `time.*`, `need.*`) so strains
can transfer between them.

```bash
./build/adaptogen evolve --problem scheduling --save-strain my-scheduler
```

First results (one run each, seed 1, unseen test instances):

| Problem | evolved rule | best reference rule |
|---|---|---|
| scheduling: weighted lateness (lower is better) | **92.9** | 147.7 (weighted shortest job first) |
| errands: score (higher is better) | **0.954** | 0.953 (earliest closing first) |
| packing: share of the optimum | **99.1%** (70% exactly optimal) | 98.7% (best value per kg) |

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
| `stress` | SOS response | When progress stalls for `patience` generations (per lineage, or `"trigger": "population"` for the whole population), offspring get up to `max_multiplier`× more mutations |
| `hypermutation` | Mutator strains | Every program carries a heritable mutation rate that itself mutates (log-normal steps) |
| `transfer` | Plasmids / conjugation | Offspring receive a short code segment (1..`max_segment`) from a random neighbour |
| `duplication` | Gene duplication | A code segment is copied in place; the copies can then diverge |
| `spatial` | Colonies on a plate | Population on a torus; offspring compete with the cell they would replace |
| `curriculum` | MEGA-plate gradient | Grid columns form bands judged on progressively harder subsets of the cases (needs `spatial`) |

**What the experiments say so far** ([benchmarks/RESULTS.md](benchmarks/RESULTS.md)): no
mechanism is a statistically significant improvement yet, so all are off by default. The most
consistent, modest gains came from population-level stress
([examples/stress-population.json](examples/stress-population.json)) and low-rate horizontal
transfer ([examples/transfer-low.json](examples/transfer-low.json)), each used on its own.
`spatial` and `curriculum` hurt loop discovery on list problems.

Per-generation statistics (mean mutation rate, mean stress multiplier, number of transfers and
duplications) appear in `--json` output under `population_stats`.

## Strain bank: save evolved programs and build on them

Like a freezer of bacterial stocks: save a good program as a **strain**, then revive it as
the starting point for a related problem.

```bash
./build/adaptogen evolve --problem list --param task=sum --generations 300 --save-strain list-sum --description "sums a list"
```

```bash
./build/adaptogen strains list
```

```bash
./build/adaptogen strains show list-sum
```

```bash
./build/adaptogen strains check list-sum --problem list --param task=count_positive
```

```bash
./build/adaptogen strains simplify list-sum     # save a cleaned-up next version (lineage kept)
```

```bash
./build/adaptogen evolve --problem list --param task=count_positive --seed-strain list-sum
```

A strain is one human-readable JSON file (`strains/<name>-v<N>.json`, plus `index.json`) holding:

- **interface:** every channel it evolved with (name, kind, arity, description, units), with the
  ones its code actually uses marked;
- **behaviour:** the evolved program;
- **lineage:** parent strains, the problem and settings it evolved with, and its scores.

Saved strains are frozen. Saving the same name again creates the next version, so anything
derived from `list-sum-v1` never changes underneath you.

**Reviving on another problem.** Channels are matched by name, kind and arity. `strains check`
reports what matched, what is missing (code using it becomes inactive pseudogenes that evolution
can repurpose) and what is new. It warns when a matched channel's units or description differ
(same name, possibly different meaning).

| Option | Meaning |
|---|---|
| `--seed-strain ID` | Start from a strain (`list-sum` = latest version, `list-sum-v1` = exact). Repeat to start from **several parents** |
| `--seed-random F` | Fraction of the population that starts random (default 0.1), as a guard against a misleading start |
| `--retain W` | Forgetting control: keep scoring on the parent strains' own problems with weight `W` (0 = pure specialisation) |
| `--map OLD=NEW` | Connect a strain channel to a differently named problem channel |
| `--library DIR` | Library folder (default `$ADAPTOGEN_LIBRARY` or `./strains`) |

**Does it help?** On the same task with new data, reviving a strain solved every run in the first
generation at 1% of the cost of starting from scratch. For a *different* task (sum → max),
**simplified** strains transfer significantly better than raw ones (7/20 vs 0/20 solved, p = 0.008).
Five simplified parents raised median test accuracy from 0.09 to 0.82. See
[benchmarks/RESULTS.md](benchmarks/RESULTS.md) §4–5.

The same settings live in the config's `"seeding"` section, so a JSON config fully describes a
seeded run. A strain can also become the C++ base class of a new problem; see
[Deriving a problem from a strain](#deriving-a-problem-from-a-strain-dynamic-base-classes).

## Adding a problem: plug-ins

A new problem is **one C++ file** written against the plug-in SDK (`include/adaptogen/sdk.hpp`).
There's no need to touch or rebuild the engine. adaptogen compiles it on demand (cached) and
runs it in **separate worker processes**. If a plug-in crashes or hangs on some program, only a
worker dies: it is restarted and that program gets the worst possible score (like a cell killed
by the drug), while the rest of the population is scored normally.

```bash
./build/adaptogen plugin new my_problem               # writes my_problem.cpp from a working template
```

```bash
./build/adaptogen plugin validate my_problem.cpp      # compile + check interface and behaviour
```

```bash
./build/adaptogen evolve --problem plugin:my_problem.cpp
```

```cpp
#include <adaptogen/sdk.hpp>
using namespace adaptogen::sdk;

class MyProblem : public Problem {
public:
    MyProblem(const Params& params, std::uint64_t seed) { /* generate cases from the seed */ }
    std::string name() const override { return "my-problem"; }
    std::string description() const override { return "what it is about"; }
    std::vector<Channel> channels() const override {
        return {sensor("price", 0, "today's price", "EUR"), action("buy", 1, "buy n units", "units")};
    }
    std::size_t num_cases(Split s) const override { return s == Split::Train ? 100 : 50; }
    double evaluate_case(Runner& run, Split s, std::size_t i) const override {
        auto env = make_env([&](int channel, const double* args, int n) { /* answer sensors */ return 0.0; },
                            [&](int channel, const double* args, int n) { /* perform actions */ return 0.0; });
        run(env);                   // runs the evolved program (call repeatedly for time steps)
        return -std::fabs(run.reg(0) - 42);  // higher is better; give partial credit
    }
};
ADAPTOGEN_PLUGIN(MyProblem)
```

`plugin validate` reports in plain text, or as JSON with `--json` (for Claude and scripts):

- compile errors with file and line;
- interface problems (names, arities, duplicate channels, no test cases);
- **inheritance violations**;
- crashes or hangs on random programs;
- NaN scores, non-deterministic scores, and scores that never vary (nothing to select on).

### Deriving a problem from a strain (dynamic base classes)

A saved strain can become the **base class** of a new problem:

```bash
./build/adaptogen plugin new weighted_sum --from list-sum-v6    # writes weighted_sum_base.hpp + weighted_sum.cpp
```

The generated base class fixes the strain's channels (names, kinds, arities, descriptions and
units). Its `channels()` is `final`, so the C++ compiler itself refuses a redefinition, and the
parent's evolved code stays meaningful. The derived problem adds channels in `extra_channels()`,
can drop inherited ones via `disabled_channels()` (their code becomes pseudogenes), and records
its parents. `evolve --seed-parents` then starts from the parent strains.

- **Multiple inheritance:** pass several `--from` strains (or use `plugin header A B --class X`).
  Identical channels inherited twice are merged. The same name with a different meaning is a
  conflict, which you resolve with `--rename STRAIN:CHANNEL=NEW`; the rename is applied when
  seeding.
- **Example:** [examples/plugins/weighted_sum.cpp](examples/plugins/weighted_sum.cpp) derives from
  the `list-sum` strain and adds `list.weight`. Seeded from its parent, evolution found
  `Σ item × weight` without a multiply instruction: it adds each item `weight` times in a nested
  loop, i.e. it invented multiplication by repeated addition. The new strain
  `weighted-sum-v1` records `list-sum-v6` as its parent.

Built-in problems can still be written directly against `adaptogen::Problem` and registered with
`adaptogen::register_problem(...)`.

**Safety:** plug-ins are native code running with your permissions. The worker processes contain
crashes; they are not a security sandbox. Review generated plug-ins like any other code. Building
needs a C++20 compiler (`$ADAPTOGEN_CXX`, `$CXX`, or the one adaptogen was built with).

## Output tools

```bash
./build/adaptogen evolve --problem errands --save-strain errands-rule --lineage errands-lineage.md
```

```bash
./build/adaptogen export --strain errands-rule -o errands_rule.hpp
```

```bash
./build/adaptogen strains lineage weighted-sum
```

- **Readable pseudo-code** is printed after every run (simplified; `--all` shows the raw program).
- **C++ export:** a self-contained header with the evolved program as a function, plus an
  `Environment` interface to connect it to your data. Formula programs also get a plain
  `compute(x, y, ...)`. The exported code computes *exactly* what the VM computes, including the
  protected operators, loop caps and step limit; a test compiles exported programs and compares
  them with the VM register by register.
- **Lineage report** (`--lineage FILE.md`): every time the best program improved, what produced it
  (crossover, a point mutation, an insertion, a horizontal transfer, gene duplication, constant
  tuning) and the code change as a diff. `strains lineage ID` does the same across a strain's
  ancestry.
- **Progress and data:** live progress in the terminal, `--csv` per-generation statistics,
  `--json` one event per line, `--out` the full result.

## Using it from Claude (MCP)

`adaptogen mcp` is a [Model Context Protocol](https://modelcontextprotocol.io) server, so Claude can
do all of the above for you: understand your scenario, pick or write a problem, evolve, explain
and save the result.

```bash
claude mcp add adaptogen -- /path/to/Self-Replicating-Code/build/adaptogen mcp --workspace /path/to/workspace --library /path/to/strains
```

For Claude Desktop, add to `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "adaptogen": {
      "command": "/path/to/Self-Replicating-Code/build/adaptogen",
      "args": ["mcp", "--workspace", "/path/to/workspace", "--library", "/path/to/strains"]
    }
  }
}
```

Tools: `list_problems`, `describe_problem`, `evolve_start` / `evolve_status` / `evolve_cancel`
(evolution runs in the background), `plugin_new`, `plugin_write`, `plugin_read`,
`plugin_validate`, `strains_list`, `strain_show`, `strain_check`, `strain_simplify`,
`strain_lineage`, `export_cpp`. Plug-in files can only be written and read inside `--workspace`.
The server also sends Claude a short workflow guide when it connects.

Then ask, for example: *"Evolve a rule for planning my Saturday errands: the pharmacy closes at
13:00 and the market at 12:00."* Claude checks the built-in `errands` problem (or writes a
plug-in for your exact situation), runs evolution, and shows you the rule and how it compares.

## Experiments

```bash
./build/adaptogen-compare --experiment experiments/quick.json
```

An experiment file is a JSON description of a comparison: suite, runs, seeds, budget,
benchmarks, plug-in problems and variants (config overlays). The files in
[experiments/](experiments/) reproduce the main published comparisons.

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
include/adaptogen/   public API (vm, problem, engine, variation, analysis, serialize, registry,
                     strain, simplify, plugin) and the plug-in SDK (sdk.hpp, plugin_abi.h)
src/                 engine implementation, CLI, built-in problems, plug-in worker
bench/               comparison/experiment tool and baseline algorithms
problems/            built-in problems written as plug-ins
experiments/         experiment files for adaptogen-compare
tests/               unit tests (self-contained runner)
examples/            configs (quick, bacterial, stress-population, transfer-low), data, plug-ins
benchmarks/          comparison results
```
