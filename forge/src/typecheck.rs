//! The type checker: every name is declared, every value has the right type,
//! and every call matches the action it calls. Rules are in SPEC sections 3 to 6.

use crate::ast::*;
use crate::error::ForgeError;
use crate::manifest::Manifest;
use crate::project::{LoadedFile, Project};
use crate::ty::Type;
use std::collections::HashMap;

#[derive(Clone, Debug, PartialEq)]
enum Kind {
    Variable,
    Constant,
    Param,
    Local,
    Loop,
    Entity,
    File,
    System,
    Component,
    Event,
    /// The name given to an `only once`, `first N times` or `alternate` so `reset` can start it over.
    Latch,
    /// One of the values of an enum (`Idle`).
    EnumValue,
    /// A `table` (looked up with `row "Sword" of Items`).
    Table,
}

/// For entities and file names: which file's actions can be called on them.
#[derive(Clone, Debug, PartialEq)]
enum FileRef {
    None,
    /// The file couldn't be loaded; that problem is reported on its own, so stay quiet here.
    Broken,
    Key(String),
}

#[derive(Clone, Debug)]
struct Binding {
    kind: Kind,
    ty: Type,
    file: FileRef,
}

/// A value's type. A whole-number literal can stand in for a decimal (SPEC 3.1).
#[derive(Clone, Debug)]
pub struct Typed {
    pub ty: Type,
    /// A whole-number literal, which also fits where a decimal is wanted.
    pub int_lit: bool,
}

impl Typed {
    fn of(ty: Type) -> Option<Typed> {
        Some(Typed { ty, int_lit: false })
    }
}

struct ActionCtx {
    gives: Option<Type>,
    gave: bool,
}

pub fn typecheck(key: &str, project: &Project, manifest: &Manifest) -> Vec<ForgeError> {
    let Some(loaded) = project.files.get(key) else { return Vec::new() };
    let Ok(ast) = &loaded.parsed else { return Vec::new() };
    let mut c = Checker {
        manifest,
        project,
        loaded,
        globals: HashMap::new(),
        scopes: Vec::new(),
        errors: Vec::new(),
        action: None,
        loops: 0,
        enums: HashMap::new(),
        structs: HashMap::from([(
            "hit".to_string(),
            vec![
                ("Blocked".to_string(), Type::Boolean),
                ("Entity".to_string(), Type::Entity),
                ("Location".to_string(), Type::Vector3),
                ("Normal".to_string(), Type::Vector3),
                ("Distance".to_string(), Type::Decimal),
            ],
        )]),
        tables: HashMap::new(),
    };
    c.header(ast);
    c.latches(ast);
    c.implement(ast);
    c.errors
}

struct Checker<'a> {
    manifest: &'a Manifest,
    project: &'a Project,
    loaded: &'a LoadedFile,
    globals: HashMap<String, Binding>,
    scopes: Vec<HashMap<String, Binding>>,
    errors: Vec<ForgeError>,
    action: Option<ActionCtx>,
    loops: usize,
    /// The enums declared in this file: name -> its values.
    enums: HashMap<String, Vec<String>>,
    /// The structs this file can use (its own, and the built-in `hit`): name -> its fields.
    structs: HashMap<String, Vec<(String, Type)>>,
    /// The tables declared in this file: name -> the struct its rows are.
    tables: HashMap<String, String>,
}

fn part_keys(ty: &Type) -> &'static [&'static str] {
    match ty {
        Type::Vector2 => &["x", "y"],
        Type::Vector3 => &["x", "y", "z"],
        Type::Color => &["r", "g", "b", "a"],
        _ => &[],
    }
}

fn list_of(names: &[&str]) -> String {
    names.iter().map(|n| format!("`{n}`")).collect::<Vec<_>>().join(", ")
}

impl<'a> Checker<'a> {
    fn err(&mut self, line: usize, msg: impl Into<String>) {
        // Messages are written with "a `type`"; fix the article for the two types that start with a vowel.
        let msg = msg.into().replace("a `integer`", "an `integer`").replace("a `entity`", "an `entity`");
        self.errors.push(ForgeError::new(line, 1, msg));
    }

    fn lookup(&self, name: &str) -> Option<Binding> {
        for scope in self.scopes.iter().rev() {
            if let Some(b) = scope.get(name) {
                return Some(b.clone());
            }
        }
        self.globals.get(name).cloned()
    }

    fn bind_global(&mut self, name: &str, kind: Kind, ty: Type, file: FileRef) {
        // Duplicate names are reported by `check::check`; keep the first.
        self.globals.entry(name.to_string()).or_insert(Binding { kind, ty, file });
    }

    fn bind_local(&mut self, name: &str, kind: Kind, ty: Type, file: FileRef) {
        if let Some(scope) = self.scopes.last_mut() {
            scope.entry(name.to_string()).or_insert(Binding { kind, ty, file });
        }
    }

    // ---- header ----

    fn dep(&mut self, rel: &str, line: usize) -> FileRef {
        match self.loaded.deps.get(rel) {
            Some(Ok(key)) => {
                if self.project.files.get(key).is_some_and(|f| f.parsed.is_ok()) {
                    FileRef::Key(key.clone())
                } else {
                    FileRef::Broken
                }
            }
            Some(Err(reason)) => {
                self.err(line, format!("can't use `{rel}`: {reason}"));
                FileRef::Broken
            }
            None => FileRef::Broken,
        }
    }

