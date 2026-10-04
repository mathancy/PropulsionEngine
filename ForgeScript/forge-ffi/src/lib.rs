//! A C interface to the ForgeScript interpreter.
//!
//! This is how an editor runs `.forge` files straight from a project folder: it loads the scripts
//! when Play starts, so changing a script never needs a rebuild. The interpreter is the reference
//! for what every sentence means (SPEC 4.5a), and the generated C++ is tested against it, so a
//! script behaves the same here and in a shipped game.
//!
//! Everything is single-threaded: use a session from one thread only. No function lets a Rust
//! panic cross into C; a panic is turned into a failure return value instead.
//!
//! Strings returned by this library (`char*`) belong to the caller and must be passed to
//! `forge_string_free`. Strings passed in are copied and only need to live for the call.

use forge::runtime::{Host, Val, World};
use forge::{ast::PressKind, Project};
use std::ffi::{c_char, c_void, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::{Path, PathBuf};

// ---- the engine's side: callbacks the interpreter makes ----

/// What the engine provides to scripts. Any callback may be null; a missing one does nothing
/// (input reads as "not pressed" and an axis as 0).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct ForgeHost {
    /// Passed back to every callback.
    pub user: *mut c_void,
    /// `kind`: 0 = is pressing, 1 = is not pressing, 2 = starts pressing, 3 = stops pressing.
    pub pressing: Option<extern "C" fn(user: *mut c_void, system: *const c_char, input: *const c_char, kind: i32) -> bool>,
    /// An analog input between -1 and 1.
    pub axis: Option<extern "C" fn(user: *mut c_void, system: *const c_char, input: *const c_char) -> f32>,
    /// `do Play on Audio sound:"coin.wav"` calls this with system "Audio", action "Play" and
    /// `args` as text: `sound="coin.wav"` (one `name=value` per argument, separated by spaces).
    pub call: Option<extern "C" fn(user: *mut c_void, system: *const c_char, action: *const c_char, args: *const c_char)>,
    /// `print "..."`.
    pub print: Option<extern "C" fn(user: *mut c_void, text: *const c_char)>,
    /// Once at the end of every `forge_step`, so started/stopped input can reset.
    pub end_frame: Option<extern "C" fn(user: *mut c_void)>,
    /// `level name`: the name of the level being played. The text only has to stay valid until the next call.
    pub level_name: Option<extern "C" fn(user: *mut c_void) -> *const c_char>,
}

fn c(s: &str) -> CString {
    // Interior NULs can't be passed through C strings; drop them rather than fail.
    CString::new(s.replace('\0', "")).unwrap_or_default()
}

struct CHost(ForgeHost);

impl Host for CHost {
    fn call(&mut self, system: &str, action: &str, args: &[(String, Val)]) {
        if let Some(f) = self.0.call {
            let text = args.iter().map(|(n, v)| format!("{n}={}", v.literal())).collect::<Vec<_>>().join(" ");
            f(self.0.user, c(system).as_ptr(), c(action).as_ptr(), c(&text).as_ptr());
        }
    }

    fn pressing(&self, system: &str, input: &str, kind: PressKind) -> bool {
        let Some(f) = self.0.pressing else { return matches!(kind, PressKind::NotHeld) };
        let kind = match kind {
            PressKind::Held => 0,
            PressKind::NotHeld => 1,
            PressKind::Started => 2,
            PressKind::Stopped => 3,
        };
        f(self.0.user, c(system).as_ptr(), c(input).as_ptr(), kind)
    }

    fn axis(&self, system: &str, input: &str) -> f32 {
        match self.0.axis {
            Some(f) => f(self.0.user, c(system).as_ptr(), c(input).as_ptr()),
            None => 0.0,
        }
    }

    fn print(&mut self, text: &str) {
        if let Some(f) = self.0.print {
            f(self.0.user, c(text).as_ptr());
        }
    }

    fn level_name(&self) -> String {
        match self.0.level_name {
            Some(f) => text(f(self.0.user)),
            None => String::new(),
        }
    }

    fn end_frame(&mut self) {
        if let Some(f) = self.0.end_frame {
            f(self.0.user);
        }
    }
}

