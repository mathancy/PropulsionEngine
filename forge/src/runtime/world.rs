use super::exec::Run;
use crate::phrases::{rotate_by_euler, rotate_by_euler_inverse};
use super::host::Host;
use super::value::Val;
use crate::ast::*;
use crate::manifest::Manifest;
use crate::project::{FileReport, LoadedFile, Project};
use crate::ty::Type;
use std::collections::{HashMap, HashSet, VecDeque};
use std::rc::Rc;

/// One running copy of a `.forge` file, with its own variables and position.
#[derive(Debug, Clone)]
pub struct Entity {
    pub id: usize,
    /// Key of the file this entity runs.
    pub file: String,
    pub alive: bool,
    pub tags: Vec<String>,
    pub vars: HashMap<String, Val>,
    pub loc: [f32; 3],
    /// Euler angles in degrees, like the engine's `TransformComponent::rotation`.
    pub rot: [f32; 3],
    pub scale: [f32; 3],
    /// `make Visibility.Shown false`: whether the entity is drawn.
    pub shown: bool,
    /// `make Collision.Enabled false`: whether it takes part in collisions.
    pub collides: bool,
    /// The entity this one follows (`attach Hat to Player`), and where it sits relative to it.
    pub parent: Option<usize>,
    local_loc: [f32; 3],
    local_rot: [f32; 3],
    /// The state of `only once`, `first N times` and `alternate`, by the line of the statement.
    pub(crate) flow: HashMap<usize, i64>,
    /// When each `every N seconds` handler (by item index) is next due.
    due: HashMap<usize, f64>,
    /// The last value seen for each variable a `when <Variable> changes:` handler watches.
    watch: HashMap<String, Val>,
    born: f64,
}

#[derive(Clone, Copy, PartialEq)]
enum Overlap {
    Begin,
    End,
    Hit,
}

/// A block scheduled by `after N seconds:`.
pub(crate) struct Pending {
    pub due: f64,
    pub seq: u64,
    pub entity: usize,
    pub key: String,
    pub body: Vec<Stmt>,
    /// The local names visible when it was scheduled, copied.
    pub scope: HashMap<String, Val>,
}

/// A block running every frame for a while (`animate Lift from 0.0 to 5.0 over 2 seconds:`).
pub(crate) struct Timeline {
    pub seq: u64,
    pub entity: usize,
    pub key: String,
    pub body: Rc<Vec<Stmt>>,
    pub scope: HashMap<String, Val>,
    pub var: String,
    pub from: Val,
    pub to: Val,
    pub start: f64,
    pub duration: f64,
}

/// What a file's header says about the outside world, worked out once.
#[derive(Debug, Default)]
pub(crate) struct FileInfo {
    pub tags: Vec<String>,
    /// `needs file:"..." as Name`: alias -> key of that file.
    pub aliases: HashMap<String, String>,
    /// Files to make sure exist when an entity of this file is created (`when:startup`).
    pub startup: Vec<String>,
    /// `needs entity` / `needs entities`: name -> (tag, is a single entity).
    pub entity_bindings: HashMap<String, (String, bool)>,
    pub systems: HashSet<String>,
    /// Variables declared `exposed:true`: a placed instance may override them.
    pub exposed: HashSet<String>,
    /// `only once as Greeting:` and friends: the name, and the line of the statement it starts over.
    pub latches: HashMap<String, usize>,
    /// `table name:Items type:Item`: the name, the struct its rows are, and the rows (key, and the fields given).
    pub tables: HashMap<String, TableInfo>,
}

/// A table of structs to look up by key.
#[derive(Debug, Default)]
pub(crate) struct TableInfo {
    pub ty: String,
    pub rows: Vec<(String, Vec<(String, Term)>)>,
}

