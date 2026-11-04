/* adaptogen plug-in ABI, version 1.
 *
 * A plug-in is a shared library that exports one C function, adaptogen_plugin_v1(), returning a
 * table of functions. Only plain C types cross this boundary, so a plug-in built with a different
 * compiler (or compiler version) than the engine still loads.
 *
 * The engine's virtual machine stays in the host: to run the evolved program the plug-in calls
 * host->run(...), passing an environment that answers the program's sense/act instructions.
 *
 * Plug-in authors normally do not use this header directly: see adaptogen/sdk.hpp.
 */
#ifndef ADAPTOGEN_PLUGIN_ABI_H
#define ADAPTOGEN_PLUGIN_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

#define ADAPTOGEN_PLUGIN_ABI_VERSION 1

/* Answers the program's sensor reads and actions. */
typedef struct ag_env {
    void* self;
    double (*sense)(void* self, int channel, const double* args, int nargs);
    double (*act)(void* self, int channel, const double* args, int nargs);
} ag_env;

/* Provided by the host: runs the current program once.
 * regs must hold nregs values (the machine's register count, see ag_host.registers); they are
 * used as-is, so a plug-in can keep state between runs (e.g. once per time step).
 * Returns the number of executed instructions. */
typedef struct ag_host {
    void* ctx;
    int registers;
    int (*run)(void* ctx, const ag_env* env, double* regs, int nregs);
} ag_host;

typedef struct ag_plugin_v1 {
    int abi_version; /* = ADAPTOGEN_PLUGIN_ABI_VERSION */

    /* params_json: a JSON object. On failure return NULL and write a message into err. */
    void* (*create)(const char* params_json, unsigned long long seed, char* err, int errlen);
    void (*destroy)(void* problem);

    /* JSON object describing the problem (valid until destroy):
     * {"name", "description", "channels": [{"name","kind","arity","description","units"}],
     *  "num_outputs", "default_ops": [...], "cases": {"train": n, "test": m},
     *  "parents": [strain ids], "disabled_channels": [...], "channel_maps": {"strain:old": "new"}} */
    const char* (*info)(void* problem);

    /* split: 0 = train, 1 = test. Higher is better. Must be deterministic. NaN = error. */
    double (*evaluate_case)(void* problem, const ag_host* host, int split, unsigned long long index);

    /* Metrics of the current program as a JSON object written into buf (NUL-terminated).
     * Returns the length needed (excluding NUL); if >= buflen the call is repeated with a
     * larger buffer. Negative = error. */
    int (*metrics)(void* problem, const ag_host* host, int split, char* buf, int buflen);

    /* 1 if the current program solves the split, 0 if not, negative on error. */
    int (*solved)(void* problem, const ag_host* host, int split);
} ag_plugin_v1;

/* The one symbol a plug-in exports. */
typedef const ag_plugin_v1* (*ag_plugin_entry_v1)(void);
#define ADAPTOGEN_PLUGIN_ENTRY "adaptogen_plugin_v1"

#ifdef __cplusplus
}
#endif

#endif /* ADAPTOGEN_PLUGIN_ABI_H */
