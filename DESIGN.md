# adaptogen: Design Specification

> Status: **all eight stages are built** (see section 12 for what was built and where it
> deviates from this plan). Measured results: [benchmarks/RESULTS.md](benchmarks/RESULTS.md).
> Working name: **adaptogen** (adapt + gene); easy to rename.

## 1. Purpose

adaptogen is a general-purpose engine that **evolves programs**, meaning reusable strategies
and not one-off answers. It needs very little prior knowledge of the problem. It is inspired by
how bacteria acquire antibiotic resistance through replication, mutation and gene transfer.

- **Primary goal:** a usable tool. Describe a scenario (directly, or through Claude) and get
  an evolved program that solves *new, unseen* instances of it.
- **Secondary goal:** a research platform. Each biological mechanism can be switched on or off
  and measured.
- **Non-goal:** beating purpose-built solvers on their own problems. The strength is
  versatility: one engine, minimal prior knowledge, many problems.

### What "minimal knowledge" means
The engine is never told *how* to solve a problem. A problem supplies only:
- **Sense:** what can be observed
- **Act:** what can be done
- **Consequence:** how good the outcome was (graded per-case scores, never just pass/fail)

Strictly speaking this is reward-driven search (black-box optimisation / RL-like), not
"unsupervised learning" in the ML sense.

## 2. Biological analogy

| Biology | adaptogen |
|---|---|
| Genome / DNA | Program: a linear list of VM instructions |
| Cell machinery | Virtual machine |
| Antibiotic / environment | Problem instance and its scoring |
| Resistance mechanism | Evolved behaviour that scores well |
| Copy error, indel | Point mutation, insertion, deletion |
| Gene duplication | Duplicate a code block; the copy can diverge |
| Plasmid / conjugation | Horizontal transfer of code segments between programs |
| SOS response | Stress-induced mutagenesis when a lineage stagnates |
| Hypermutator strains | Heritable, evolvable mutation rates |
| Biofilm / spatial structure | Population on a grid with local competition |
| MEGA-plate drug gradient | Difficulty curriculum (stepping stones) |
| Fitness cost of resistance | Configurable cost for code length and running time |
| Frozen strain stock | Saved strain in the strain bank |
| Pseudogene | Instruction referencing an unavailable channel (inactive) |

## 3. Core engine

- **Language/toolchain:** C++20, CMake. Targets Linux and macOS.
- **Dependencies:** pulled with CMake FetchContent (e.g. nlohmann/json). The first build needs
  internet access.
- **Virtual machine:** linear instruction programs, real-number registers, indexed access to
  input lists, loops and jumps, and a hard step limit per evaluation (no infinite loops). Any
  instruction sequence is valid, so mutations never produce code that cannot run.
- **Instruction set:** a fixed general-purpose core (arithmetic, comparison, control flow, list
  access), plus **sensor/actuator instructions generated from the problem's channels**.
- **Evaluation:** train/test split of randomly generated instances. The reported score is on
  unseen test instances, which measures generalisation.
- **Selection:** problems return per-case scores; the engine uses a weighted total or lexicase
  selection, chosen per problem or run.
- **Performance:** multithreaded evaluation. Deterministic: same seed gives same results.

## 4. Bacteria-inspired mechanisms (each switchable)

1. **Stress-induced mutagenesis + hypermutators:** mutation rates are heritable and evolve;
   they rise automatically when a lineage stagnates.
2. **Horizontal transfer:** code segments spread between unrelated programs.
3. **Gene duplication:** blocks are duplicated and the copies diverge into new functions.
4. **Spatial population + curriculum:** a grid with local competition, and a MEGA-plate-style
   difficulty gradient.
5. **Cost of complexity:** configurable penalties for code length and running time.

## 5. Problems and channels

The engine has **exactly one generic `Problem` interface** and **no hard-coded domain base
classes**. Built-in problems are examples, not categories.

A problem declares its interface as **named, typed channels**, each with a description and units:

```
sensors:  item.count, item.duration[i] (minutes), item.deadline[i] (minutes from start), ...
actions:  place(i, t), skip(i)
score:    per-case scores (deadlines met, priority-weighted, ...)
```

A recommended (not enforced) naming convention keeps channels reusable across problems.

## 6. Strain bank and dynamic base classes

### 6.1 Strains
A **strain** is a saved evolved program. It is stored as one human-readable JSON file per strain
in a library folder, plus a searchable index. A strain bundles:

- **Interface:** the full set of channels it evolved with (names, types, descriptions,
  units), with the channels its program actually uses flagged.
- **Behaviour:** the evolved program, acting as a default implementation.
- **Lineage:** where it came from (parent strains, problem, run settings, scores, date).

### 6.2 A saved strain becomes a base class
Nothing is hard-coded. Base classes are **created at runtime from evolved strains**:

1. Any problem defined from scratch is a root. Evolve it, then save, e.g. `sched-generic-v1`.
2. A new problem can **derive from** one or more strains. The engine **generates a C++ header**
   from the strain's interface, and the new plug-in is a genuine C++ class inheriting from it.
3. The derived problem inherits every parent channel exactly (same name, same meaning) and adds
   its own.
4. Evolution of the derived problem is seeded with the parent's program. The result can be saved
   and becomes a base class in turn.

The strain library therefore forms a class hierarchy that grows through evolution; the class tree
and the evolutionary tree are the same thing.

Example: `sched-generic-v1` → `sched-lectures-v1` → `sched-lectures-<university>-v1`

### 6.3 Inheritance rules
- **No redefinition:** inherited channels keep their meaning. Derived problems may add channels
  but not change existing ones.
- **Overrides with adapters:** an action can be extended (`place(i, t)` → `place(i, t, room)`);
  the old form maps onto the new one with a default value, so inherited programs keep working.
