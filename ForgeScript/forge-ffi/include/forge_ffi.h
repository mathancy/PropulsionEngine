#pragma once

/*
 * forge_ffi.h
 * C interface to the ForgeScript interpreter (forge_ffi.dll).
 *
 * An editor loads .forge files straight from a project folder when Play starts, so changing a
 * script never needs a rebuild. Single-threaded: use a session from one thread only.
 *
 * Strings returned as `char*` belong to the caller: free them with forge_string_free.
 * Strings passed in are copied; they only need to live for the call.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ForgeSession ForgeSession;

/* What the engine provides to scripts. Any callback may be NULL. */
typedef struct ForgeHost {
    void* user; /* passed back to every callback */

    /* kind: 0 = is pressing, 1 = is not pressing, 2 = starts pressing, 3 = stops pressing */
    bool (*pressing)(void* user, const char* system, const char* input, int32_t kind);
    /* an analog input between -1 and 1 */
    float (*axis)(void* user, const char* system, const char* input);
    /* `do Play on Audio sound:"coin.wav"`: system "Audio", action "Play", args `sound="coin.wav"`
       (one name=value per argument, separated by spaces) */
    void (*call)(void* user, const char* system, const char* action, const char* args);
    /* `print "..."` */
    void (*print)(void* user, const char* text);
    /* once at the end of every forge_step, so started/stopped input can reset */
    void (*end_frame)(void* user);
    /* `level name`: the name of the level being played (the text only has to stay valid until the next call) */
    const char* (*level_name)(void* user);
} ForgeHost;

/* Load `count` scripts (and everything each needs) and check them all. Returns NULL and sets
   *errors (when not NULL) if any has a problem; the text names the file and the line. */
ForgeSession* forge_session_new(ForgeHost host, const char* const* paths, size_t count, char** errors);
void forge_session_free(ForgeSession* session);

/* Check one script without running it. On failure, *errors holds the problems. */
bool forge_check_file(const char* path, char** errors);

/* Create an entity running the script at `path` (one given to forge_session_new), at a location,
   rotation (degrees) and scale. Its `when created` handlers have run when this returns.
   Returns its id, or -1 if that script isn't loaded. NULL vectors mean 0,0,0 (1,1,1 for scale). */
int64_t forge_spawn(ForgeSession* session, const char* path, const float* loc, const float* rot, const float* scale);

/* Read or set an entity's transform (three floats each; outputs may be NULL).
   Both return false if the entity doesn't exist or has been destroyed. */
bool forge_get_transform(ForgeSession* session, int64_t id, float* loc, float* rot, float* scale);
bool forge_set_transform(ForgeSession* session, int64_t id, const float* loc, const float* rot, const float* scale);

/* Advance every script by dt seconds. */
void forge_step(ForgeSession* session, float dt);

/* `when Self touches ...`: two entities began (touching = true) or stopped overlapping. */
void forge_touch(ForgeSession* session, int64_t a, int64_t b, bool touching);

/* `when Self hits ...`: two entities collided and blocked each other. */
void forge_hit(ForgeSession* session, int64_t a, int64_t b);

/* `when damaged as Amount from Attacker`: damage an entity from the engine. `source` is who did it, or -1. */
void forge_damage(ForgeSession* session, int64_t target, int64_t source, float amount);

/* Whether an entity is drawn (`make Visibility.Shown false`) and takes part in collisions
   (`make Collision.Enabled false`). Returns false if the entity doesn't exist or has been destroyed.
   Outputs may be NULL. */
bool forge_get_flags(ForgeSession* session, int64_t id, bool* shown, bool* collides);

/* How many entities have been created, including ones scripts spawned themselves. */
size_t forge_entity_count(ForgeSession* session);

/* Things scripts did that were skipped since the last call, one per line, or NULL if none. */
char* forge_take_warnings(ForgeSession* session);


/* ---- the script graph ----
   A node-and-wire view of a script's text. The text is always the source of truth: every edit is
   turned back into ForgeScript, checked, and saved only if the script stays valid. Reload the graph
   after every edit (node ids are paths and change when nodes are added or removed). */

/* The graph of the script at `path` as JSON: nodes (with pins, execution order, and value nodes
   wired into pins), the header's variables, and the checker's problems. NULL and *error on failure. */
char* forge_graph_load(const char* path, char** error);

/* One edit; returns the new graph as JSON, or NULL with the reason in *error (the file is then
   untouched). op and arguments:
     set_pin        a1 node id, a2 pin name, a3 new text
     add            a1 node name, a2 parent id ("" for a new event), a3 body slot, a4 index
     delete         a1 node id
     move           a1 node id, a2 new parent id, a3 body slot, a4 index
     plug           a1 node id, a2 pin name, a3 value node name
     unplug         a1 value node id
     add_branch     a1 node id, a2 "else_if" or "otherwise" (a Branch), "case" or "otherwise" (a Switch)
     remove_branch  a1 node id, a2 body slot
     add_variable   a1 a declaration, e.g. "variable name:Speed type:decimal value:45.0"
     set_variable   a1 variable name, a2 its new declaration (same name)
     delete_variable a1 variable name (refused while the script uses it)
     rename_variable a1 old name, a2 new name (every use is renamed)
     add_reference  a1 a declaration: "needs entity:Player file:\"player.forge\" tag:\"player\"",
                    "needs entities:Foes tag:\"enemy\"" or "needs file:\"coin.forge\" as Coin"
     set_reference  a1 reference name, a2 its new declaration (same name)
     delete_reference a1 reference name
     rename_reference a1 old name, a2 new name (every use is renamed)
     set_positions  a1 lines of "node id<TAB>x<TAB>y" (grid units); writes only the layout file,
                    <script>.layout, never the script
     set_source     a1 the script's whole new text (what typing in a code view does). It is written
                    as given, not re-formatted, and must parse (see forge_graph_check_source); it may
                    replace a script that does not parse */
char* forge_graph_edit(const char* path, const char* op, const char* a1, const char* a2, const char* a3, const char* a4, char** error);

/* Whether `text` is a script the graph can show (the bar set_source holds a new text to), without
   touching any file. Returns true if it is. If not, returns false and, for each output that is not
   NULL, sets *line and *col (1-based) to where it stopped and *message to why (free it with
   forge_string_free). */
bool forge_graph_check_source(const char* text, int32_t* line, int32_t* col, char** message);

/* Every node the editor can offer, as JSON. */
char* forge_graph_catalog(void);

/* The nodes that are not in the script yet and could take the variable `variable` in one of their pins, as JSON:
   [{"node":"Branch","pin":"Condition","category":"Flow"}]. The checker decides; the script is not changed.
   NULL and *error on failure. */
char* forge_graph_fits(const char* path, const char* variable, char** error);

void forge_string_free(char* s);
const char* forge_version(void); /* not to be freed */

#ifdef __cplusplus
}
#endif