    fn header(&mut self, ast: &File) {
        for d in &ast.header {
            let line = d.line;
            match &d.kind {
                DeclKind::Variable { name, ty, value, parts, entries, exposed } => {
                    let t = Type::from_expr(ty);
                    if *exposed && matches!(t, Type::Timer | Type::Gate | Type::List(_) | Type::Enum(_) | Type::Struct(_)) {
                        self.err(line, format!("a `{t}` can't be exposed; only plain values (numbers, text, booleans, vectors, colors, entities) can"));
                    }
                    self.initial(&t, value.as_ref(), parts, entries, line, false);
                    self.bind_global(name, Kind::Variable, t, FileRef::None);
                }
                DeclKind::Constant { name, ty, value, parts } => {
                    let t = Type::from_expr(ty);
                    self.initial(&t, value.as_ref(), parts, &[], line, true);
                    self.bind_global(name, Kind::Constant, t, FileRef::None);
                }
                DeclKind::NeedsComponent(n) => {
                    if !self.manifest.components.contains_key(n) {
                        let avail = list_of(&self.manifest.component_names());
                        self.err(line, format!("the engine has no component `{n}`. Available components: {avail}"));
                    }
                    self.bind_global(n, Kind::Component, Type::Entity, FileRef::None);
                }
                DeclKind::NeedsSystem(n) => {
                    if !self.manifest.systems.contains_key(n) {
                        let avail = list_of(&self.manifest.system_names());
                        self.err(line, format!("the engine has no system `{n}`. Available systems: {avail}"));
                    }
                    self.bind_global(n, Kind::System, Type::Entity, FileRef::None);
                }
                DeclKind::NeedsFile { file, alias, when } => {
                    let fr = self.dep(file, line);
                    if let Some(w) = when {
                        if w != "used" && w != "startup" {
                            self.err(line, format!("`when:{w}` isn't a loading mode; I expected `when:used` or `when:startup`"));
                        }
                    }
                    match alias {
                        Some(a) => self.bind_global(a, Kind::File, Type::Entity, fr),
                        None => self.err(line, "`needs file` needs a name to call it by, like `as Coin`"),
                    }
                }
                DeclKind::NeedsEntity { name, file, .. } => {
                    let fr = self.dep(file, line);
                    self.bind_global(name, Kind::Entity, Type::Entity, fr);
                }
                DeclKind::NeedsEntities { name, file, .. } => {
                    let fr = match file {
                        Some(f) => self.dep(f, line),
                        None => FileRef::None,
                    };
                    self.bind_global(name, Kind::Entity, Type::List(Box::new(Type::Entity)), fr);
                }
                DeclKind::Event(n) => self.bind_global(n, Kind::Event, Type::Entity, FileRef::None),
                DeclKind::Tag(_) => {}
                DeclKind::Enum { name, values } => {
                    self.enums.insert(name.clone(), values.clone());
                    for v in values {
                        self.bind_global(v, Kind::EnumValue, Type::Enum(name.clone()), FileRef::None);
                    }
                }
                DeclKind::Struct { name, fields } => {
                    let mut seen: Vec<&str> = Vec::new();
                    let mut typed = Vec::new();
                    for (f, fty) in fields {
                        let t = Type::from_expr(fty);
                        if matches!(f.as_str(), "type" | "key" | "as") {
                            self.err(line, format!("`{f}` can't be the name of a field: it has a meaning inside the lines that give a table its rows"));
                        }
                        if matches!(f.as_str(), "X" | "Y" | "Z" | "R" | "G" | "B" | "A" | "Pitch" | "Yaw" | "Roll") {
                            self.err(line, format!("`{f}` can't be the name of a field: `Name.{f}` already means a part of a vector or color"));
                        }
                        if seen.contains(&f.as_str()) {
                            self.err(line, format!("`{name}` has two fields called `{f}`"));
                        }
                        seen.push(f);
                        if matches!(t, Type::Timer | Type::Gate) {
                            self.err(line, format!("a struct holds values, so a field can't be a `{t}`"));
                        }
                        typed.push((f.clone(), t));
                    }
                    self.structs.insert(name.clone(), typed);
                }
                DeclKind::Table { name, ty, rows } => {
                    self.tables.insert(name.clone(), ty.clone());
                    self.bind_global(name, Kind::Table, Type::Struct(ty.clone()), FileRef::None);
                    let fields = self.structs.get(ty).cloned().unwrap_or_default();
                    let mut keys: Vec<&str> = Vec::new();
                    for row in rows {
                        if keys.contains(&row.key.as_str()) {
                            self.err(line, format!("`{name}` has two rows with the key \"{}\"", row.key));
                        }
                        keys.push(&row.key);
                        for (f, t) in &row.values {
                            let Some((_, fty)) = fields.iter().find(|(n, _)| n == f) else {
                                let names: Vec<&str> = fields.iter().map(|(n, _)| n.as_str()).collect();
                                self.err(line, format!("`{ty}` has no field `{f}`. Its fields: {}", list_of(&names)));
                                continue;
                            };
                            let fixed = matches!(t, Term::Int(_) | Term::Decimal(_) | Term::Text(_) | Term::Bool(_))
                                || matches!(t, Term::Name(n) if matches!(self.globals.get(n), Some(b) if b.kind == Kind::EnumValue));
                            if !fixed || !matches!(fty, Type::Integer | Type::Decimal | Type::Boolean | Type::Text | Type::Enum(_)) {
                                self.err(line, format!("a row can give numbers, text, booleans and enum values, so `{f}:` can't be set to this (the field is a `{fty}`)"));
                                continue;
                            }
                            self.expect_term(t, fty, line, &format!("`{f}:`"));
                        }
                    }
                }
            }
        }
    }

    /// Check a variable's or constant's starting value, parts, and entries.
    fn initial(&mut self, ty: &Type, value: Option<&Term>, parts: &Parts, entries: &[Parts], line: usize, constant: bool) {
        if constant && matches!(ty, Type::Timer | Type::Gate | Type::Entity | Type::List(_) | Type::Struct(_)) {
            self.err(line, format!("a constant can't be a `{ty}`; use `variable` instead"));
            return;
        }
        if matches!(ty, Type::Timer | Type::Entity) && (value.is_some() || !parts.is_empty()) {
            self.err(line, format!("a `{ty}` always starts out empty, so it can't have a starting value"));
            return;
        }
        if value.is_some() && !parts.is_empty() {
            self.err(line, "give a starting value with either `value:` or named parts like `x:0 y:1 z:0`, not both");
            return;
        }
        if let Some(v) = value {
            if *ty == Type::Gate {
                if !matches!(v, Term::Name(n) if n == "open" || n == "closed") {
                    self.err(line, "a gate starts `value:open` (the default) or `value:closed`");
                }
            } else if let Type::Enum(en) = ty {
                let values = self.enums.get(en).cloned().unwrap_or_default();
                if !matches!(v, Term::Name(n) if values.contains(n)) {
                    let names: Vec<&str> = values.iter().map(String::as_str).collect();
                    self.err(line, format!("a `{en}` starts as one of its values: {}", list_of(&names)));
                }
            } else if matches!(ty, Type::Struct(_)) {
                self.err(line, format!("a `{ty}` starts out with its fields at their defaults; change them with `make Name.Field value`, or give it a whole one with `new`"));
            } else if matches!(ty, Type::List(_)) {
                self.err(line, "a list starts out empty, or with indented `entry` lines under it");
            } else if matches!(ty, Type::Vector2 | Type::Vector3 | Type::Color) {
                let keys = part_keys(ty);
                self.err(line, format!("write a `{ty}` with named parts ({}), not `value:`", list_of(keys)));
            } else {
                self.expect_term(v, ty, line, "the starting value");
            }
        }
        self.check_parts(ty, parts, line);

        if !entries.is_empty() {
            match ty {
                Type::List(el) if !part_keys(el).is_empty() => {
                    for entry in entries {
                        self.check_parts(el, entry, line);
                    }
                }
                Type::List(el) => self.err(line, format!("`entry` lines only work for lists of vectors or colors, not `list of {el}`")),
                _ => self.err(line, "`entry` lines only work under a list"),
            }
        }
    }

    fn check_parts(&mut self, ty: &Type, parts: &Parts, line: usize) {
        let keys = part_keys(ty);
        for (k, t) in parts {
            if !keys.contains(&k.as_str()) {
                if keys.is_empty() {
                    self.err(line, format!("`{k}:` isn't used for a `{ty}`"));
                } else {
                    self.err(line, format!("`{k}:` isn't a part of a `{ty}`; its parts are {}", list_of(keys)));
                }
                continue;
            }
            self.expect_term(t, &Type::Decimal, line, &format!("`{k}:`"));
        }
    }

    // ---- implement ----

    /// The names `only once as X`, `first N times as X` and `alternate as X` give, so `reset X` can find them.
    fn latches(&mut self, ast: &File) {
        fn walk(body: &[Stmt], out: &mut Vec<String>) {
            for s in body {
                match &s.kind {
                    StmtKind::OnlyOnce { name, body } | StmtKind::FirstTimes { name, body, .. } => {
                        out.extend(name.clone());
                        walk(body, out);
                    }
                    StmtKind::Alternate { name, first, second } => {
                        out.extend(name.clone());
                        walk(first, out);
                        walk(second, out);
                    }
                    StmtKind::If { branches, otherwise, .. } => {
                        for (_, b) in branches {
                            walk(b, out);
                        }
                        if let Some(b) = otherwise {
                            walk(b, out);
                        }
                    }
                    StmtKind::Choose { cases, otherwise, .. } => {
                        for c in cases {
                            walk(&c.body, out);
                        }
                        if let Some(b) = otherwise {
                            walk(b, out);
                        }
                    }
                    StmtKind::Repeat { body, .. }
                    | StmtKind::ForEach { body, .. }
                    | StmtKind::ForRange { body, .. }
                    | StmtKind::While { body, .. }
                    | StmtKind::After { body, .. }
                    | StmtKind::Through { body, .. }
                    | StmtKind::Animate { body, .. } => walk(body, out),
                    _ => {}
                }
            }
        }
        let mut names = Vec::new();
        for item in &ast.implement {
            match &item.kind {
                ItemKind::Handler { body, .. } | ItemKind::Action { body, .. } => walk(body, &mut names),
            }
        }
        for n in names {
            self.bind_global(&n, Kind::Latch, Type::Boolean, FileRef::None);
        }
    }