// ---- sessions ----

/// A running set of scripts: the loaded files and every entity made from them.
pub struct ForgeSession {
    world: World<CHost>,
}

/// Run `body`, turning a panic into `fallback` so it never unwinds into C.
fn guard<T>(fallback: T, body: impl FnOnce() -> T) -> T {
    catch_unwind(AssertUnwindSafe(body)).unwrap_or(fallback)
}

fn text(p: *const c_char) -> String {
    if p.is_null() {
        String::new()
    } else {
        unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
    }
}

fn give(s: &str) -> *mut c_char {
    c(s).into_raw()
}

fn set_error(out: *mut *mut c_char, message: &str) {
    if !out.is_null() {
        unsafe { *out = give(message) };
    }
}

fn floats3(p: *const f32, default: [f32; 3]) -> [f32; 3] {
    if p.is_null() {
        default
    } else {
        let s = unsafe { std::slice::from_raw_parts(p, 3) };
        [s[0], s[1], s[2]]
    }
}

fn write3(p: *mut f32, v: [f32; 3]) {
    if !p.is_null() {
        unsafe { std::slice::from_raw_parts_mut(p, 3) }.copy_from_slice(&v);
    }
}

/// Every problem in the project, rendered with the line it is on, or `None` if there are none.
fn problems(project: &Project) -> Option<String> {
    let mut out = String::new();
    for report in project.check(&forge::Manifest::builtin()) {
        for e in &report.errors {
            out.push_str(&e.render(&report.source, &report.path));
        }
    }
    (!out.is_empty()).then_some(out)
}

/// Load `count` scripts (and everything each one needs) and check them all. Returns null and sets
/// `*errors` if any has a problem (or can't be read); the text names the file and line.
///
/// # Safety
/// `paths` must point to `count` valid C strings; `errors` may be null.
#[no_mangle]
pub unsafe extern "C" fn forge_session_new(
    host: ForgeHost,
    paths: *const *const c_char,
    count: usize,
    errors: *mut *mut c_char,
) -> *mut ForgeSession {
    guard(std::ptr::null_mut(), || {
        let entries: Vec<PathBuf> = (0..count).map(|i| PathBuf::from(text(unsafe { *paths.add(i) }))).collect();
        let project = match Project::load_many(&entries) {
            Ok(p) => p,
            Err(e) => {
                set_error(errors, &format!("error: {e}\n"));
                return std::ptr::null_mut();
            }
        };
        if let Some(text) = problems(&project) {
            set_error(errors, &text);
            return std::ptr::null_mut();
        }
        match World::new(project, CHost(host)) {
            Ok(mut world) => {
                // A game rolls different numbers each time it is played.
                let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos() as u64).unwrap_or(1);
                world.seed(now);
                Box::into_raw(Box::new(ForgeSession { world }))
            }
            Err(_) => {
                set_error(errors, "error: the scripts did not pass their checks\n");
                std::ptr::null_mut()
            }
        }
    })
}

/// # Safety
/// `session` must come from `forge_session_new` and not be used afterwards.
#[no_mangle]
pub unsafe extern "C" fn forge_session_free(session: *mut ForgeSession) {
    if !session.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(unsafe { Box::from_raw(session) })));
    }
}

/// Check one script without running it. Returns true if it is fine; otherwise false and `*errors`
/// holds the problems (to show in the editor).
///
/// # Safety
/// `path` must be a valid C string; `errors` may be null.
#[no_mangle]
pub unsafe extern "C" fn forge_check_file(path: *const c_char, errors: *mut *mut c_char) -> bool {
    guard(false, || match Project::load(Path::new(&text(path))) {
        Err(e) => {
            set_error(errors, &format!("error: {e}\n"));
            false
        }
        Ok(project) => match problems(&project) {
            Some(text) => {
                set_error(errors, &text);
                false
            }
            None => true,
        },
    })
}