- **Disabling:** a derived problem may disable inherited channels it does not support.
  Instructions using them become inactive pseudogenes that evolution can repurpose or delete.
- **Versioning:** a saved strain's interface is frozen. Improvements create a new version
  (`v2`); derived strains keep pointing at the version they came from and never break.

### 6.4 Multiple inheritance
A problem may derive from **several strains at once** (e.g. a scheduler and a budget planner).

- Channels inherited from the same origin through several paths (a diamond) are merged.
- Channels with the same name but different origins are a **conflict**. They must be resolved
  explicitly by namespacing (`schedA::item.priority`) or by mapping. Claude may propose a
  resolution; the user approves it.
- The seed population mixes programs from all parents, and horizontal transfer can recombine
  their code.

### 6.5 Reviving and transfer
- **Seeding:** clones of the saved strain(s) with variation, optionally mixed with fresh random
  programs and other strains for diversity (this guards against negative transfer).
- **Forgetting:** chosen per run. Optionally keep evaluating on the parent problem with a chosen
  weight; off means pure specialisation.
- **Fallback for unrelated strains:** channels can also be matched by name and type, with
  optional mapping files. This is secondary to derivation.

## 7. Plug-ins (generated C++)

New problems are C++ plug-ins, typically written by Claude.

- **Plug-in SDK:** one header, a fill-in template, and generated base-class headers from strains.
- **Build and validate:** the engine compiles a plug-in to a shared library (`.so` / `.dylib`)
  and runs it on a sample instance. Compiler and validation errors are returned in structured
  form so Claude can fix its own code.
- **Stable boundary:** plug-ins talk to the engine through a C-style ABI.
- **Isolation:** plug-ins run in a **separate worker process**, so crashes and hangs cannot take
  down the engine or MCP server.
- **Safety:** this is crash isolation, not a security sandbox. Generated plug-ins are native
  code running with the user's permissions and should be reviewed like any other code.
- **Requirement:** a C++ compiler on the machine where plug-ins are built.

### Built-in problems
- **Benchmarks:** formula discovery, list algorithms (sort, max, count), agent foraging, generic
  scheduling.
- **Everyday:** errand route planning, budget & bills, packing / meal planning, home energy
  control.

## 8. Interfaces

All interfaces are clients of one engine API. The engine never depends on any UI.

- **C++ library API:** embed the engine in other applications.
- **CLI with JSON I/O:** e.g. `adaptogen evolve --problem errands --config run.json`.
- **MCP server:** lets Claude define problems (write, build and validate plug-ins, derive from
  strains), run evolution, check progress, browse the strain bank, and fetch and explain
  solutions. The LLM is an **interface only**; it does not take part in evolution.
- **Event/data API:** population state, progress, lineage events, grid state and the strain
  library are published in structured form, so a future GUI is just another client.

## 9. Outputs (each optional)

- Readable pseudo-code (simplified, dead code removed)
- Export as a standalone C++ function
- Lineage report: which mutations or transfers produced each improvement
- Live progress and CSV statistics

## 10. Build stages

1. VM, engine, `Problem` + channel interface, CLI, formula discovery end to end
2. Bacteria-inspired mechanisms with on/off switches
3. Strain bank: save, index, revive, seeding, forgetting control
4. Plug-in SDK: compiler/loader, worker-process isolation, generated base classes, multiple
   inheritance
5. Remaining built-in problems
6. Output tools: pseudo-code, C++ export, lineage report
7. MCP server, JSON CLI, event API
8. Experiment runner for ablations ("does mechanism X help on problem Y?")

**Later:** visual application (technology chosen at that point) built on the event API.

## 11. Known limitations

- Evolving code is compute-hungry; complex problems may take minutes to hours.
- Transfer works best between related problems. A change in the *kind* of output (e.g. a ranking
  instead of placements) transfers little.
- A seeded strain can occasionally be a worse start than random (negative transfer).
- Generated plug-ins are unsandboxed native code.

## 12. Implementation status and deviations from the plan

| Stage | Status | Notes |
|---|---|---|
| 1. Core engine | done | Linear VM, lexicase/tournament, effective mutation, CLI, formula discovery |
| 2. Bacterial mechanisms | done | All six switchable; measured in detail. None is a significant improvement yet; population-level stress and low-rate transfer are the most promising (RESULTS §2–3) |
| 3. Strain bank | done | Plus a verified **simplifier**, which turned out to be what makes cross-task transfer work (RESULTS §4–5) |
| 4. Plug-ins | done | C ABI + header-only SDK, isolated worker processes, validation, base classes generated from strains |
| 5. Built-in problems | done | Written as **plug-ins** (`problems/`), not compiled into the engine. Decision problems evolve *priority rules* |
| 6. Output tools | done | Pseudo-code, exact C++ export, run and strain lineage reports, CSV/JSON |
| 7. MCP server | done | `adaptogen mcp`: background evolution jobs; file access limited to a workspace |
| 8. Experiment runner | done | `adaptogen-compare` (suites, ablations, Fisher/CMH tests) + `--experiment FILE.json` |

Additions beyond the original plan, each backed by measurements:
- **Down-sampled lexicase is the default:** about 3× more solved runs at equal compute; adaptogen
  went from on par with tree GP to significantly better (RESULTS §6).
- **Validation split** (`validation_fraction`) to choose the final program on held-back cases.
- **Budgets in case evaluations**, the fair unit when comparing settings.

Known limits:
- Tested on Linux only; the macOS paths (`.dylib`, worker discovery) are written but unverified.
- Worker processes contain crashes but are not a security sandbox.
- Transfer to a *different* task needs simplified donors and long runs. Block-aware mutation
  (insertion that grows a loop body) is the most promising next step.