    fn implement(&mut self, ast: &File) {
        for item in &ast.implement {
            let line = item.line;
            match &item.kind {
                ItemKind::Handler { trigger, body } => {
                    self.scopes.push(HashMap::new());
                    match trigger {
                        Trigger::Damaged { amount, source } => {
                            self.bind_local(amount, Kind::Param, Type::Decimal, FileRef::None);
                            if let Some(s) = source {
                                self.bind_local(s, Kind::Entity, Type::Entity, FileRef::None);
                            }
                        }
                        Trigger::Touches { file, name } | Trigger::StopsTouching { file, name } | Trigger::Hits { file, name } => {
                            if let Some(fr) = self.file_alias(file, line) {
                                self.bind_local(name, Kind::Entity, Type::Entity, fr);
                            }
                        }
                        Trigger::Changes(var) => match self.lookup(var) {
                            Some(b) if b.kind == Kind::Variable && b.ty != Type::Timer => {}
                            Some(_) => self.err(line, format!("`{var}` isn't a variable that can change, so there is nothing to watch")),
                            None => self.unknown(var, line),
                        },
                        Trigger::Input { system, input, .. } => self.check_input(system, input, line),
                        Trigger::Sends { file, event, name } => {
                            if let Some(fr) = self.file_alias(file, line) {
                                if let FileRef::Key(k) = &fr {
                                    let sends = self.project.files.get(k).is_some_and(|f| f.sig.events.contains(event));
                                    if !sends {
                                        self.err(line, format!("`{file}` doesn't have an event called `{event}`"));
                                    }
                                }
                                self.bind_local(name, Kind::Entity, Type::Entity, fr);
                            }
                        }
                        Trigger::Event(n) => {
                            if !matches!(self.lookup(n), Some(b) if b.kind == Kind::Event) {
                                self.err(line, format!("`{n}` isn't an event declared in `header:`; add `event name:{n}`"));
                            }
                        }
                        Trigger::Created | Trigger::Destroyed | Trigger::EveryFrame | Trigger::EverySeconds(_) => {}
                    }
                    self.block(body);
                    self.scopes.pop();
                }
                ItemKind::Action { name, params, gives, body } => {
                    self.scopes.push(HashMap::new());
                    for p in params {
                        let ty = Type::from_expr(&p.ty);
                        self.bind_local(&p.name, Kind::Param, ty, FileRef::None);
                    }
                    self.action = Some(ActionCtx { gives: gives.as_ref().map(Type::from_expr), gave: false });
                    self.block(body);
                    if let Some(ctx) = self.action.take() {
                        if let (Some(t), false) = (&ctx.gives, ctx.gave) {
                            self.err(line, format!("`{name}` says it gives `{t}` but never does `give back`"));
                        }
                    }
                    self.scopes.pop();
                }
            }
        }
    }

    fn file_alias(&mut self, name: &str, line: usize) -> Option<FileRef> {
        match self.lookup(name) {
            Some(b) if b.kind == Kind::File => Some(b.file),
            _ => {
                self.err(line, format!("`{name}` isn't a file from the header; add `needs file:\"...\" as {name}`"));
                None
            }
        }
    }

    fn block(&mut self, body: &[Stmt]) {
        self.scopes.push(HashMap::new());
        for s in body {
            self.stmt(s);
        }
        self.scopes.pop();
    }

    // ---- statements ----