/// Create an entity running the script at `path` (one that was given to `forge_session_new`),
/// at the given location, rotation (degrees) and scale. Its `when created` handlers have run when
/// this returns. Returns its id, or -1 if that script isn't loaded. Null vectors mean 0, 0, 0
/// (and 1, 1, 1 for scale).
///
/// # Safety
/// `session` must be valid; the vectors, when not null, must point to three floats.
#[no_mangle]
pub unsafe extern "C" fn forge_spawn(
    session: *mut ForgeSession,
    path: *const c_char,
    loc: *const f32,
    rot: *const f32,
    scale: *const f32,
) -> i64 {
    guard(-1, || {
        let Some(s) = (unsafe { session.as_mut() }) else { return -1 };
        let key = Project::key_for(Path::new(&text(path)));
        if !s.world.has_file(&key) {
            return -1;
        }
        s.world.spawn_placed(&key, floats3(loc, [0.0; 3]), floats3(rot, [0.0; 3]), floats3(scale, [1.0; 3])) as i64
    })
}

/// Read an entity's transform. Returns false if the entity doesn't exist or has been destroyed.
///
/// # Safety
/// `session` must be valid; each output, when not null, must point to three floats.
#[no_mangle]
pub unsafe extern "C" fn forge_get_transform(session: *mut ForgeSession, id: i64, loc: *mut f32, rot: *mut f32, scale: *mut f32) -> bool {
    guard(false, || {
        let Some(s) = (unsafe { session.as_mut() }) else { return false };
        let Some(e) = usize::try_from(id).ok().and_then(|i| s.world.entities.get(i)) else { return false };
        if !e.alive {
            return false;
        }
        write3(loc, e.loc);
        write3(rot, e.rot);
        write3(scale, e.scale);
        true
    })
}

/// Set an entity's transform (for example, after the user moved the object).
///
/// # Safety
/// As `forge_get_transform`, with inputs in place of outputs.
#[no_mangle]
pub unsafe extern "C" fn forge_set_transform(session: *mut ForgeSession, id: i64, loc: *const f32, rot: *const f32, scale: *const f32) -> bool {
    guard(false, || {
        let Some(s) = (unsafe { session.as_mut() }) else { return false };
        let Some(e) = usize::try_from(id).ok().and_then(|i| s.world.entities.get_mut(i)) else { return false };
        if !e.alive {
            return false;
        }
        e.loc = floats3(loc, e.loc);
        e.rot = floats3(rot, e.rot);
        e.scale = floats3(scale, e.scale);
        true
    })
}

/// Advance every script by `dt` seconds.
///
/// # Safety
/// `session` must be valid.
#[no_mangle]
pub unsafe extern "C" fn forge_step(session: *mut ForgeSession, dt: f32) {
    guard((), || {
        if let Some(s) = unsafe { session.as_mut() } {
            s.world.step(dt as f64);
        }
    })
}

/// Tell scripts two entities began or stopped overlapping (`when Self touches ...`).
///
/// # Safety
/// `session` must be valid.
#[no_mangle]
pub unsafe extern "C" fn forge_touch(session: *mut ForgeSession, a: i64, b: i64, touching: bool) {
    guard((), || {
        let Some(s) = (unsafe { session.as_mut() }) else { return };
        let (Ok(a), Ok(b)) = (usize::try_from(a), usize::try_from(b)) else { return };
        if a < s.world.entities.len() && b < s.world.entities.len() {
            if touching {
                s.world.touch(a, b);
            } else {
                s.world.untouch(a, b);
            }
        }
    })
}

/// Tell scripts two entities collided and blocked each other (`when Self hits ...`).
///
/// # Safety
/// `session` must be valid.
#[no_mangle]
pub unsafe extern "C" fn forge_hit(session: *mut ForgeSession, a: i64, b: i64) {
    guard((), || {
        let Some(s) = (unsafe { session.as_mut() }) else { return };
        let (Ok(a), Ok(b)) = (usize::try_from(a), usize::try_from(b)) else { return };
        if a < s.world.entities.len() && b < s.world.entities.len() {
            s.world.hit(a, b);
        }
    })
}

/// Damage an entity from the engine (`when damaged as Amount from Attacker`). `source` is who did it, or -1.
///
/// # Safety
/// `session` must be valid.
#[no_mangle]
pub unsafe extern "C" fn forge_damage(session: *mut ForgeSession, target: i64, source: i64, amount: f32) {
    guard((), || {
        let Some(s) = (unsafe { session.as_mut() }) else { return };
        let Ok(target) = usize::try_from(target) else { return };
        if target < s.world.entities.len() {
            s.world.damage(target, usize::try_from(source).ok().filter(|i| *i < s.world.entities.len()), amount);
        }
    })
}