impl FileInfo {
    fn build(loaded: &LoadedFile, file: &File) -> FileInfo {
        let mut info = FileInfo::default();
        let dep = |rel: &str| loaded.deps.get(rel).and_then(|r| r.as_ref().ok()).cloned();
        for d in &file.header {
            match &d.kind {
                DeclKind::Tag(t) => info.tags.push(t.clone()),
                DeclKind::NeedsSystem(n) => {
                    info.systems.insert(n.clone());
                }
                DeclKind::Variable { name, exposed: true, .. } => {
                    info.exposed.insert(name.clone());
                }
                DeclKind::NeedsFile { file, alias, when } => {
                    if let Some(key) = dep(file) {
                        if when.as_deref() == Some("startup") {
                            info.startup.push(key.clone());
                        }
                        if let Some(a) = alias {
                            info.aliases.insert(a.clone(), key);
                        }
                    }
                }
                DeclKind::NeedsEntity { name, tag, .. } => {
                    info.entity_bindings.insert(name.clone(), (tag.clone(), true));
                }
                DeclKind::NeedsEntities { name, tag, .. } => {
                    info.entity_bindings.insert(name.clone(), (tag.clone(), false));
                }
                DeclKind::Table { name, ty, rows } => {
                    info.tables.insert(name.clone(), TableInfo { ty: ty.clone(), rows: rows.iter().map(|r| (r.key.clone(), r.values.clone())).collect() });
                }
                _ => {}
            }
        }
        for item in &file.implement {
            match &item.kind {
                ItemKind::Handler { body, .. } | ItemKind::Action { body, .. } => find_latches(body, &mut info.latches),
            }
        }
        info
    }
}

fn find_latches(body: &[Stmt], out: &mut HashMap<String, usize>) {
    for s in body {
        match &s.kind {
            StmtKind::OnlyOnce { name, body } | StmtKind::FirstTimes { name, body, .. } => {
                if let Some(n) = name {
                    out.insert(n.clone(), s.line);
                }
                find_latches(body, out);
            }
            StmtKind::Alternate { name, first, second } => {
                if let Some(n) = name {
                    out.insert(n.clone(), s.line);
                }
                find_latches(first, out);
                find_latches(second, out);
            }
            StmtKind::If { branches, otherwise, .. } => {
                for (_, b) in branches {
                    find_latches(b, out);
                }
                if let Some(b) = otherwise {
                    find_latches(b, out);
                }
            }
            StmtKind::Choose { cases, otherwise, .. } => {
                for c in cases {
                    find_latches(&c.body, out);
                }
                if let Some(b) = otherwise {
                    find_latches(b, out);
                }
            }
            StmtKind::Repeat { body, .. }
            | StmtKind::ForEach { body, .. }
            | StmtKind::ForRange { body, .. }
            | StmtKind::While { body, .. }
            | StmtKind::After { body, .. }
            | StmtKind::Through { body, .. }
            | StmtKind::Animate { body, .. } => find_latches(body, out),
            _ => {}
        }
    }
}

/// All running entities, the clock, and the event queue.
pub struct World<H: Host> {
    pub host: H,
    pub(crate) project: Rc<Project>,
    pub(crate) infos: HashMap<String, Rc<FileInfo>>,
    pub entities: Vec<Entity>,
    /// Seconds since the world started.
    pub time: f64,
    /// Length of the current frame in seconds (what `per second` multiplies by).
    pub(crate) dt: f64,
    pub(crate) queue: VecDeque<(usize, String)>,
    /// Blocks waiting on an `after N seconds:`.
    pub(crate) pending: Vec<Pending>,
    /// Blocks running every frame for a while (`animate`).
    pub(crate) timelines: Vec<Timeline>,
    pub(crate) next_seq: u64,
    random_state: u64,
    /// Every enum (name -> its values) and struct (name -> its fields) the scripts declare, and the built-in `hit`.
    enums: HashMap<String, Vec<String>>,
    structs: HashMap<String, Vec<(String, Type)>>,
    /// Things scripts did that were skipped, like acting on a missing entity (SPEC 4.6).
    pub warnings: Vec<String>,
    /// Rotation and scale for the next entity created, set by `spawn_placed`.
    spawn_pose: Option<([f32; 3], [f32; 3])>,
}

fn term_num(t: &Term) -> f32 {
    match t {
        Term::Int(i) => *i as f32,
        Term::Decimal(d) => *d as f32,
        _ => 0.0,
    }
}

fn vector_from_parts(ty: &Type, parts: &Parts) -> Val {
    let get = |k: &str| parts.iter().find(|(n, _)| n == k).map(|(_, t)| term_num(t)).unwrap_or(0.0);
    match ty {
        Type::Vector2 => Val::Vec2([get("x"), get("y")]),
        Type::Vector3 => Val::Vec3([get("x"), get("y"), get("z")]),
        Type::Color => Val::Color([get("r"), get("g"), get("b"), get("a")]),
        _ => Val::default_for(ty, 0.0),
    }
}