    fn stmt(&mut self, s: &Stmt) {
        let line = s.line;
        match &s.kind {
            StmtKind::Make { target, value, limit } => {
                if let Some(tt) = self.target(target, line) {
                    self.expect_value(value, &tt, line, "this value");
                    self.limits(limit, &tt, line);
                } else {
                    self.value_type(value, line);
                }
            }
            StmtKind::Add { value, target, limit } => match self.target(target, line) {
                Some(Type::List(el)) => {
                    if limit.capped.is_some() || limit.floored.is_some() {
                        self.err(line, "`capped at` and `floored at` only work on numbers, not lists");
                    }
                    self.expect_value(value, &el, line, "the item to add");
                }
                Some(tt) if tt.is_numeric() || tt == Type::Vector2 || tt == Type::Vector3 => {
                    self.expect_value(value, &tt, line, "what you add");
                    self.limits(limit, &tt, line);
                }
                Some(tt) => self.err(line, format!("you can't `add` to a `{tt}`; use `make`, or `joined with` for text")),
                None => {
                    self.value_type(value, line);
                }
            },
            StmtKind::Subtract { value, target, limit } => match self.target(target, line) {
                Some(tt) if tt.is_numeric() || tt == Type::Vector2 || tt == Type::Vector3 => {
                    self.expect_value(value, &tt, line, "what you subtract");
                    self.limits(limit, &tt, line);
                }
                Some(tt) => self.err(line, format!("you can't `subtract` from a `{tt}`")),
                None => {
                    self.value_type(value, line);
                }
            },
            StmtKind::Multiply { target, value } | StmtKind::Divide { target, value } => {
                let verb = if matches!(s.kind, StmtKind::Multiply { .. }) { "multiply" } else { "divide" };
                match self.target(target, line) {
                    Some(tt) if tt.is_numeric() => {
                        self.expect_value(value, &tt, line, "the amount");
                    }
                    Some(Type::Vector3) | Some(Type::Vector2) => {
                        self.expect_value(value, &Type::Decimal, line, "the amount");
                    }
                    Some(tt) => self.err(line, format!("you can't `{verb}` a `{tt}`")),
                    None => {
                        self.value_type(value, line);
                    }
                }
            }
            StmtKind::Remove { value, list } => match self.term_type(list, line).map(|t| t.ty) {
                Some(Type::List(el)) => {
                    self.expect_value(value, &el, line, "the item to remove");
                }
                Some(other) => self.err(line, format!("`remove ... from` needs a list, but this is a `{other}`")),
                None => {}
            },
            StmtKind::RemoveAt { index, list } => {
                self.expect_term(index, &Type::Integer, line, "the position to remove");
                match self.term_type(list, line).map(|t| t.ty) {
                    Some(Type::List(_)) | None => {}
                    Some(other) => self.err(line, format!("`remove item ... from` needs a list, but this is a `{other}`")),
                }
            }
            StmtKind::AddUnique { value, list } => match self.target(list, line) {
                Some(Type::List(el)) => {
                    self.expect_value(value, &el, line, "the item to add");
                }
                Some(other) => {
                    self.err(line, format!("`, if new` only works when adding to a list, but this is a `{other}`"));
                    self.value_type(value, line);
                }
                None => {
                    self.value_type(value, line);
                }
            },
            StmtKind::Clear(list) => match self.target(list, line) {
                Some(Type::List(_)) | None => {}
                Some(other) => self.err(line, format!("`clear` empties a list, but this is a `{other}`")),
            },
            StmtKind::OnlyOnce { body, .. } => self.block(body),
            StmtKind::FirstTimes { count, body, .. } => {
                self.expect_term(count, &Type::Integer, line, "how many times");
                self.block(body);
            }
            StmtKind::Alternate { first, second, .. } => {
                self.block(first);
                self.block(second);
            }
            StmtKind::GateChange { gate, .. } => self.check_gate(gate, line),
            StmtKind::Through { gate, body } => {
                self.check_gate(gate, line);
                self.block(body);
            }
            StmtKind::Choose { value, cases, otherwise } => {
                let chosen = self.term_type(value, line);
                let mut seen: Vec<Term> = Vec::new();
                for case in cases {
                    for v in &case.values {
                        if let Some(Typed { ty: Type::Enum(en), .. }) = &chosen {
                            let values = self.enums.get(en).cloned().unwrap_or_default();
                            if !matches!(v, Term::Name(n) if values.contains(n)) {
                                let names: Vec<&str> = values.iter().map(String::as_str).collect();
                                self.err(line, format!("this `case` has to be one of the values of `{en}`: {}", list_of(&names)));
                            } else if seen.contains(v) {
                                self.err(line, "two cases can't have the same value");
                            } else {
                                seen.push(v.clone());
                            }
                            continue;
                        }
                        if !matches!(v, Term::Int(_) | Term::Text(_) | Term::Bool(_)) {
                            self.err(line, "a `case` is a fixed value: a whole number, text, or true/false");
                            continue;
                        }
                        if let Some(c) = &chosen {
                            if !matches!(c.ty, Type::Integer | Type::Text | Type::Boolean) {
                                self.err(line, format!("`choose` picks by a whole number, text, boolean, or enum, but this is a `{}`", c.ty));
                                break;
                            }
                            self.expect_term(v, &c.ty, line, "this `case`");
                        }
                        if seen.contains(v) {
                            self.err(line, "two cases can't have the same value");
                        }
                        seen.push(v.clone());
                    }
                    self.block(&case.body);
                }
                if let Some(b) = otherwise {
                    self.block(b);
                }
            }
            StmtKind::Animate { var, from, to, seconds, body } => {
                let a = self.term_type(from, line);
                let b = self.term_type(to, line);
                let mut ty = Type::Decimal;
                if let (Some(a), Some(b)) = (&a, &b) {
                    let both_decimal = (a.ty == Type::Decimal || (a.ty == Type::Integer && a.int_lit)) && (b.ty == Type::Decimal || (b.ty == Type::Integer && b.int_lit));
                    if both_decimal {
                        ty = Type::Decimal;
                    } else if a.ty == b.ty && matches!(a.ty, Type::Vector2 | Type::Vector3) {
                        ty = a.ty.clone();
                    } else {
                        self.err(line, format!("`animate` moves a decimal or a vector from one value to another of the same kind, but this is a `{}` and a `{}`", a.ty, b.ty));
                    }
                }
                if let Some(t) = self.term_type(seconds, line) {
                    if t.ty != Type::Duration && !t.ty.is_numeric() {
                        self.err(line, format!("`over` needs a number of seconds, but this is a `{}`", t.ty));
                    }
                }
                // Like `after`, the block runs later and on its own.
                let saved_loops = std::mem::take(&mut self.loops);
                let saved_action = self.action.take();
                self.scopes.push(HashMap::new());
                self.bind_local(var, Kind::Loop, ty, FileRef::None);
                self.block(body);
                self.scopes.pop();
                self.loops = saved_loops;
                self.action = saved_action;
            }
            StmtKind::Damage { target, amount } => {
                self.expect_term(target, &Type::Entity, line, "what you damage");
                self.expect_value(amount, &Type::Decimal, line, "the amount of damage");
            }
            StmtKind::Attach { child, parent } => {
                self.expect_term(child, &Type::Entity, line, "what you attach");
                self.expect_term(parent, &Type::Entity, line, "what you attach it to");
            }
            StmtKind::Detach(child) => {
                self.expect_term(child, &Type::Entity, line, "what you detach");
            }
            StmtKind::Tell { target, args, .. } => {
                self.expect_term(target, &Type::Entity, line, "who you tell");
                for a in args {
                    if let Some(t) = self.value_type(&a.value, line) {
                        if !matches!(t.ty, Type::Integer | Type::Decimal | Type::Boolean | Type::Text | Type::Vector2 | Type::Vector3 | Type::Color | Type::Entity) {
                            self.err(line, format!("`{}:` can't be sent with `tell`: a message carries numbers, text, booleans, vectors, colors and entities, not a `{}`", a.name, t.ty));
                        }
                    }
                }
            }
            StmtKind::Do { action, on, args } => {
                self.call(action, on.as_deref(), args, line);
            }
            StmtKind::Send(event) => {
                if !matches!(self.globals.get(event), Some(b) if b.kind == Kind::Event) {
                    self.err(line, format!("`{event}` isn't an event declared in `header:`; add `event name:{event}`"));
                }
            }
            StmtKind::Remember { name, value } => {
                if let Some(t) = self.value_type(value, line) {
                    if t.ty == Type::Duration {
                        self.err(line, "you can't `remember` a duration; durations only work next to a timer");
                    } else {
                        let fr = match value {
                            Value::Term(Term::Name(n)) => self.lookup(n).map(|b| b.file).unwrap_or(FileRef::None),
                            Value::CastTo { file, .. } => match self.lookup(file) {
                                Some(b) if b.kind == Kind::File => b.file,
                                _ => FileRef::None,
                            },
                            _ => FileRef::None,
                        };
                        self.bind_local(name, Kind::Local, t.ty, fr);
                    }
                }
            }
            StmtKind::Reset(name) => match self.lookup(name) {
                Some(b) if b.ty == Type::Timer || b.kind == Kind::Latch => {}
                Some(_) => self.err(line, format!("`{name}` isn't a timer or a named `only once`, so it can't be reset")),
                None => self.unknown(name, line),
            },
            StmtKind::Spawn { file, at, name } => {
                let fr = self.file_alias(file, line);
                self.expect_value(at, &Type::Vector3, line, "`at:`");
                if let Some(fr) = fr {
                    self.bind_local(name, Kind::Entity, Type::Entity, fr);
                }
            }
            StmtKind::Move { target, toward, by } => {
                if let Some(tt) = self.target(target, line) {
                    if tt != Type::Vector3 {
                        self.err(line, format!("`move` changes a position (`vector3`), but this is a `{tt}`"));
                    }
                }
                self.expect_value(toward, &Type::Vector3, line, "where to move toward");
                self.expect_value(by, &Type::Decimal, line, "how far to move");
            }
            StmtKind::MoveDir { target, by, .. } => {
                if self.expect_term(target, &Type::Entity, line, "what you move") {
                    let is_self = matches!(target, Term::Name(n) if n == "Self");
                    if is_self && !matches!(self.globals.get("Transform"), Some(b) if b.kind == Kind::Component) {
                        self.err(line, "add `needs component:Transform` to the header before moving `Self`");
                    }
                }
                self.expect_value(by, &Type::Decimal, line, "how far to move");
            }
            StmtKind::Print(v) => {
                if let Some(t) = self.value_type(v, line) {
                    if !t.ty.is_printable() {
                        self.err(line, format!("`print` shows numbers, text, booleans, vectors, and colors, not a `{}`", t.ty));
                    }
                }
            }
            StmtKind::After { seconds, body } => {
                if let Some(t) = self.term_type(seconds, line) {
                    if t.ty != Type::Duration && !t.ty.is_numeric() {
                        self.err(line, format!("`after` needs a number of seconds, but this is a `{}`", t.ty));
                    }
                }
                // The block runs later, on its own: it can't `give back`, and loops around it don't reach in.
                let saved_loops = std::mem::take(&mut self.loops);
                let saved_action = self.action.take();
                self.block(body);
                self.loops = saved_loops;
                self.action = saved_action;
            }
            StmtKind::ForRange { var, from, to, body } => {
                self.expect_term(from, &Type::Integer, line, "where the count starts");
                self.expect_term(to, &Type::Integer, line, "where the count ends");
                self.scopes.push(HashMap::new());
                self.bind_local(var, Kind::Loop, Type::Integer, FileRef::None);
                self.loops += 1;
                self.block(body);
                self.loops -= 1;
                self.scopes.pop();
            }
            StmtKind::Destroy(t) => {
                self.expect_term(t, &Type::Entity, line, "what you destroy");
            }
            StmtKind::GiveBack(v) => {
                let gives = self.action.as_ref().and_then(|a| a.gives.clone());
                match gives {
                    Some(t) => {
                        self.expect_value(v, &t, line, "what you give back");
                        if let Some(a) = self.action.as_mut() {
                            a.gave = true;
                        }
                    }
                    None => {
                        self.err(line, "`give back` only works inside an action that says `gives <type>`");
                        self.value_type(v, line);
                    }
                }
            }
            StmtKind::Finish => {}
            StmtKind::StopLoop | StmtKind::Skip => {
                if self.loops == 0 {
                    self.err(line, "`stop loop` and `skip` only work inside a loop");
                }
            }
            StmtKind::If { branches, otherwise, .. } => {
                for (cond, body) in branches {
                    self.condition(cond, line);
                    self.block(body);
                }
                if let Some(body) = otherwise {
                    self.block(body);
                }
            }
            StmtKind::Repeat { times, body } => {
                self.expect_term(times, &Type::Integer, line, "how many times");
                self.loops += 1;
                self.block(body);
                self.loops -= 1;
            }
            StmtKind::ForEach { var, list, body } => {
                self.scopes.push(HashMap::new());
                if let Some(Typed { ty: Type::List(el), .. }) = self.term_type(list, line) {
                    let fr = match list {
                        Term::Name(n) => self.lookup(n).map(|b| b.file).unwrap_or(FileRef::None),
                        _ => FileRef::None,
                    };
                    self.bind_local(var, Kind::Loop, *el, fr);
                } else if self.errors.last().is_none_or(|e| e.line != line) {
                    self.err(line, "`for each` needs a list to go through");
                }
                self.loops += 1;
                self.block(body);
                self.loops -= 1;
                self.scopes.pop();
            }
            StmtKind::While { cond, body } => {
                self.condition(cond, line);
                self.loops += 1;
                self.block(body);
                self.loops -= 1;
            }
        }
    }