/// Whether an entity is drawn (`make Visibility.Shown false`) and takes part in collisions
/// (`make Collision.Enabled false`). Returns false if the entity doesn't exist or has been destroyed.
///
/// # Safety
/// `session` must be valid; each output, when not null, must point to a bool.
#[no_mangle]
pub unsafe extern "C" fn forge_get_flags(session: *mut ForgeSession, id: i64, shown: *mut bool, collides: *mut bool) -> bool {
    guard(false, || {
        let Some(s) = (unsafe { session.as_mut() }) else { return false };
        let Some(e) = usize::try_from(id).ok().and_then(|i| s.world.entities.get(i)) else { return false };
        if !e.alive {
            return false;
        }
        if !shown.is_null() {
            unsafe { *shown = e.shown };
        }
        if !collides.is_null() {
            unsafe { *collides = e.collides };
        }
        true
    })
}

/// How many entities have ever been created, including ones scripts spawned themselves
/// (`spawn Coin at ...`). Ids from `forge_spawn` are below this; anything at or above the highest
/// id the host created was made by a script.
///
/// # Safety
/// `session` must be valid.
#[no_mangle]
pub unsafe extern "C" fn forge_entity_count(session: *mut ForgeSession) -> usize {
    guard(0, || unsafe { session.as_ref() }.map_or(0, |s| s.world.entities.len()))
}

/// The things scripts did that were skipped since the last call (acting on a missing entity and
/// so on), one per line, or null if there are none.
///
/// # Safety
/// `session` must be valid. Free the result with `forge_string_free`.
#[no_mangle]
pub unsafe extern "C" fn forge_take_warnings(session: *mut ForgeSession) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        let Some(s) = (unsafe { session.as_mut() }) else { return std::ptr::null_mut() };
        if s.world.warnings.is_empty() {
            return std::ptr::null_mut();
        }
        let joined = s.world.warnings.join("\n");
        s.world.warnings.clear();
        give(&joined)
    })
}

/// # Safety
/// `s` must be a string returned by this library, not freed already (null is fine).
#[no_mangle]
pub unsafe extern "C" fn forge_string_free(s: *mut c_char) {
    if !s.is_null() {
        drop(unsafe { CString::from_raw(s) });
    }
}

/// The library's version, like "0.1.0". Not to be freed.
#[no_mangle]
pub extern "C" fn forge_version() -> *const c_char {
    concat!(env!("CARGO_PKG_VERSION"), "\0").as_ptr() as *const c_char
}

// ---- the script graph (see forge/src/graph.rs) ----

/// The graph of the script at `path`, as JSON (nodes with their pins and execution order, the
/// header's variables, and the checker's problems). On failure returns null and sets `*error`.
///
/// # Safety
/// `path` must be a valid C string; `error` may be null. Free the result with `forge_string_free`.
#[no_mangle]
pub unsafe extern "C" fn forge_graph_load(path: *const c_char, error: *mut *mut c_char) -> *mut c_char {
    guard(std::ptr::null_mut(), || match forge::graph::load(Path::new(&text(path))) {
        Ok(g) => give(&g.to_json()),
        Err(e) => {
            set_error(error, &e);
            std::ptr::null_mut()
        }
    })
}