fn initial_value(world: &World<impl Host>, ty: &Type, value: Option<&Term>, parts: &Parts, entries: &[Parts], now: f64) -> Val {
    match ty {
        Type::Vector2 | Type::Vector3 | Type::Color => vector_from_parts(ty, parts),
        Type::Gate => Val::Bool(!matches!(value, Some(Term::Name(n)) if n == "closed")),
        Type::Enum(en) => match value {
            Some(Term::Name(n)) => Val::Enum(en.clone(), n.clone()),
            _ => world.default_val(ty, now),
        },
        Type::Struct(_) => world.default_val(ty, now),
        Type::List(el) => Val::List((**el).clone(), entries.iter().map(|e| vector_from_parts(el, e)).collect()),
        _ => match value {
            Some(Term::Int(i)) => Val::Int(*i as i32).coerce(ty),
            Some(Term::Decimal(d)) => Val::Dec(*d as f32),
            Some(Term::Text(s)) => Val::Text(s.clone()),
            Some(Term::Bool(b)) => Val::Bool(*b),
            _ => world.default_val(ty, now),
        },
    }
}

impl<H: Host> World<H> {
    /// Create a world from a loaded project. The project must pass every check; otherwise the
    /// reports with problems are returned instead.
    pub fn new(project: Project, host: H) -> Result<World<H>, Vec<FileReport>> {
        let reports = project.check(&Manifest::builtin());
        if reports.iter().any(|r| !r.errors.is_empty()) {
            return Err(reports.into_iter().filter(|r| !r.errors.is_empty()).collect());
        }
        let mut infos = HashMap::new();
        for (key, loaded) in &project.files {
            if let Ok(file) = &loaded.parsed {
                infos.insert(key.clone(), Rc::new(FileInfo::build(loaded, file)));
            }
        }
        let mut enums: HashMap<String, Vec<String>> = HashMap::new();
        let mut structs: HashMap<String, Vec<(String, Type)>> = HashMap::from([(
            "hit".to_string(),
            vec![
                ("Blocked".to_string(), Type::Boolean),
                ("Entity".to_string(), Type::Entity),
                ("Location".to_string(), Type::Vector3),
                ("Normal".to_string(), Type::Vector3),
                ("Distance".to_string(), Type::Decimal),
            ],
        )]);
        for loaded in project.files.values() {
            let Ok(file) = &loaded.parsed else { continue };
            for d in &file.header {
                match &d.kind {
                    DeclKind::Enum { name, values } => {
                        enums.entry(name.clone()).or_insert_with(|| values.clone());
                    }
                    DeclKind::Struct { name, fields } => {
                        structs.entry(name.clone()).or_insert_with(|| fields.iter().map(|(n, t)| (n.clone(), Type::from_expr(t))).collect());
                    }
                    _ => {}
                }
            }
        }
        Ok(World {
            host,
            project: Rc::new(project),
            infos,
            entities: Vec::new(),
            time: 0.0,
            dt: 0.0,
            queue: VecDeque::new(),
            pending: Vec::new(),
            timelines: Vec::new(),
            next_seq: 0,
            random_state: 0x2545_F491_4F6C_DD1D,
            enums,
            structs,
            warnings: Vec::new(),
            spawn_pose: None,
        })
    }

    /// The value a type starts as: zero, empty, an enum's first value, a struct with every field at its default.
    pub(crate) fn default_val(&self, ty: &Type, now: f64) -> Val {
        match ty {
            Type::Enum(n) => Val::Enum(n.clone(), self.enums.get(n).and_then(|v| v.first()).cloned().unwrap_or_default()),
            Type::Struct(n) => {
                let fields = self.structs.get(n).cloned().unwrap_or_default();
                Val::Struct(n.clone(), fields.iter().map(|(f, t)| (f.clone(), self.default_val(t, now))).collect())
            }
            other => Val::default_for(other, now),
        }
    }

    /// The enum a name is a value of (`Idle` of `Mode`), if it is one.
    pub(crate) fn enum_of_value(&self, value: &str) -> Option<&String> {
        self.enums.iter().find(|(_, values)| values.iter().any(|v| v == value)).map(|(name, _)| name)
    }

    /// The fields of a struct, with their types.
    pub(crate) fn struct_fields(&self, ty: &str) -> Vec<(String, Type)> {
        self.structs.get(ty).cloned().unwrap_or_default()
    }

    /// A struct with the fields given and every other field at its default.
    pub(crate) fn make_struct(&self, ty: &str, given: Vec<(String, Val)>) -> Val {
        let fields = self.structs.get(ty).cloned().unwrap_or_default();
        let now = self.time;
        Val::Struct(
            ty.to_string(),
            fields
                .iter()
                .map(|(n, t)| {
                    let v = given.iter().find(|(g, _)| g == n).map(|(_, v)| v.clone().coerce(t)).unwrap_or_else(|| self.default_val(t, now));
                    (n.clone(), v)
                })
                .collect(),
        )
    }