    /// `open Door`, `through Door:`: the name must be a gate.
    fn check_gate(&mut self, name: &str, line: usize) {
        match self.lookup(name) {
            Some(b) if b.ty == Type::Gate => {}
            Some(_) => self.err(line, format!("`{name}` isn't a gate; declare one with `variable name:{name} type:gate`")),
            None => self.unknown(name, line),
        }
    }

    fn unknown(&mut self, name: &str, line: usize) {
        self.err(
            line,
            format!("I don't know `{name}`. Declare it in `header:` first, or `remember` it before using it"),
        );
    }

    /// The type of something you can change, or an error if it can't be changed.
    fn target(&mut self, t: &Term, line: usize) -> Option<Type> {
        match t {
            Term::Name(n) => match self.lookup(n) {
                None => {
                    self.unknown(n, line);
                    None
                }
                Some(b) => match b.kind {
                    Kind::Variable | Kind::Local => {
                        if b.ty == Type::Timer {
                            self.err(line, format!("`{n}` is a timer; use `reset {n}` to restart it"));
                            None
                        } else if b.ty == Type::Gate {
                            self.err(line, format!("`{n}` is a gate; use `open {n}`, `close {n}` or `toggle {n}`"));
                            None
                        } else {
                            Some(b.ty)
                        }
                    }
                    Kind::Constant => {
                        self.err(line, format!("`{n}` is a constant and can't be changed; declare it with `variable` if it needs to change"));
                        None
                    }
                    Kind::Param | Kind::Loop => {
                        self.err(line, format!("`{n}` can't be changed here; `remember` a copy of it first"));
                        None
                    }
                    _ => {
                        self.err(line, format!("`{n}` can't be changed"));
                        None
                    }
                },
            },
            Term::Path { segments, .. } => {
                // A part of a variable (`Velocity.X`) can be changed when the variable can.
                if !self.manifest.components.contains_key(&segments[0]) {
                    if let Some(b) = self.lookup(&segments[0]) {
                        let root = &segments[0];
                        match b.kind {
                            Kind::Constant => {
                                self.err(line, format!("`{root}` is a constant and can't be changed; declare it with `variable` if it needs to change"));
                                return None;
                            }
                            Kind::Param | Kind::Loop => {
                                self.err(line, format!("`{root}` can't be changed here; `remember` a copy of it first"));
                                return None;
                            }
                            _ => {}
                        }
                    }
                }
                self.term_type(t, line).map(|t| t.ty)
            }
            _ => {
                self.err(line, "only a variable or a field can be changed, not a fixed value");
                None
            }
        }
    }

    fn limits(&mut self, limit: &Limit, ty: &Type, line: usize) {
        for (word, t) in [("capped at", &limit.capped), ("floored at", &limit.floored)] {
            if let Some(t) = t {
                if !ty.is_numeric() {
                    self.err(line, format!("`{word}` only works on numbers, not a `{ty}`"));
                } else {
                    self.expect_term(t, ty, line, &format!("the `{word}` limit"));
                }
            }
        }
    }

    // ---- calls ----