/// Make one edit to the script at `path` and return its new graph as JSON. The edit is checked
/// first, and if it would leave the script with a new problem, the file is left untouched and
/// null is returned with the reason in `*error`.
///
/// `op` and `a1`..`a4` (any may be null):
/// - `set_pin`: a1 node id, a2 pin name, a3 new text
/// - `add`: a1 node name, a2 parent id (empty for a new event), a3 body slot, a4 index
/// - `delete`: a1 node id
/// - `move`: a1 node id, a2 new parent id, a3 body slot, a4 index
/// - `plug`: a1 node id, a2 pin name, a3 value node name
/// - `unplug`: a1 value node id
/// - `add_branch`: a1 node id, a2 `else_if` or `otherwise`
/// - `remove_branch`: a1 node id, a2 body slot
/// - `add_variable`: a1 a declaration, e.g. `variable name:Speed type:decimal value:45.0`
/// - `set_variable`: a1 variable name, a2 its new declaration (same name)
/// - `delete_variable`: a1 variable name (refused while the script uses it)
/// - `rename_variable`: a1 old name, a2 new name (every use is renamed)
/// - `add_reference`: a1 a declaration: `needs entity:Player file:"player.forge" tag:"player"`,
///   `needs entities:Foes tag:"enemy"` or `needs file:"coin.forge" as Coin`
/// - `set_reference`: a1 reference name, a2 its new declaration (same name)
/// - `delete_reference`: a1 reference name
/// - `rename_reference`: a1 old name, a2 new name (every use is renamed)
/// - `add_branch`: a1 node id, a2 `else_if` or `otherwise` (a Branch), or `case` or `otherwise` (a Switch)
/// - `set_positions`: a1 lines of `node id<TAB>x<TAB>y` (grid units); writes only the layout file
///   (`<script>.layout`), never the script
/// - `set_source`: a1 the script's whole new text (what typing in a code view does). It is written as
///   given, not re-formatted, and must parse (see `forge_graph_check_source`); it may replace a script
///   that does not parse
///
/// # Safety
/// All strings must be valid C strings or null. Free the result with `forge_string_free`.
#[no_mangle]
pub unsafe extern "C" fn forge_graph_edit(
    path: *const c_char,
    op: *const c_char,
    a1: *const c_char,
    a2: *const c_char,
    a3: *const c_char,
    a4: *const c_char,
    error: *mut *mut c_char,
) -> *mut c_char {
    guard(std::ptr::null_mut(), || {
        let args = [text(a1), text(a2), text(a3), text(a4)];
        let refs: Vec<&str> = args.iter().map(String::as_str).collect();
        match forge::graph::edit(Path::new(&text(path)), &text(op), &refs) {
            Ok(g) => give(&g.to_json()),
            Err(e) => {
                set_error(error, &e);
                std::ptr::null_mut()
            }
        }
    })
}

/// Whether `text` is a script the graph can show, without touching any file: the bar `forge_graph_edit`'s
/// `set_source` holds a new text to. Returns true if it is. If not, returns false and, for each output that
/// is not null, sets `*line` and `*col` (1-based) to where it stopped and `*message` to why (free it with
/// `forge_string_free`).
///
/// # Safety
/// `text` must be a valid C string; the outputs may be null.
#[no_mangle]
pub unsafe extern "C" fn forge_graph_check_source(text_in: *const c_char, line: *mut i32, col: *mut i32, message: *mut *mut c_char) -> bool {
    guard(false, || match forge::graph::check_source(&text(text_in)) {
        Ok(()) => true,
        Err((l, c, m)) => {
            if !line.is_null() {
                unsafe { *line = l as i32 };
            }
            if !col.is_null() {
                unsafe { *col = c as i32 };
            }
            set_error(message, &m);
            false
        }
    })
}

/// The nodes that aren't in the script at `path` yet and could take the variable `variable` in one of their pins,
/// as JSON: `[{"node":"Branch","pin":"Condition","category":"Flow"}]`. The script is not changed.
///
/// # Safety
/// All strings must be valid C strings or null. Free the result with `forge_string_free`.
#[no_mangle]
pub unsafe extern "C" fn forge_graph_fits(path: *const c_char, variable: *const c_char, error: *mut *mut c_char) -> *mut c_char {
    guard(std::ptr::null_mut(), || match forge::graph::fits(Path::new(&text(path)), &text(variable)) {
        Ok(json) => give(&json),
        Err(e) => {
            set_error(error, &e);
            std::ptr::null_mut()
        }
    })
}

/// Every node the editor can offer, as JSON.
///
/// # Safety
/// Free the result with `forge_string_free`.
#[no_mangle]
pub unsafe extern "C" fn forge_graph_catalog() -> *mut c_char {
    guard(std::ptr::null_mut(), || give(&forge::graph::catalog_json()))
}