    /// What a line of sight from `from` to `to` hits first, as a `hit`. An entity counts as a ball half as wide
    /// as its largest scale until the engine reports real shapes; `radius` makes the line thick; `me` is skipped.
    pub(crate) fn trace(&self, me: usize, from: [f32; 3], to: [f32; 3], radius: f32) -> Val {
        let d = [to[0] - from[0], to[1] - from[1], to[2] - from[2]];
        let length = (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]).sqrt();
        let mut best: Option<(f32, usize)> = None;
        if length > 0.0 {
            let dir = [d[0] / length, d[1] / length, d[2] / length];
            for e in &self.entities {
                if !e.alive || !e.collides || e.id == me {
                    continue;
                }
                let r = e.scale[0].max(e.scale[1]).max(e.scale[2]) * 0.5 + radius;
                let oc = [from[0] - e.loc[0], from[1] - e.loc[1], from[2] - e.loc[2]];
                let b = oc[0] * dir[0] + oc[1] * dir[1] + oc[2] * dir[2];
                let c = oc[0] * oc[0] + oc[1] * oc[1] + oc[2] * oc[2] - r * r;
                if c > 0.0 && b > 0.0 {
                    continue; // outside the ball and looking away from it
                }
                let disc = b * b - c;
                if disc < 0.0 {
                    continue;
                }
                let t = (-b - disc.sqrt()).max(0.0);
                if t > length {
                    continue;
                }
                if best.is_none_or(|(bt, _)| t < bt) {
                    best = Some((t, e.id));
                }
            }
        }
        let fields = match best {
            Some((t, id)) => {
                let hit = [from[0] + d[0] / length * t, from[1] + d[1] / length * t, from[2] + d[2] / length * t];
                let c = self.entities[id].loc;
                let n = [hit[0] - c[0], hit[1] - c[1], hit[2] - c[2]];
                let nl = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt();
                let normal = if nl == 0.0 { [0.0, 1.0, 0.0] } else { [n[0] / nl, n[1] / nl, n[2] / nl] };
                vec![
                    ("Blocked".to_string(), Val::Bool(true)),
                    ("Entity".to_string(), Val::Entity(Some(id))),
                    ("Location".to_string(), Val::Vec3(hit)),
                    ("Normal".to_string(), Val::Vec3(normal)),
                    ("Distance".to_string(), Val::Dec(t)),
                ]
            }
            None => vec![
                ("Blocked".to_string(), Val::Bool(false)),
                ("Entity".to_string(), Val::Entity(None)),
                ("Location".to_string(), Val::Vec3(to)),
                ("Normal".to_string(), Val::Vec3([0.0; 3])),
                ("Distance".to_string(), Val::Dec(length)),
            ],
        };
        Val::Struct("hit".to_string(), fields)
    }

    /// Start the random numbers from `seed` (`random integer from 1 to 6`). A world starts from the same
    /// seed every time, so a test sees the same rolls; a game seeds from the clock.
    pub fn seed(&mut self, seed: u64) {
        self.random_state = if seed == 0 { 1 } else { seed };
    }

    pub(crate) fn next_random(&mut self) -> u64 {
        let mut x = self.random_state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.random_state = x;
        x.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }

    /// Whether the project loaded exactly this file key (unlike `file_key`, which matches a suffix).
    pub fn has_file(&self, key: &str) -> bool {
        self.project.files.contains_key(key)
    }

    /// The key of the loaded file whose path ends with `suffix`, like `"coin.forge"`.
    pub fn file_key(&self, suffix: &str) -> Option<String> {
        self.project.files.keys().find(|k| k.ends_with(suffix)).cloned()
    }

    /// Create an entity running the file the project was loaded from.
    pub fn spawn_entry(&mut self, loc: [f32; 3]) -> usize {
        let key = self.project.entry.clone();
        self.spawn(&key, loc)
    }

    /// Create an entity running the file `key` (one of the project's file keys). Its `when created:`
    /// handlers run before this returns.
    pub fn spawn(&mut self, key: &str, loc: [f32; 3]) -> usize {
        self.spawn_with(key, loc, &[])
    }

    /// Like `spawn`, with values for the file's `exposed:true` variables (what a placed instance
    /// in the editor would override). Overriding a variable that isn't exposed is skipped with a warning.
    pub fn spawn_with(&mut self, key: &str, loc: [f32; 3], overrides: &[(String, Val)]) -> usize {
        let id = self.spawn_inner(key, loc, overrides);
        self.drain_events();
        id
    }

    /// Create an entity at a full transform, for an engine placing a scene object. Unlike setting
    /// `rot` and `scale` afterwards, its `when created:` handlers already see them.
    pub fn spawn_placed(&mut self, key: &str, loc: [f32; 3], rot: [f32; 3], scale: [f32; 3]) -> usize {
        self.spawn_pose = Some((rot, scale));
        let id = self.spawn_inner(key, loc, &[]);
        self.spawn_pose = None;
        self.drain_events();
        id
    }

    /// `spawn` without delivering events afterwards, for use while a handler is running.
    pub(crate) fn spawn_inner(&mut self, key: &str, loc: [f32; 3], overrides: &[(String, Val)]) -> usize {
        let info = self.infos.get(key).unwrap_or_else(|| panic!("no loaded file `{key}`")).clone();
        let project = self.project.clone();
        let file = project.files[key].parsed.as_ref().expect("a world only holds files that parsed");

        let mut vars = HashMap::new();
        for d in &file.header {
            match &d.kind {
                DeclKind::Variable { name, ty, value, parts, entries, .. } => {
                    let t = Type::from_expr(ty);
                    vars.insert(name.clone(), initial_value(self, &t, value.as_ref(), parts, entries, self.time));
                }
                DeclKind::Constant { name, ty, value, parts } => {
                    let t = Type::from_expr(ty);
                    vars.insert(name.clone(), initial_value(self, &t, value.as_ref(), parts, &[], self.time));
                }
                _ => {}
            }
        }
        for (name, value) in overrides {
            if info.exposed.contains(name) {
                let existing = vars.get(name).cloned();
                let v = match &existing {
                    Some(e) => value.clone().coerce_like(e),
                    None => value.clone(),
                };
                vars.insert(name.clone(), v);
            } else {
                self.warn(key, 0, format!("`{name}` isn't exposed, so it can't be set on a placed instance"));
            }
        }

        let mut watch = HashMap::new();
        for item in &file.implement {
            if let ItemKind::Handler { trigger: Trigger::Changes(var), .. } = &item.kind {
                if let Some(v) = vars.get(var) {
                    watch.insert(var.clone(), v.clone());
                }
            }
        }

        // Taken now so spawns made later by this entity's handlers start with the defaults.
        let pose = self.spawn_pose.take().unwrap_or(([0.0; 3], [1.0; 3]));
        let id = self.entities.len();
        self.entities.push(Entity {
            id,
            file: key.to_string(),
            alive: true,
            tags: info.tags.clone(),
            vars,
            loc,
            rot: pose.0,
            scale: pose.1,
            shown: true,
            collides: true,
            parent: None,
            local_loc: [0.0; 3],
            local_rot: [0.0; 3],
            flow: HashMap::new(),
            due: HashMap::new(),
            watch,
            born: self.time,
        });

        for dep in &info.startup {
            self.ensure_instance(dep);
        }
        for item in &file.implement {
            if let ItemKind::Handler { trigger: Trigger::Created, body } = &item.kind {
                self.run_handler(id, key, body, Vec::new());
            }
        }
        id
    }

    /// The one entity running `key`, created if there isn't one yet. Calling an action on a file
    /// name (`do ShowCoins on Hud`) goes to this entity.
    pub(crate) fn ensure_instance(&mut self, key: &str) -> usize {
        match self.entities.iter().find(|e| e.alive && e.file == key) {
            Some(e) => e.id,
            None => self.spawn_inner(key, [0.0; 3], &[]),
        }
    }

    /// Whether the script `key` has an action called `action`.
    pub(crate) fn has_action(&self, key: &str, action: &str) -> bool {
        self.project
            .files
            .get(key)
            .and_then(|f| f.parsed.as_ref().ok())
            .is_some_and(|f| f.implement.iter().any(|i| matches!(&i.kind, ItemKind::Action { name, .. } if name == action)))
    }

    pub fn is_alive(&self, id: usize) -> bool {
        self.entities.get(id).is_some_and(|e| e.alive)
    }

    pub fn find_by_tag(&self, tag: &str) -> Option<usize> {
        self.entities.iter().find(|e| e.alive && e.tags.iter().any(|t| t == tag)).map(|e| e.id)
    }

    pub fn all_by_tag(&self, tag: &str) -> Vec<usize> {
        self.entities.iter().filter(|e| e.alive && e.tags.iter().any(|t| t == tag)).map(|e| e.id).collect()
    }

    pub fn get(&self, id: usize, var: &str) -> Option<&Val> {
        self.entities.get(id).and_then(|e| e.vars.get(var))
    }

    pub fn set(&mut self, id: usize, var: &str, value: Val) {
        if let Some(e) = self.entities.get_mut(id) {
            e.vars.insert(var.to_string(), value);
        }
    }

    pub(crate) fn warn(&mut self, key: &str, line: usize, msg: String) {
        self.warnings.push(format!("{key}:{line}: {msg}"));
    }

    // ---- running code ----

    pub(crate) fn run_handler(&mut self, id: usize, key: &str, body: &[Stmt], binds: Vec<(String, Val)>) {
        let info = self.infos[key].clone();
        let scope: HashMap<String, Val> = binds.into_iter().collect();
        let mut run = Run { w: self, key: key.to_string(), info, me: id, scopes: vec![scope], depth: 0, line: 0 };
        let _ = run.exec_block(body);
    }

    /// Call an action on an entity from outside the script (for tests and the engine).
    pub fn call(&mut self, id: usize, action: &str, args: Vec<(String, Val)>) -> Option<Val> {
        let key = self.entities.get(id)?.file.clone();
        let info = self.infos[&key].clone();
        let result = {
            let mut run = Run { w: self, key: key.clone(), info, me: id, scopes: vec![HashMap::new()], depth: 0, line: 0 };
            run.call_action(id, &key, action, args)
        };
        self.drain_events();
        result
    }

    /// Advance the world by `dt` seconds. In order: blocks whose `after` delay is up, then for each
    /// entity its input handlers, `every frame:` handlers, and due `every N seconds:` handlers.
    /// Events and variable changes are delivered after each one.
    pub fn step(&mut self, dt: f64) {
        self.dt = dt;
        self.time += dt;
        self.run_due_pending();
        self.run_timelines();

        let project = self.project.clone();
        let count = self.entities.len();
        for id in 0..count {
            if !self.entities[id].alive {
                continue;
            }
            let key = self.entities[id].file.clone();
            let file = project.files[&key].parsed.as_ref().expect("a world only holds files that parsed");
            for (idx, item) in file.implement.iter().enumerate() {
                if !self.entities[id].alive {
                    break;
                }
                let ItemKind::Handler { trigger, body } = &item.kind else { continue };
                match trigger {
                    Trigger::EveryFrame => self.run_handler(id, &key, body, Vec::new()),
                    Trigger::Input { system, input, kind } => {
                        if self.host.pressing(system, input, *kind) {
                            self.run_handler(id, &key, body, Vec::new());
                        }
                    }
                    Trigger::EverySeconds(secs) => {
                        let born = self.entities[id].born;
                        let due = *self.entities[id].due.entry(idx).or_insert(born + secs);
                        if self.time >= due {
                            let mut next = due + secs;
                            while next <= self.time {
                                next += secs;
                            }
                            self.entities[id].due.insert(idx, next);
                            self.run_handler(id, &key, body, Vec::new());
                        }
                    }
                    _ => {}
                }
            }
            self.drain_events();
        }
        self.follow_parents();
        self.host.end_frame();
    }

    /// Run every `animate` block one more frame. One that is finished (or whose entity is gone) is dropped.
    fn run_timelines(&mut self) {
        let count = self.timelines.len(); // blocks started during this frame run from the next one
        let mut finished: Vec<u64> = Vec::new();
        for idx in 0..count {
            let t = &self.timelines[idx];
            let (seq, entity, key, body, var, from, to, start, duration) =
                (t.seq, t.entity, t.key.clone(), t.body.clone(), t.var.clone(), t.from.clone(), t.to.clone(), t.start, t.duration);
            let mut scope = t.scope.clone();
            if !self.is_alive(entity) {
                finished.push(seq);
                continue;
            }
            let progress = if duration <= 0.0 { 1.0 } else { ((self.time - start) / duration).clamp(0.0, 1.0) };
            scope.insert(var, Val::mix(&from, &to, progress as f32));
            self.run_handler(entity, &key, &body, scope.into_iter().collect());
            self.drain_events();
            if progress >= 1.0 {
                finished.push(seq);
            }
        }
        self.timelines.retain(|t| !finished.contains(&t.seq));
    }

    /// `attach A to B`: A keeps where it is now, measured from B, and follows B from here on.
    pub(crate) fn attach(&mut self, child: usize, parent: usize) -> Result<(), String> {
        if child == parent {
            return Err("an entity can't be attached to itself".to_string());
        }
        // Following your own follower would go round in circles.
        let mut up = Some(parent);
        let mut hops = 0;
        while let Some(p) = up {
            if p == child {
                return Err("that would make two entities follow each other".to_string());
            }
            hops += 1;
            if hops > 64 {
                break;
            }
            up = self.entities[p].parent;
        }
        let (ploc, prot) = (self.entities[parent].loc, self.entities[parent].rot);
        let e = &mut self.entities[child];
        let offset = [e.loc[0] - ploc[0], e.loc[1] - ploc[1], e.loc[2] - ploc[2]];
        e.local_loc = rotate_by_euler_inverse(prot, offset);
        e.local_rot = [e.rot[0] - prot[0], e.rot[1] - prot[1], e.rot[2] - prot[2]];
        e.parent = Some(parent);
        Ok(())
    }

    /// Put every attached entity where its parent says it is.
    fn follow_parents(&mut self) {
        for id in 0..self.entities.len() {
            self.follow(id, 0);
        }
    }

    fn follow(&mut self, id: usize, depth: usize) {
        if depth > 64 || !self.entities[id].alive {
            return;
        }
        let Some(p) = self.entities[id].parent else { return };
        if !self.is_alive(p) {
            self.entities[id].parent = None;
            return;
        }
        self.follow(p, depth + 1);
        let (ploc, prot) = (self.entities[p].loc, self.entities[p].rot);
        let e = &mut self.entities[id];
        let offset = rotate_by_euler(prot, e.local_loc);
        e.loc = [ploc[0] + offset[0], ploc[1] + offset[1], ploc[2] + offset[2]];
        e.rot = [prot[0] + e.local_rot[0], prot[1] + e.local_rot[1], prot[2] + e.local_rot[2]];
    }

    /// `damage Foe by 5.0`: runs the target's `when damaged` handlers (immediately, like an action call).
    pub(crate) fn deal_damage(&mut self, target: usize, source: usize, amount: f32) {
        let project = self.project.clone();
        let key = self.entities[target].file.clone();
        let file = project.files[&key].parsed.as_ref().expect("a world only holds files that parsed");
        for item in &file.implement {
            let ItemKind::Handler { trigger: Trigger::Damaged { amount: amount_name, source: source_name }, body } = &item.kind else { continue };
            let mut binds = vec![(amount_name.clone(), Val::Dec(amount))];
            if let Some(s) = source_name {
                binds.push((s.clone(), Val::Entity(Some(source))));
            }
            self.run_handler(target, &key, body, binds);
        }
    }

    /// Damage an entity from outside the script (for tests and the engine); `source` is who did it, if anyone.
    pub fn damage(&mut self, target: usize, source: Option<usize>, amount: f32) {
        if self.is_alive(target) {
            self.deal_damage(target, source.unwrap_or(target), amount);
            self.drain_events();
        }
    }

    fn run_due_pending(&mut self) {
        let mut due = Vec::new();
        let mut i = 0;
        while i < self.pending.len() {
            if self.pending[i].due <= self.time {
                due.push(self.pending.remove(i));
            } else {
                i += 1;
            }
        }
        due.sort_by(|a, b| a.due.total_cmp(&b.due).then(a.seq.cmp(&b.seq)));
        for p in due {
            if self.is_alive(p.entity) {
                let binds: Vec<(String, Val)> = p.scope.into_iter().collect();
                self.run_handler(p.entity, &p.key, &p.body, binds);
                self.drain_events();
            }
        }
    }

    /// Tell the world two entities started touching. Each one's `when Self touches <File>` handler
    /// runs if the other runs that file.
    pub fn touch(&mut self, a: usize, b: usize) {
        self.touch_change(a, b, Overlap::Begin);
    }

    /// Tell the world two entities stopped touching (`when Self stops touching <File>`).
    pub fn untouch(&mut self, a: usize, b: usize) {
        self.touch_change(a, b, Overlap::End);
    }

    /// Tell the world two entities collided and blocked each other (`when Self hits <File>`).
    pub fn hit(&mut self, a: usize, b: usize) {
        self.touch_change(a, b, Overlap::Hit);
    }

    fn touch_change(&mut self, a: usize, b: usize, kind: Overlap) {
        for (x, y) in [(a, b), (b, a)] {
            if !self.is_alive(x) || y >= self.entities.len() {
                continue;
            }
            let project = self.project.clone();
            let key = self.entities[x].file.clone();
            let other_key = self.entities[y].file.clone();
            let info = self.infos[&key].clone();
            let file = project.files[&key].parsed.as_ref().expect("a world only holds files that parsed");
            for item in &file.implement {
                let ItemKind::Handler { trigger, body } = &item.kind else { continue };
                let (alias, name) = match (trigger, kind) {
                    (Trigger::Touches { file, name }, Overlap::Begin)
                    | (Trigger::StopsTouching { file, name }, Overlap::End)
                    | (Trigger::Hits { file, name }, Overlap::Hit) => (file, name),
                    _ => continue,
                };
                if info.aliases.get(alias) == Some(&other_key) {
                    self.run_handler(x, &key, body, vec![(name.clone(), Val::Entity(Some(y)))]);
                }
            }
        }
        self.drain_events();
    }

    /// Remove an entity. Its `when destroyed:` handlers run first.
    pub(crate) fn destroy(&mut self, id: usize) {
        if !self.is_alive(id) {
            return;
        }
        let project = self.project.clone();
        let key = self.entities[id].file.clone();
        let file = project.files[&key].parsed.as_ref().expect("a world only holds files that parsed");
        for item in &file.implement {
            if let ItemKind::Handler { trigger: Trigger::Destroyed, body } = &item.kind {
                self.run_handler(id, &key, body, Vec::new());
            }
        }
        self.entities[id].alive = false;
        // What was attached to it stays where it is.
        for e in self.entities.iter_mut() {
            if e.parent == Some(id) {
                e.parent = None;
            }
        }
    }

    /// Deliver queued events, then run `when <Variable> changes:` handlers for anything that
    /// changed, repeating until nothing more happens.
    pub(crate) fn drain_events(&mut self) {
        let mut rounds = 0;
        loop {
            self.deliver_queue();
            let changed = self.run_change_handlers();
            if !changed && self.queue.is_empty() {
                break;
            }
            rounds += 1;
            if rounds > 64 {
                self.warnings.push("variables and events kept triggering each other; the rest were dropped".to_string());
                self.queue.clear();
                break;
            }
        }
    }

    /// An event goes to its sender's own `when <Event>:` handlers, and to every entity with a
    /// `when <File> sends <Event> as <Name>:` handler for the sender's file.
    fn deliver_queue(&mut self) {
        let mut delivered = 0;
        while let Some((source, name)) = self.queue.pop_front() {
            delivered += 1;
            if delivered > 10_000 {
                self.warnings.push("events kept sending each other; the rest were dropped".to_string());
                self.queue.clear();
                break;
            }
            let project = self.project.clone();
            let source_key = self.entities[source].file.clone();

            if self.entities[source].alive {
                let file = project.files[&source_key].parsed.as_ref().expect("parsed");
                for item in &file.implement {
                    if let ItemKind::Handler { trigger: Trigger::Event(n), body } = &item.kind {
                        if *n == name {
                            self.run_handler(source, &source_key, body, Vec::new());
                        }
                    }
                }
            }

            for id in 0..self.entities.len() {
                if !self.entities[id].alive {
                    continue;
                }
                let key = self.entities[id].file.clone();
                let info = self.infos[&key].clone();
                let file = project.files[&key].parsed.as_ref().expect("parsed");
                for item in &file.implement {
                    if let ItemKind::Handler { trigger: Trigger::Sends { file: alias, event, name: bind }, body } = &item.kind {
                        if *event == name && info.aliases.get(alias) == Some(&source_key) {
                            self.run_handler(id, &key, body, vec![(bind.clone(), Val::Entity(Some(source)))]);
                        }
                    }
                }
            }
        }
    }

    /// Run the handler for every watched variable whose value differs from the last one seen.
    fn run_change_handlers(&mut self) -> bool {
        let project = self.project.clone();
        let mut any = false;
        for id in 0..self.entities.len() {
            if !self.entities[id].alive {
                continue;
            }
            let key = self.entities[id].file.clone();
            let file = project.files[&key].parsed.as_ref().expect("parsed");
            for item in &file.implement {
                let ItemKind::Handler { trigger: Trigger::Changes(var), body } = &item.kind else { continue };
                let current = self.entities[id].vars.get(var).cloned();
                let last = self.entities[id].watch.get(var).cloned();
                if current != last {
                    if let Some(c) = current {
                        self.entities[id].watch.insert(var.clone(), c);
                    }
                    any = true;
                    self.run_handler(id, &key, body, Vec::new());
                }
            }
        }
        any
    }
}