    /// Check a call. Returns `Some(gives)` if the action was found.
    fn call(&mut self, action: &str, on: Option<&str>, args: &[Arg], line: usize) -> Option<Option<Type>> {
        let mut optional: Vec<String> = Vec::new();
        let (params, gives): (Vec<(String, Type)>, Option<Type>) = match on {
            None | Some("Self") => match self.loaded.sig.actions.get(action) {
                Some(sig) => (sig.params.clone(), sig.gives.clone()),
                None => {
                    self.err(line, format!("this file has no action called `{action}`"));
                    return None;
                }
            },
            Some(n) => {
                let Some(b) = self.lookup(n) else {
                    self.unknown(n, line);
                    return None;
                };
                if b.kind == Kind::System {
                    let found = self.manifest.systems.get(n).and_then(|s| s.actions.get(action)).cloned();
                    optional = self.manifest.systems.get(n).and_then(|s| s.optional.get(action)).cloned().unwrap_or_default();
                    match found {
                        Some(p) => (p, None),
                        None => {
                            let mut names: Vec<&str> = self
                                .manifest
                                .systems
                                .get(n)
                                .map(|s| s.actions.keys().map(|k| k.as_str()).collect())
                                .unwrap_or_default();
                            names.sort();
                            self.err(line, format!("`{n}` has no action called `{action}`. Its actions: {}", list_of(&names)));
                            return None;
                        }
                    }
                } else if b.kind == Kind::File || b.ty == Type::Entity {
                    match &b.file {
                        FileRef::Broken => return None,
                        FileRef::None => {
                            self.err(
                                line,
                                format!("I don't know which file `{n}` runs, so I can't check `{action}` on it"),
                            );
                            return None;
                        }
                        FileRef::Key(k) => {
                            let sig = self.project.files.get(k).and_then(|f| f.sig.actions.get(action)).cloned();
                            match sig {
                                Some(s) if s.params.iter().any(|(_, t)| holds_struct(t)) || s.gives.as_ref().is_some_and(holds_struct) => {
                                    self.err(line, format!("`{action}` takes or gives a struct, and a struct belongs to the script that declares it, so another script can't call it"));
                                    return None;
                                }
                                Some(s) => (s.params, s.gives),
                                None => {
                                    self.err(line, format!("`{n}` has no action called `{action}`"));
                                    return None;
                                }
                            }
                        }
                    }
                } else {
                    self.err(line, format!("you can't call actions `on {n}`"));
                    return None;
                }
            }
        };

        for (pname, pty) in &params {
            match args.iter().find(|a| a.name == *pname) {
                Some(a) => {
                    self.expect_value(&a.value, pty, line, &format!("`{pname}:`"));
                }
                None if optional.contains(pname) => {}
                None => self.err(line, format!("`{action}` needs `{pname}:` (a `{pty}`)")),
            }
        }
        for a in args {
            if !params.iter().any(|(p, _)| *p == a.name) {
                let known: Vec<&str> = params.iter().map(|(p, _)| p.as_str()).collect();
                let hint = if known.is_empty() { "it takes no parameters".to_string() } else { format!("it takes {}", list_of(&known)) };
                self.err(line, format!("`{action}` has no `{}:`; {hint}", a.name));
            }
        }
        Some(gives)
    }

    // ---- conditions ----

    fn condition(&mut self, c: &Condition, line: usize) {
        self.comparison(&c.first, line);
        for r in &c.rest {
            self.comparison(r, line);
        }
    }

    fn comparison(&mut self, c: &Comparison, line: usize) {
        match c {
            Comparison::Compare { left, op, right } => {
                let (Some(l), Some(r)) = (self.value_type(left, line), self.value_type(right, line)) else { return };
                let ordering = matches!(op, CmpOp::Above | CmpOp::Below | CmpOp::AtLeast | CmpOp::AtMost);
                if l.ty == Type::Timer {
                    if r.ty != Type::Duration {
                        self.err(line, format!("a timer is compared with a duration like `10 seconds`, but this is a `{}`", r.ty));
                    } else if !ordering {
                        self.err(line, "a timer can be `above`, `below`, `at least`, or `at most` a duration, but not equal to one");
                    }
                    return;
                }
                if r.ty == Type::Duration || l.ty == Type::Duration {
                    self.err(line, "a duration like `10 seconds` can only be compared with a timer");
                    return;
                }
                let same = compatible(&l.ty, &r) || compatible(&r.ty, &l);
                if !same {
                    self.err(line, format!("can't compare a `{}` with a `{}`", l.ty, r.ty));
                } else if ordering && !l.ty.is_numeric() {
                    self.err(line, format!("`above`, `below`, `at least`, and `at most` compare numbers, not a `{}`", l.ty));
                }
            }
            Comparison::Contains { list, item, .. } => {
                let l = self.value_type(list, line);
                let i = self.value_type(item, line);
                if let (Some(l), Some(i)) = (l, i) {
                    match &l.ty {
                        Type::List(el) => {
                            self.compare_expected(el, &i, line, "the item you look for");
                        }
                        other => self.err(line, format!("`contains` looks in a list, but this is a `{other}`")),
                    }
                }
            }
            Comparison::Pressing { system, input, .. } => self.check_input(system, input, line),
            Comparison::Presence { value, .. } => {
                if let Some(t) = self.value_type(value, line) {
                    if t.ty != Type::Entity {
                        self.err(line, format!("`is missing` and `is present` check an entity, but this is a `{}`", t.ty));
                    }
                }
            }
        }
    }

    /// `<system> is pressing <input>` (and the handler form): the system must be declared and know the input.
    fn check_input(&mut self, system: &str, input: &str, line: usize) {
        match self.lookup(system) {
            Some(b) if b.kind == Kind::System => {
                let inputs = self.manifest.systems.get(system).map(|s| s.inputs.clone()).unwrap_or_default();
                if inputs.is_empty() {
                    self.err(line, format!("`{system}` has no inputs to press"));
                } else if !inputs.contains(&input.to_string()) {
                    let names: Vec<&str> = inputs.iter().map(|s| s.as_str()).collect();
                    self.err(line, format!("`{input}` isn't an input of `{system}`. Its inputs: {}", list_of(&names)));
                }
            }
            Some(_) => self.err(line, format!("`{system}` isn't a system, so nothing can be pressed on it")),
            None => self.unknown(system, line),
        }
    }

    // ---- values ----

    fn expect_value(&mut self, v: &Value, expected: &Type, line: usize, what: &str) -> bool {
        match self.value_type(v, line) {
            Some(t) => self.compare_expected(expected, &t, line, what),
            None => false,
        }
    }

    fn expect_term(&mut self, t: &Term, expected: &Type, line: usize, what: &str) -> bool {
        match self.term_type(t, line) {
            Some(typed) => self.compare_expected(expected, &typed, line, what),
            None => false,
        }
    }

    fn compare_expected(&mut self, expected: &Type, found: &Typed, line: usize, what: &str) -> bool {
        if compatible(expected, found) {
            return true;
        }
        let hint = match (expected, &found.ty) {
            (Type::Decimal, Type::Integer) => " (use `decimal of ...` to convert a whole number)",
            (Type::Integer, Type::Decimal) => " (use `integer of ...` to convert a decimal)",
            _ => "",
        };
        self.err(line, format!("{what} must be a `{expected}`, but this is a `{}`{hint}", found.ty));
        false
    }

    fn term_type(&mut self, t: &Term, line: usize) -> Option<Typed> {
        match t {
            Term::Int(_) => Some(Typed { ty: Type::Integer, int_lit: true }),
            Term::Decimal(_) => Typed::of(Type::Decimal),
            Term::Text(_) => Typed::of(Type::Text),
            Term::Bool(_) => Typed::of(Type::Boolean),
            Term::Duration(_) => Typed::of(Type::Duration),
            Term::Name(n) => {
                if n == "Self" {
                    return Typed::of(Type::Entity);
                }
                match self.lookup(n) {
                    None => {
                        self.unknown(n, line);
                        None
                    }
                    Some(b) => match b.kind {
                        Kind::Variable | Kind::Constant | Kind::Param | Kind::Local | Kind::Loop | Kind::Entity if b.ty == Type::Gate => {
                            self.err(line, format!("`{n}` is a gate; change it with `open`, `close` or `toggle`, and use it with `through {n}:`"));
                            None
                        }
                        Kind::Latch => {
                            self.err(line, format!("`{n}` names an `only once`; the only thing to do with it is `reset {n}`"));
                            None
                        }
                        Kind::Table => {
                            self.err(line, format!("`{n}` is a table; look a row up with `row \"Key\" of {n}`"));
                            None
                        }
                        Kind::EnumValue => Typed::of(b.ty),
                        Kind::Variable | Kind::Constant | Kind::Param | Kind::Local | Kind::Loop | Kind::Entity => Typed::of(b.ty),
                        Kind::Event => {
                            self.err(line, format!("`{n}` is an event, not a value; use `send {n}`"));
                            None
                        }
                        Kind::File => {
                            self.err(line, format!("`{n}` is a file, not a value; call its actions with `do ... on {n}`"));
                            None
                        }
                        Kind::System => {
                            self.err(line, format!("`{n}` is a system, not a value; call its actions with `do ... on {n}`"));
                            None
                        }
                        Kind::Component => {
                            self.err(line, format!("`{n}` is a component; use one of its fields, like `{n}.Loc`"));
                            None
                        }
                    },
                }
            }
            Term::Path { segments, owner } => self.path_type(segments, owner.as_deref(), line),
        }
    }

    /// `Velocity.X`, `Held.Cost`, `Held.Spot.Y`: a part of a vector or color, or a field of a struct, held by a
    /// variable, parameter or remembered value.
    fn value_path_type(&mut self, b: &Binding, segments: &[String], owner: Option<&str>, line: usize) -> Option<Typed> {
        let root = &segments[0];
        if owner.is_some() {
            self.err(line, format!("`of` reaches a component of another entity, but `{root}` is a value of this script"));
            return None;
        }
        if !matches!(b.kind, Kind::Variable | Kind::Constant | Kind::Param | Kind::Local | Kind::Loop) || b.ty == Type::Gate {
            self.err(line, format!("`{root}` has no parts to reach with a dot"));
            return None;
        }
        let mut ty = b.ty.clone();
        let mut so_far = root.clone();
        for (i, part) in segments[1..].iter().enumerate() {
            match &ty {
                Type::Struct(sn) => {
                    let fields = self.structs.get(sn).cloned().unwrap_or_default();
                    match fields.iter().find(|(n, _)| n == part) {
                        Some((_, fty)) => ty = fty.clone(),
                        None => {
                            let names: Vec<&str> = fields.iter().map(|(n, _)| n.as_str()).collect();
                            self.err(line, format!("`{so_far}` is a `{sn}`, which has no field `{part}`. Its fields: {}", list_of(&names)));
                            return None;
                        }
                    }
                }
                Type::Vector2 | Type::Vector3 | Type::Color => {
                    if crate::ty::part_index(&ty, part).is_none() {
                        self.err(line, format!("`{part}` isn't a part of a `{ty}`; its parts are {}", crate::ty::part_names(&ty)));
                        return None;
                    }
                    if i + 2 < segments.len() {
                        self.err(line, "a part of a vector has no parts of its own, so a path like this stops after one part, like `Velocity.X`");
                        return None;
                    }
                    ty = Type::Decimal;
                }
                other => {
                    self.err(line, format!("`{so_far}` is a `{other}`, which has no parts; only vectors, colors and structs do"));
                    return None;
                }
            }
            so_far = format!("{so_far}.{part}");
        }
        Typed::of(ty)
    }

    fn path_type(&mut self, segments: &[String], owner: Option<&str>, line: usize) -> Option<Typed> {
        let comp = &segments[0];
        if !self.manifest.components.contains_key(comp) {
            if let Some(b) = self.lookup(comp) {
                return self.value_path_type(&b, segments, owner, line);
            }
        }
        let Some(fields) = self.manifest.components.get(comp).cloned() else {
            let avail = list_of(&self.manifest.component_names());
            self.err(line, format!("the engine has no component `{comp}`. Available components: {avail}"));
            return None;
        };
        match owner {
            None | Some("Self") => {
                if !matches!(self.globals.get(comp), Some(b) if b.kind == Kind::Component) {
                    self.err(line, format!("add `needs component:{comp}` to the header before using `{comp}`"));
                    return None;
                }
            }
            Some(o) => match self.lookup(o) {
                Some(b) if b.ty == Type::Entity && b.kind != Kind::File && b.kind != Kind::System => {}
                Some(_) => {
                    self.err(line, format!("`{o}` isn't an entity, so `{comp}` can't be read from it"));
                    return None;
                }
                None => {
                    self.unknown(o, line);
                    return None;
                }
            },
        }
        let Some(field) = segments.get(1) else { return None };
        let Some((_, mut ty)) = fields.iter().find(|(n, _)| n == field).cloned() else {
            let names: Vec<&str> = fields.iter().map(|(n, _)| n.as_str()).collect();
            self.err(line, format!("`{comp}` has no field `{field}`. Its fields: {}", list_of(&names)));
            return None;
        };
        if let Some(axis) = segments.get(2) {
            if crate::ty::part_index(&ty, axis).is_none() {
                self.err(line, format!("`{axis}` isn't an axis of `{comp}.{field}` (axes are `X`, `Y`, `Z`, or `Pitch`, `Yaw`, `Roll` for a rotation)"));
                return None;
            }
            ty = Type::Decimal;
        }
        if segments.len() > 3 {
            self.err(line, "a field path has at most three parts, like `Transform.Loc.X`");
            return None;
        }
        Typed::of(ty)
    }

    fn numeric_term(&mut self, t: &Term, line: usize, phrase: &str) -> Option<Typed> {
        let typed = self.term_type(t, line)?;
        if typed.ty.is_numeric() {
            Some(typed)
        } else {
            self.err(line, format!("`{phrase}` needs a number, but this is a `{}`", typed.ty));
            None
        }
    }

    fn value_type(&mut self, v: &Value, line: usize) -> Option<Typed> {
        match v {
            Value::Term(t) => self.term_type(t, line),
            Value::PerSecond(t) => {
                let a = self.term_type(t, line)?;
                match a.ty {
                    Type::Integer | Type::Decimal => Typed::of(Type::Decimal),
                    Type::Vector2 | Type::Vector3 => Typed::of(a.ty),
                    other => {
                        self.err(line, format!("`per second` needs a number or a vector, but this is a `{other}`"));
                        None
                    }
                }
            }
            Value::Distance { from, to } => {
                let a = self.term_type(from, line)?;
                let b = self.term_type(to, line)?;
                let ok = a.ty == b.ty && matches!(a.ty, Type::Vector2 | Type::Vector3 | Type::Entity);
                if !ok {
                    self.err(
                        line,
                        format!("`distance from ... to ...` needs two positions or two entities of the same kind, but this is a `{}` and a `{}`", a.ty, b.ty),
                    );
                    return None;
                }
                Typed::of(Type::Decimal)
            }
            Value::CountOf(t) => {
                let a = self.term_type(t, line)?;
                if !matches!(a.ty, Type::List(_)) {
                    self.err(line, format!("`count of` needs a list, but this is a `{}`", a.ty));
                    return None;
                }
                Typed::of(Type::Integer)
            }
            Value::ItemOf { index, list } => {
                let i = self.term_type(index, line);
                let l = self.term_type(list, line)?;
                if let Some(i) = i {
                    self.compare_expected(&Type::Integer, &i, line, "the position in `item ... of`");
                }
                match l.ty {
                    Type::List(el) => Typed::of(*el),
                    other => {
                        self.err(line, format!("`item ... of` needs a list, but this is a `{other}`"));
                        None
                    }
                }
            }
            Value::JoinedWith { left, right } => {
                let l = self.term_type(left, line);
                let r = self.term_type(right, line);
                if let Some(l) = &l {
                    if l.ty != Type::Text {
                        self.err(line, format!("`joined with` starts from text, but this is a `{}`", l.ty));
                    }
                }
                if let Some(r) = &r {
                    if !r.ty.is_printable() {
                        self.err(line, format!("a `{}` can't be joined onto text", r.ty));
                    }
                }
                Typed::of(Type::Text)
            }
            Value::TextOf(t) => {
                let a = self.term_type(t, line)?;
                if !a.ty.is_printable() {
                    self.err(line, format!("a `{}` can't be turned into text", a.ty));
                    return None;
                }
                Typed::of(Type::Text)
            }
            Value::DecimalOf(t) => {
                let a = self.term_type(t, line)?;
                if a.ty != Type::Integer {
                    self.err(line, format!("`decimal of` converts an integer, but this is a `{}`", a.ty));
                    return None;
                }
                Typed::of(Type::Decimal)
            }
            Value::IntegerOf(t) => {
                let a = self.term_type(t, line)?;
                if a.ty != Type::Decimal {
                    self.err(line, format!("`integer of` converts a decimal, but this is a `{}`", a.ty));
                    return None;
                }
                Typed::of(Type::Integer)
            }
            Value::AbsoluteValueOf(t) => {
                let a = self.numeric_term(t, line, "absolute value of")?;
                Typed::of(a.ty)
            }
            Value::ResultOf { action, args } => match self.call(action, None, args, line)? {
                Some(ty) => Typed::of(ty),
                None => {
                    self.err(line, format!("`{action}` doesn't give anything back, so it has no result"));
                    None
                }
            },
            Value::Vector { x, y, z } => {
                for (axis, t) in [("x", x), ("y", y), ("z", z)] {
                    self.expect_term(t, &Type::Decimal, line, &format!("`{axis}:`"));
                }
                Typed::of(Type::Vector3)
            }
            Value::TimeSinceStart => Typed::of(Type::Decimal),
            Value::Axis { system, input } => {
                match self.lookup(system) {
                    Some(b) if b.kind == Kind::System => {
                        let axes = self.manifest.systems.get(system).map(|s| s.axes.clone()).unwrap_or_default();
                        if !axes.contains(input) {
                            let names: Vec<&str> = axes.iter().map(|s| s.as_str()).collect();
                            self.err(line, format!("`{input}` isn't an analog input of `{system}`. Its axes: {}", list_of(&names)));
                        }
                    }
                    Some(_) => self.err(line, format!("`{system}` isn't a system, so it has no axes")),
                    None => self.unknown(system, line),
                }
                Typed::of(Type::Decimal)
            }
            Value::Lerp { t, from, to } => {
                self.expect_term(t, &Type::Decimal, line, "how far along (0 to 1)");
                let a = self.term_type(from, line)?;
                let b = self.term_type(to, line)?;
                if a.ty == b.ty && matches!(a.ty, Type::Decimal | Type::Vector2 | Type::Vector3) {
                    Typed::of(a.ty)
                } else if (a.ty == Type::Decimal && compatible(&Type::Decimal, &b)) || (b.ty == Type::Decimal && compatible(&Type::Decimal, &a)) {
                    Typed::of(Type::Decimal)
                } else {
                    self.err(line, format!("`of the way from ... to ...` blends two decimals or two vectors, but this is a `{}` and a `{}`", a.ty, b.ty));
                    None
                }
            }
            Value::Arith { left, op, right } => {
                let l = self.term_type(left, line)?;
                let r = self.term_type(right, line)?;
                let both_int = l.ty == Type::Integer && r.ty == Type::Integer;
                let dec_mix = (l.ty == Type::Decimal && compatible(&Type::Decimal, &r))
                    || (r.ty == Type::Decimal && compatible(&Type::Decimal, &l));
                if both_int {
                    Typed::of(Type::Integer)
                } else if dec_mix {
                    Typed::of(Type::Decimal)
                } else if matches!(l.ty, Type::Vector2 | Type::Vector3) && r.ty == l.ty && matches!(op, ArithOp::Plus | ArithOp::Minus) {
                    Typed::of(l.ty)
                } else if matches!(l.ty, Type::Vector2 | Type::Vector3)
                    && compatible(&Type::Decimal, &r)
                    && matches!(op, ArithOp::Times | ArithOp::DividedBy)
                {
                    Typed::of(l.ty)
                } else {
                    let hint = if l.ty.is_numeric() && r.ty.is_numeric() {
                        " (convert one side with `decimal of ...` or `integer of ...`)"
                    } else {
                        ""
                    };
                    self.err(line, format!("can't do arithmetic on a `{}` and a `{}`{hint}", l.ty, r.ty));
                    None
                }
            }
            Value::Condition(c) => {
                self.condition(c, line);
                Typed::of(Type::Boolean)
            }
            Value::Phrase { node, args } => {
                let def = crate::phrases::find(node)?;
                let mut typed = Vec::new();
                for a in args {
                    typed.push(self.term_type(a, line)?);
                }
                match (def.typing)(&typed) {
                    Ok(ty) => Typed::of(ty),
                    Err(message) => {
                        self.err(line, message);
                        None
                    }
                }
            }
            Value::Pick { yes, cond, no } => {
                self.condition(cond, line);
                let a = self.term_type(yes, line)?;
                let b = self.term_type(no, line)?;
                if a.ty == b.ty {
                    Typed::of(a.ty)
                } else if (a.ty == Type::Decimal && compatible(&Type::Decimal, &b)) || (b.ty == Type::Decimal && compatible(&Type::Decimal, &a)) {
                    Typed::of(Type::Decimal)
                } else {
                    self.err(line, format!("the two values to pick from must be the same kind, but this is a `{}` and a `{}`", a.ty, b.ty));
                    None
                }
            }
            Value::CastTo { value, file } => {
                self.expect_term(value, &Type::Entity, line, "what you cast");
                self.file_alias(file, line);
                Typed::of(Type::Entity)
            }
            Value::NewStruct { ty, args } => {
                let fields = self.structs.get(ty).cloned().unwrap_or_default();
                let mut given: Vec<&str> = Vec::new();
                for a in args {
                    match fields.iter().find(|(n, _)| *n == a.name) {
                        Some((_, fty)) => {
                            if given.contains(&a.name.as_str()) {
                                self.err(line, format!("`{}:` is given twice", a.name));
                            }
                            given.push(&a.name);
                            self.expect_value(&a.value, fty, line, &format!("`{}:`", a.name));
                        }
                        None => {
                            let names: Vec<&str> = fields.iter().map(|(n, _)| n.as_str()).collect();
                            self.err(line, format!("`{ty}` has no field `{}`. Its fields: {}", a.name, list_of(&names)));
                        }
                    }
                }
                Typed::of(Type::Struct(ty.clone()))
            }
            Value::RowOf { key, table } => {
                self.expect_term(key, &Type::Text, line, "the key of the row");
                match self.tables.get(table).cloned() {
                    Some(ty) => Typed::of(Type::Struct(ty)),
                    None => {
                        self.err(line, format!("`{table}` isn't a table; declare one in `header:` with `table name:{table} type:<Struct>`"));
                        None
                    }
                }
            }
            Value::Format { parts } => {
                for p in parts {
                    if let FormatPart::Value(t) = p {
                        if let Some(a) = self.term_type(t, line) {
                            if !a.ty.is_printable() {
                                self.err(line, format!("a `{}` can't be put into `format` text", a.ty));
                            }
                        }
                    }
                }
                Typed::of(Type::Text)
            }
        }
    }
}

/// Can a value of type `found` be used where `expected` is wanted?
/// The same type always works; a whole-number literal also works as a decimal.
fn compatible(expected: &Type, found: &Typed) -> bool {
    *expected == found.ty || (*expected == Type::Decimal && found.ty == Type::Integer && found.int_lit)
}

/// Does this type hold a struct somewhere inside it?
fn holds_struct(t: &Type) -> bool {
    match t {
        Type::Struct(_) => true,
        Type::List(inner) => holds_struct(inner),
        _ => false,
    }
}
