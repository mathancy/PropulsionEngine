//! Running statements and evaluating values for one handler or action call.

use super::host::Host;
use super::value::Val;
use super::world::{FileInfo, Pending, Timeline, World};
use crate::phrases::{rotate_by_euler, same_value, PhraseEnv};
use crate::ast::*;
use crate::ty::Type;
use std::collections::HashMap;
use std::rc::Rc;

/// Loops and calls are capped so a script mistake can't freeze the game (SPEC 4.6).
const MAX_LOOP: usize = 100_000;
const MAX_CALL_DEPTH: usize = 64;

pub(crate) enum Flow {
    Next,
    Finish,
    StopLoop,
    Skip,
    Return(Val),
}

pub(crate) struct Run<'a, H: Host> {
    pub w: &'a mut World<H>,
    pub key: String,
    pub info: Rc<FileInfo>,
    /// The entity this code is running as (`Self`).
    pub me: usize,
    pub scopes: Vec<HashMap<String, Val>>,
    pub depth: usize,
    /// The line being run, for warnings.
    pub line: usize,
}

/// Which number of a vector or color a part name picks out (`X` and `Pitch` the first, `Y` and `Yaw` the second...).
fn axis(a: &str) -> usize {
    match a {
        "X" | "Pitch" | "R" => 0,
        "Y" | "Yaw" | "G" => 1,
        "Z" | "Roll" | "B" => 2,
        _ => 3,
    }
}

/// The components an entity has; any other first word of a path is a value (`Velocity.X`).
fn is_component(name: &str) -> bool {
    matches!(name, "Transform" | "Visibility" | "Collision")
}

/// The part of a value a path picks out: a field of a struct, or a number of a vector or color.
fn path_into(v: Val, path: &[String]) -> Val {
    let Some((first, rest)) = path.split_first() else { return v };
    let next = match v {
        Val::Struct(_, fields) => fields.into_iter().find(|(n, _)| n == first).map(|(_, v)| v).unwrap_or(Val::Int(0)),
        Val::Vec3(p) => p.get(axis(first)).map(|x| Val::Dec(*x)).unwrap_or(Val::Dec(0.0)),
        Val::Vec2(p) => p.get(axis(first)).map(|x| Val::Dec(*x)).unwrap_or(Val::Dec(0.0)),
        Val::Color(p) => p.get(axis(first)).map(|x| Val::Dec(*x)).unwrap_or(Val::Dec(0.0)),
        other => other,
    };
    path_into(next, rest)
}

/// The same value with the part a path picks out replaced.
fn set_into(v: Val, path: &[String], new: Val) -> Val {
    let Some((first, rest)) = path.split_first() else { return new };
    match v {
        Val::Struct(name, mut fields) => {
            if let Some(slot) = fields.iter_mut().find(|(n, _)| n == first) {
                let changed = set_into(std::mem::replace(&mut slot.1, Val::Int(0)), rest, new);
                slot.1 = changed.coerce_like(&slot.1);
            }
            Val::Struct(name, fields)
        }
        Val::Vec3(mut p) => {
            if let (Some(n), Some(slot)) = (new.num(), p.get_mut(axis(first))) {
                *slot = n as f32;
            }
            Val::Vec3(p)
        }
        Val::Vec2(mut p) => {
            if let (Some(n), Some(slot)) = (new.num(), p.get_mut(axis(first))) {
                *slot = n as f32;
            }
            Val::Vec2(p)
        }
        Val::Color(mut p) => {
            if let (Some(n), Some(slot)) = (new.num(), p.get_mut(axis(first))) {
                *slot = n as f32;
            }
            Val::Color(p)
        }
        other => other,
    }
}

fn ordered(a: f64, b: f64, op: CmpOp) -> bool {
    match op {
        CmpOp::Eq => a == b,
        CmpOp::NotEq => a != b,
        CmpOp::Above => a > b,
        CmpOp::Below => a < b,
        CmpOp::AtLeast => a >= b,
        CmpOp::AtMost => a <= b,
    }
}

impl<'a, H: Host> Run<'a, H> {
    fn warn(&mut self, msg: impl Into<String>) {
        let (key, line) = (self.key.clone(), self.line);
        self.w.warn(&key, line, msg.into());
    }

    // ---- statements ----

    pub(crate) fn exec_block(&mut self, body: &[Stmt]) -> Flow {
        self.scopes.push(HashMap::new());
        let mut flow = Flow::Next;
        for s in body {
            flow = self.exec_stmt(s);
            if !matches!(flow, Flow::Next) {
                break;
            }
        }
        self.scopes.pop();
        flow
    }

    fn exec_stmt(&mut self, s: &Stmt) -> Flow {
        self.line = s.line;
        match &s.kind {
            StmtKind::Make { target, value, limit } => {
                let v = self.value(value);
                let v = self.limited(v, limit);
                self.assign(target, v);
            }
            StmtKind::Add { value, target, limit } => {
                let v = self.value(value);
                let current = self.term(target);
                let new = match current {
                    Val::List(ty, mut items) => {
                        items.push(v);
                        Val::List(ty, items)
                    }
                    other => self.arith(other, ArithOp::Plus, v),
                };
                let new = self.limited(new, limit);
                self.assign(target, new);
            }
            StmtKind::Subtract { value, target, limit } => {
                let v = self.value(value);
                let current = self.term(target);
                let new = self.arith(current, ArithOp::Minus, v);
                let new = self.limited(new, limit);
                self.assign(target, new);
            }
            StmtKind::Multiply { target, value } => {
                let v = self.value(value);
                let current = self.term(target);
                let new = self.arith(current, ArithOp::Times, v);
                self.assign(target, new);
            }
            StmtKind::Divide { target, value } => {
                let v = self.value(value);
                let current = self.term(target);
                let new = self.arith(current, ArithOp::DividedBy, v);
                self.assign(target, new);
            }
            StmtKind::Remove { value, list } => {
                let v = self.value(value);
                if let Val::List(ty, mut items) = self.term(list) {
                    if let Some(pos) = items.iter().position(|x| *x == v) {
                        items.remove(pos);
                    }
                    self.assign(list, Val::List(ty, items));
                }
            }
            StmtKind::RemoveAt { index, list } => {
                let at = self.term(index).num().unwrap_or(-1.0);
                if let Val::List(ty, mut items) = self.term(list) {
                    if at >= 0.0 && (at as usize) < items.len() {
                        items.remove(at as usize);
                    } else {
                        self.warn(format!("item {at} is past the end of a list of {}; nothing was removed", items.len()));
                    }
                    self.assign(list, Val::List(ty, items));
                }
            }
            StmtKind::AddUnique { value, list } => {
                let v = self.value(value);
                if let Val::List(ty, mut items) = self.term(list) {
                    let v = v.coerce(&ty);
                    if !items.iter().any(|x| same_value(x, &v)) {
                        items.push(v);
                    }
                    self.assign(list, Val::List(ty, items));
                }
            }
            StmtKind::Clear(list) => {
                if let Val::List(ty, _) = self.term(list) {
                    self.assign(list, Val::List(ty, Vec::new()));
                }
            }
            StmtKind::OnlyOnce { body, .. } => {
                if self.latch(s.line) == 0 {
                    self.set_latch(s.line, 1);
                    return self.exec_block(body);
                }
            }
            StmtKind::FirstTimes { count, body, .. } => {
                let limit = self.term(count).num().unwrap_or(0.0) as i64;
                let so_far = self.latch(s.line);
                if so_far < limit {
                    self.set_latch(s.line, so_far + 1);
                    return self.exec_block(body);
                }
            }
            StmtKind::Alternate { first, second, .. } => {
                if self.latch(s.line) == 0 {
                    self.set_latch(s.line, 1);
                    return self.exec_block(first);
                }
                self.set_latch(s.line, 0);
                return self.exec_block(second);
            }
            StmtKind::GateChange { gate, change } => {
                let open = match change {
                    GateChange::Open => true,
                    GateChange::Close => false,
                    GateChange::Toggle => !matches!(self.read_name(gate), Val::Bool(true)),
                };
                self.assign_name(gate, Val::Bool(open));
            }
            StmtKind::Through { gate, body } => {
                if matches!(self.read_name(gate), Val::Bool(true)) {
                    return self.exec_block(body);
                }
            }
            StmtKind::Choose { value, cases, otherwise } => {
                let chosen = self.term(value);
                for case in cases {
                    if case.values.iter().any(|t| same_value(&self.term(t), &chosen)) {
                        return self.exec_block(&case.body);
                    }
                }
                if let Some(body) = otherwise {
                    return self.exec_block(body);
                }
            }
            StmtKind::Animate { var, from, to, seconds, body } => {
                let duration = match self.term(seconds) {
                    Val::Duration(d) => d,
                    other => other.num().unwrap_or(0.0),
                }
                .max(0.0);
                let start = self.term(from);
                let end = self.term(to);
                let mut scope = HashMap::new();
                for s in &self.scopes {
                    for (k, v) in s {
                        scope.insert(k.clone(), v.clone());
                    }
                }
                let seq = self.w.next_seq;
                self.w.next_seq += 1;
                let now = self.w.time;
                self.w.timelines.push(Timeline {
                    seq,
                    entity: self.me,
                    key: self.key.clone(),
                    body: Rc::new(body.clone()),
                    scope,
                    var: var.clone(),
                    from: start,
                    to: end,
                    start: now,
                    duration,
                });
            }
            StmtKind::Damage { target, amount } => {
                let amount = self.value(amount).num().unwrap_or(0.0) as f32;
                match self.term(target) {
                    Val::Entity(Some(id)) if self.w.is_alive(id) => {
                        let me = self.me;
                        self.w.deal_damage(id, me, amount);
                    }
                    _ => self.warn("tried to damage an entity that is missing; skipped"),
                }
            }
            StmtKind::Attach { child, parent } => match (self.term(child), self.term(parent)) {
                (Val::Entity(Some(c)), Val::Entity(Some(p))) if self.w.is_alive(c) && self.w.is_alive(p) => {
                    if let Err(why) = self.w.attach(c, p) {
                        self.warn(format!("`attach` was skipped: {why}"));
                    }
                }
                _ => self.warn("tried to attach an entity that is missing; skipped"),
            },
            StmtKind::Detach(child) => match self.term(child) {
                Val::Entity(Some(c)) if self.w.is_alive(c) => self.w.entities[c].parent = None,
                _ => self.warn("tried to detach an entity that is missing; skipped"),
            },
            StmtKind::Tell { target, action, args } => {
                let argv = self.args(args);
                if let Val::Entity(Some(id)) = self.term(target) {
                    if self.w.is_alive(id) {
                        let key = self.w.entities[id].file.clone();
                        if self.w.has_action(&key, action) {
                            self.call_action(id, &key, action, argv);
                        }
                    }
                }
            }
            StmtKind::Do { action, on, args } => {
                let argv = self.args(args);
                self.invoke(action, on.as_deref(), argv);
            }
            StmtKind::Send(event) => self.w.queue.push_back((self.me, event.clone())),
            StmtKind::Remember { name, value } => {
                let v = self.value(value);
                if let Some(scope) = self.scopes.last_mut() {
                    scope.insert(name.clone(), v);
                }
            }
            StmtKind::Reset(name) => {
                if let Some(line) = self.info.latches.get(name).copied() {
                    self.w.entities[self.me].flow.remove(&line);
                } else {
                    let now = self.w.time;
                    self.assign_name(name, Val::Timer(now));
                }
            }
            StmtKind::Spawn { file, at, name } => {
                let at = self.value(at);
                let loc = match at {
                    Val::Vec3(v) => v,
                    _ => [0.0; 3],
                };
                if let Some(key) = self.info.aliases.get(file).cloned() {
                    let id = self.w.spawn_inner(&key, loc, &[]);
                    if let Some(scope) = self.scopes.last_mut() {
                        scope.insert(name.clone(), Val::Entity(Some(id)));
                    }
                }
            }
            StmtKind::Move { target, toward, by } => {
                let current = self.term(target);
                let dest = self.value(toward);
                let step = self.value(by).num().unwrap_or(0.0) as f32;
                if let (Val::Vec3(c), Val::Vec3(d)) = (current, dest) {
                    let delta = [d[0] - c[0], d[1] - c[1], d[2] - c[2]];
                    let dist = (delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]).sqrt();
                    let new = if dist <= step || dist == 0.0 {
                        d
                    } else {
                        let k = step / dist;
                        [c[0] + delta[0] * k, c[1] + delta[1] * k, c[2] + delta[2] * k]
                    };
                    self.assign(target, Val::Vec3(new));
                }
            }
            StmtKind::MoveDir { target, dir, by } => {
                let amount = self.value(by).num().unwrap_or(0.0) as f32;
                match self.term(target) {
                    Val::Entity(Some(id)) if self.w.is_alive(id) => {
                        // The engine's convention: forward is -Z, right is +X, up is +Y.
                        let local = match dir {
                            Direction::Forward => [0.0, 0.0, -1.0],
                            Direction::Back => [0.0, 0.0, 1.0],
                            Direction::Right => [1.0, 0.0, 0.0],
                            Direction::Left => [-1.0, 0.0, 0.0],
                            Direction::Up => [0.0, 1.0, 0.0],
                            Direction::Down => [0.0, -1.0, 0.0],
                        };
                        let world = rotate_by_euler(self.w.entities[id].rot, local);
                        let e = &mut self.w.entities[id];
                        for i in 0..3 {
                            e.loc[i] += world[i] * amount;
                        }
                    }
                    _ => self.warn("tried to move an entity that is missing; skipped"),
                }
            }
            StmtKind::Print(v) => {
                let text = self.value(v).to_string();
                self.w.host.print(&text);
            }
            StmtKind::After { seconds, body } => {
                let delay = match self.term(seconds) {
                    Val::Duration(d) => d,
                    other => other.num().unwrap_or(0.0),
                }
                .max(0.0);
                let mut scope = HashMap::new();
                for s in &self.scopes {
                    for (k, v) in s {
                        scope.insert(k.clone(), v.clone());
                    }
                }
                let seq = self.w.next_seq;
                self.w.next_seq += 1;
                let due = self.w.time + delay;
                self.w.pending.push(Pending { due, seq, entity: self.me, key: self.key.clone(), body: body.clone(), scope });
            }
            StmtKind::ForRange { var, from, to, body } => {
                let start = self.term(from).num().unwrap_or(0.0) as i64;
                let end = self.term(to).num().unwrap_or(-1.0) as i64;
                let mut i = start;
                let mut turns = 0;
                while i <= end {
                    turns += 1;
                    if turns > MAX_LOOP {
                        self.warn("a `for` loop ran too long and was stopped");
                        break;
                    }
                    self.scopes.push(HashMap::from([(var.clone(), Val::Int(i as i32))]));
                    let flow = self.exec_block(body);
                    self.scopes.pop();
                    match flow {
                        Flow::StopLoop => break,
                        Flow::Finish => return Flow::Finish,
                        Flow::Return(v) => return Flow::Return(v),
                        Flow::Next | Flow::Skip => {}
                    }
                    i += 1;
                }
            }
            StmtKind::Destroy(t) => match self.term(t) {
                Val::Entity(Some(id)) if self.w.is_alive(id) => self.w.destroy(id),
                _ => self.warn("tried to destroy an entity that is missing; skipped"),
            },
            StmtKind::GiveBack(v) => return Flow::Return(self.value(v)),
            StmtKind::Finish => return Flow::Finish,
            StmtKind::StopLoop => return Flow::StopLoop,
            StmtKind::Skip => return Flow::Skip,
            StmtKind::If { branches, otherwise, .. } => {
                for (cond, body) in branches {
                    if self.cond(cond) {
                        return self.exec_block(body);
                    }
                }
                if let Some(body) = otherwise {
                    return self.exec_block(body);
                }
            }
            StmtKind::Repeat { times, body } => {
                let n = self.term(times).num().unwrap_or(0.0).max(0.0) as usize;
                for _ in 0..n.min(MAX_LOOP) {
                    match self.exec_block(body) {
                        Flow::StopLoop => break,
                        Flow::Finish => return Flow::Finish,
                        Flow::Return(v) => return Flow::Return(v),
                        Flow::Next | Flow::Skip => {}
                    }
                }
            }
            StmtKind::ForEach { var, list, body } => {
                let items = match self.term(list) {
                    Val::List(_, items) => items,
                    _ => Vec::new(),
                };
                for item in items {
                    self.scopes.push(HashMap::from([(var.clone(), item)]));
                    let flow = self.exec_block(body);
                    self.scopes.pop();
                    match flow {
                        Flow::StopLoop => break,
                        Flow::Finish => return Flow::Finish,
                        Flow::Return(v) => return Flow::Return(v),
                        Flow::Next | Flow::Skip => {}
                    }
                }
            }
            StmtKind::While { cond, body } => {
                let mut turns = 0;
                while self.cond(cond) {
                    turns += 1;
                    if turns > MAX_LOOP {
                        self.warn("a `while` loop ran too long and was stopped");
                        break;
                    }
                    match self.exec_block(body) {
                        Flow::StopLoop => break,
                        Flow::Finish => return Flow::Finish,
                        Flow::Return(v) => return Flow::Return(v),
                        Flow::Next | Flow::Skip => {}
                    }
                }
            }
        }
        Flow::Next
    }

    /// Apply `, capped at` / `, floored at`.
    fn limited(&mut self, v: Val, limit: &Limit) -> Val {
        let mut v = v;
        if let Some(t) = &limit.capped {
            let cap = self.term(t);
            if let (Some(a), Some(b)) = (v.num(), cap.num()) {
                if a > b {
                    v = cap.coerce_like(&v);
                }
            }
        }
        if let Some(t) = &limit.floored {
            let floor = self.term(t);
            if let (Some(a), Some(b)) = (v.num(), floor.num()) {
                if a < b {
                    v = floor.coerce_like(&v);
                }
            }
        }
        v
    }

    // ---- state of `only once` and friends ----

    fn latch(&self, line: usize) -> i64 {
        self.w.entities[self.me].flow.get(&line).copied().unwrap_or(0)
    }

    fn set_latch(&mut self, line: usize, to: i64) {
        self.w.entities[self.me].flow.insert(line, to);
    }

    // ---- assignment ----

    fn assign(&mut self, target: &Term, v: Val) {
        match target {
            Term::Name(n) => self.assign_name(n, v),
            Term::Path { segments, owner } => self.write_path(segments, owner.as_deref(), v),
            _ => {}
        }
    }

    fn assign_name(&mut self, name: &str, v: Val) {
        for scope in self.scopes.iter_mut().rev() {
            if let Some(slot) = scope.get_mut(name) {
                let v = v.coerce_like(&*slot);
                *slot = v;
                return;
            }
        }
        let vars = &mut self.w.entities[self.me].vars;
        match vars.get_mut(name) {
            Some(slot) => {
                let v = v.coerce_like(&*slot);
                *slot = v;
            }
            None => {
                vars.insert(name.to_string(), v);
            }
        }
    }

    fn write_path(&mut self, seg: &[String], owner: Option<&str>, v: Val) {
        if !is_component(&seg[0]) {
            // A part of a vector or color, or a field of a struct: read the whole, change that part, put it back.
            let whole = self.read_name(&seg[0]);
            let new = set_into(whole, &seg[1..], v);
            self.assign_name(&seg[0], new);
            return;
        }
        let Some(id) = self.owner_entity(owner) else { return };
        let e = &mut self.w.entities[id];
        match (seg[0].as_str(), seg.get(1).map(String::as_str)) {
            ("Visibility", Some("Shown")) => {
                if let Val::Bool(b) = v {
                    e.shown = b;
                }
                return;
            }
            ("Collision", Some("Enabled")) => {
                if let Val::Bool(b) = v {
                    e.collides = b;
                }
                return;
            }
            _ => {}
        }
        let field = match seg.get(1).map(String::as_str) {
            Some("Loc") => &mut e.loc,
            Some("Rot") => &mut e.rot,
            Some("Scale") => &mut e.scale,
            _ => return,
        };
        match (seg.get(2), v) {
            (Some(a), v) => {
                if let Some(n) = v.num() {
                    field[axis(a)] = n as f32;
                }
            }
            (None, Val::Vec3(new)) => *field = new,
            _ => {}
        }
    }

    fn owner_entity(&mut self, owner: Option<&str>) -> Option<usize> {
        match owner {
            None | Some("Self") => Some(self.me),
            Some(o) => match self.read_name(o) {
                Val::Entity(Some(id)) if self.w.is_alive(id) => Some(id),
                _ => {
                    self.warn(format!("`{o}` is missing, so its fields were skipped"));
                    None
                }
            },
        }
    }

    // ---- calls ----

    fn args(&mut self, args: &[Arg]) -> Vec<(String, Val)> {
        args.iter().map(|a| (a.name.clone(), self.value(&a.value))).collect()
    }

    /// Run an action on its target and return what it gives back.
    fn invoke(&mut self, action: &str, on: Option<&str>, args: Vec<(String, Val)>) -> Option<Val> {
        match on {
            None | Some("Self") => {
                let key = self.key.clone();
                self.call_action(self.me, &key, action, args)
            }
            Some(n) => {
                if self.info.systems.contains(n) {
                    self.w.host.call(n, action, &args);
                    return None;
                }
                if let Some(key) = self.info.aliases.get(n).cloned() {
                    let id = self.w.ensure_instance(&key);
                    return self.call_action(id, &key, action, args);
                }
                match self.read_name(n) {
                    Val::Entity(Some(id)) if self.w.is_alive(id) => {
                        let key = self.w.entities[id].file.clone();
                        self.call_action(id, &key, action, args)
                    }
                    _ => {
                        self.warn(format!("`{action}` was skipped because `{n}` is missing"));
                        None
                    }
                }
            }
        }
    }

    pub(crate) fn call_action(&mut self, id: usize, key: &str, action: &str, args: Vec<(String, Val)>) -> Option<Val> {
        if self.depth >= MAX_CALL_DEPTH {
            self.warn(format!("`{action}` was skipped: actions called each other too deeply"));
            return None;
        }
        let project = self.w.project.clone();
        let file = project.files.get(key).and_then(|f| f.parsed.as_ref().ok())?;
        let item = file.implement.iter().find(|i| matches!(&i.kind, ItemKind::Action { name, .. } if name == action));
        let Some(item) = item else {
            self.warn(format!("`{action}` doesn't exist in `{key}`"));
            return None;
        };
        let ItemKind::Action { params, gives, body, .. } = &item.kind else { return None };

        let now = self.w.time;
        let mut scope = HashMap::new();
        for p in params {
            let ty = Type::from_expr(&p.ty);
            let v = args
                .iter()
                .find(|(n, _)| *n == p.name)
                .map(|(_, v)| v.clone())
                .unwrap_or_else(|| self.w.default_val(&ty, now));
            scope.insert(p.name.clone(), v.coerce(&ty));
        }
        let info = self.w.infos[key].clone();
        let depth = self.depth + 1;
        let mut inner = Run { w: &mut *self.w, key: key.to_string(), info, me: id, scopes: vec![scope], depth, line: item.line };
        match inner.exec_block(body) {
            Flow::Return(v) => Some(v),
            _ => gives.as_ref().map(|t| inner.w.default_val(&Type::from_expr(t), now)),
        }
    }

    // ---- conditions ----

    fn cond(&mut self, c: &Condition) -> bool {
        let mut result = self.comparison(&c.first);
        for r in &c.rest {
            result = match c.joiner {
                Some(Joiner::Or) => result || self.comparison(r),
                _ => result && self.comparison(r),
            };
        }
        result
    }

    fn comparison(&mut self, c: &Comparison) -> bool {
        match c {
            Comparison::Compare { left, op, right } => {
                let l = self.value(left);
                let r = self.value(right);
                self.compare(&l, *op, &r)
            }
            Comparison::Pressing { system, input, kind } => self.w.host.pressing(system, input, *kind),
            Comparison::Presence { value, missing } => {
                let present = matches!(self.value(value), Val::Entity(Some(id)) if self.w.is_alive(id));
                present != *missing
            }
            Comparison::Contains { list, item, negated } => {
                let items = match self.value(list) {
                    Val::List(_, items) => items,
                    _ => Vec::new(),
                };
                let wanted = self.value(item);
                items.iter().any(|x| same_value(x, &wanted)) != *negated
            }
        }
    }

    fn compare(&mut self, l: &Val, op: CmpOp, r: &Val) -> bool {
        if let (Val::Timer(since), Val::Duration(secs)) = (l, r) {
            return ordered(self.w.time - since, *secs, op);
        }
        if let (Some(a), Some(b)) = (l.num(), r.num()) {
            return ordered(a, b, op);
        }
        match op {
            CmpOp::Eq => l == r,
            CmpOp::NotEq => l != r,
            _ => {
                self.warn("compared values that can't be ordered");
                false
            }
        }
    }

    // ---- values ----

    fn read_name(&mut self, n: &str) -> Val {
        if n == "Self" {
            return Val::Entity(Some(self.me));
        }
        for scope in self.scopes.iter().rev() {
            if let Some(v) = scope.get(n) {
                return v.clone();
            }
        }
        if let Some(v) = self.w.entities[self.me].vars.get(n) {
            return v.clone();
        }
        if let Some((tag, single)) = self.info.entity_bindings.get(n).cloned() {
            return if single {
                Val::Entity(self.w.find_by_tag(&tag))
            } else {
                Val::List(Type::Entity, self.w.all_by_tag(&tag).into_iter().map(|id| Val::Entity(Some(id))).collect())
            };
        }
        if let Some(en) = self.w.enum_of_value(n) {
            return Val::Enum(en.clone(), n.to_string());
        }
        self.warn(format!("`{n}` has no value here"));
        Val::Int(0)
    }

    fn read_path(&mut self, seg: &[String], owner: Option<&str>) -> Val {
        if !is_component(&seg[0]) {
            return path_into(self.read_name(&seg[0]), &seg[1..]);
        }
        let zero = match seg[0].as_str() {
            "Visibility" | "Collision" => Val::Bool(false),
            _ if seg.len() >= 3 => Val::Dec(0.0),
            _ => Val::Vec3([0.0; 3]),
        };
        let Some(id) = self.owner_entity(owner) else { return zero };
        let e = &self.w.entities[id];
        match (seg[0].as_str(), seg.get(1).map(String::as_str)) {
            ("Visibility", Some("Shown")) => return Val::Bool(e.shown),
            ("Collision", Some("Enabled")) => return Val::Bool(e.collides),
            _ => {}
        }
        let v = match seg.get(1).map(String::as_str) {
            Some("Loc") => e.loc,
            Some("Rot") => e.rot,
            Some("Scale") => e.scale,
            _ => return zero,
        };
        match seg.get(2) {
            Some(a) => Val::Dec(v[axis(a)]),
            None => Val::Vec3(v),
        }
    }

    pub(crate) fn term(&mut self, t: &Term) -> Val {
        match t {
            Term::Int(i) => Val::Int(*i as i32),
            Term::Decimal(d) => Val::Dec(*d as f32),
            Term::Text(s) => Val::Text(s.clone()),
            Term::Bool(b) => Val::Bool(*b),
            Term::Duration(d) => Val::Duration(*d),
            Term::Name(n) => self.read_name(n),
            Term::Path { segments, owner } => self.read_path(segments, owner.as_deref()),
        }
    }

    /// A position: a vector, or an entity's location.
    fn position(&mut self, t: &Term) -> [f32; 3] {
        match self.term(t) {
            Val::Vec3(v) => v,
            Val::Vec2(v) => [v[0], v[1], 0.0],
            Val::Entity(Some(id)) if self.w.is_alive(id) => self.w.entities[id].loc,
            _ => {
                self.warn("a position was missing, so it counted as (0, 0, 0)");
                [0.0; 3]
            }
        }
    }

    fn value(&mut self, v: &Value) -> Val {
        match v {
            Value::Term(t) => self.term(t),
            Value::PerSecond(t) => {
                let dt = self.w.dt as f32;
                match self.term(t) {
                    Val::Vec3(v) => Val::Vec3([v[0] * dt, v[1] * dt, v[2] * dt]),
                    Val::Vec2(v) => Val::Vec2([v[0] * dt, v[1] * dt]),
                    other => Val::Dec(other.num().unwrap_or(0.0) as f32 * dt),
                }
            }
            Value::Distance { from, to } => {
                let a = self.position(from);
                let b = self.position(to);
                let d = [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
                Val::Dec((d[0] * d[0] + d[1] * d[1] + d[2] * d[2]).sqrt())
            }
            Value::CountOf(t) => match self.term(t) {
                Val::List(_, items) => Val::Int(items.len() as i32),
                _ => Val::Int(0),
            },
            Value::ItemOf { index, list } => {
                let i = self.term(index).num().unwrap_or(0.0);
                match self.term(list) {
                    Val::List(ty, items) => {
                        if i >= 0.0 && (i as usize) < items.len() {
                            items[i as usize].clone()
                        } else {
                            self.warn(format!("item {i} is past the end of a list of {}; used the default", items.len()));
                            self.w.default_val(&ty, self.w.time)
                        }
                    }
                    _ => Val::Int(0),
                }
            }
            Value::JoinedWith { left, right } => {
                let l = self.term(left);
                let r = self.term(right);
                Val::Text(format!("{l}{r}"))
            }
            Value::TextOf(t) => Val::Text(self.term(t).to_string()),
            Value::DecimalOf(t) => Val::Dec(self.term(t).num().unwrap_or(0.0) as f32),
            Value::IntegerOf(t) => Val::Int(self.term(t).num().unwrap_or(0.0) as i32),
            Value::AbsoluteValueOf(t) => match self.term(t) {
                Val::Int(i) => Val::Int(i.wrapping_abs()),
                Val::Dec(d) => Val::Dec(d.abs()),
                other => other,
            },
            Value::ResultOf { action, args } => {
                let argv = self.args(args);
                self.invoke(action, None, argv).unwrap_or(Val::Int(0))
            }
            Value::Vector { x, y, z } => {
                let n = |s: &mut Self, t: &Term| s.term(t).num().unwrap_or(0.0) as f32;
                Val::Vec3([n(self, x), n(self, y), n(self, z)])
            }
            Value::TimeSinceStart => Val::Dec(self.w.time as f32),
            Value::Axis { system, input } => Val::Dec(self.w.host.axis(system, input)),
            Value::Lerp { t, from, to } => {
                let t = self.term(t).num().unwrap_or(0.0) as f32;
                let a = self.term(from);
                let b = self.term(to);
                let mix = |x: f32, y: f32| x + (y - x) * t;
                match (&a, &b) {
                    (Val::Vec3(p), Val::Vec3(q)) => Val::Vec3([mix(p[0], q[0]), mix(p[1], q[1]), mix(p[2], q[2])]),
                    (Val::Vec2(p), Val::Vec2(q)) => Val::Vec2([mix(p[0], q[0]), mix(p[1], q[1])]),
                    _ => match (a.num(), b.num()) {
                        (Some(x), Some(y)) => Val::Dec(mix(x as f32, y as f32)),
                        _ => {
                            self.warn("can't blend these values; used the first one");
                            a
                        }
                    },
                }
            }
            Value::Arith { left, op, right } => {
                let l = self.term(left);
                let r = self.term(right);
                self.arith(l, *op, r)
            }
            Value::Condition(c) => Val::Bool(self.cond(c)),
            Value::Phrase { node, args } => {
                let vals: Vec<Val> = args.iter().map(|t| self.term(t)).collect();
                match crate::phrases::find(node) {
                    Some(def) => (def.eval)(self, &vals),
                    None => Val::Int(0),
                }
            }
            Value::Pick { yes, cond, no } => {
                let (chosen, other) = if self.cond(cond) { (yes, no) } else { (no, yes) };
                match self.term(chosen) {
                    // `1 if ... otherwise Speed`: a whole number beside a decimal is a decimal.
                    Val::Int(i) if matches!(self.term(other), Val::Dec(_)) => Val::Dec(i as f32),
                    v => v,
                }
            }
            Value::CastTo { value, file } => match self.term(value) {
                Val::Entity(Some(id)) if self.w.is_alive(id) && self.info.aliases.get(file) == Some(&self.w.entities[id].file) => Val::Entity(Some(id)),
                _ => Val::Entity(None),
            },
            Value::NewStruct { ty, args } => {
                let given = args.iter().map(|a| (a.name.clone(), self.value(&a.value))).collect();
                self.w.make_struct(ty, given)
            }
            Value::RowOf { key, table } => {
                let wanted = self.term(key).to_string();
                let row = self.info.tables.get(table).and_then(|t| t.rows.iter().find(|(k, _)| *k == wanted).map(|(_, v)| (t.ty.clone(), v.clone())));
                match row {
                    Some((ty, fields)) => {
                        let field_types = self.w.struct_fields(&ty);
                        let given = fields
                            .iter()
                            .map(|(n, t)| {
                                let v = match (t, field_types.iter().find(|(f, _)| f == n)) {
                                    (Term::Name(value), Some((_, Type::Enum(en)))) => Val::Enum(en.clone(), value.clone()),
                                    _ => self.term(t),
                                };
                                (n.clone(), v)
                            })
                            .collect();
                        self.w.make_struct(&ty, given)
                    }
                    None => {
                        self.warn(format!("`{table}` has no row \"{wanted}\"; used the defaults"));
                        match self.info.tables.get(table).map(|t| t.ty.clone()) {
                            Some(ty) => self.w.make_struct(&ty, Vec::new()),
                            None => Val::Int(0),
                        }
                    }
                }
            }
            Value::Format { parts } => {
                let mut out = String::new();
                for p in parts {
                    match p {
                        FormatPart::Text(t) => out.push_str(t),
                        FormatPart::Value(t) => out.push_str(&self.term(t).to_string()),
                    }
                }
                Val::Text(out)
            }
        }
    }

    fn position_of_val(&mut self, v: &Val) -> [f32; 3] {
        match v {
            Val::Vec3(v) => *v,
            Val::Vec2(v) => [v[0], v[1], 0.0],
            Val::Entity(Some(id)) if self.w.is_alive(*id) => self.w.entities[*id].loc,
            _ => {
                self.warn("a position was missing, so it counted as (0, 0, 0)");
                [0.0; 3]
            }
        }
    }

    fn arith(&mut self, a: Val, op: ArithOp, b: Val) -> Val {
        use ArithOp::*;
        match (&a, &b) {
            (Val::Int(x), Val::Int(y)) => match op {
                Plus => Val::Int(x.wrapping_add(*y)),
                Minus => Val::Int(x.wrapping_sub(*y)),
                Times => Val::Int(x.wrapping_mul(*y)),
                DividedBy | Remainder if *y == 0 => {
                    self.warn("divided by zero; the result is 0");
                    Val::Int(0)
                }
                DividedBy => Val::Int(x.wrapping_div(*y)),
                Remainder => Val::Int(x.wrapping_rem(*y)),
            },
            _ if a.num().is_some() && b.num().is_some() => {
                let (x, y) = (a.num().unwrap() as f32, b.num().unwrap() as f32);
                match op {
                    Plus => Val::Dec(x + y),
                    Minus => Val::Dec(x - y),
                    Times => Val::Dec(x * y),
                    DividedBy | Remainder if y == 0.0 => {
                        self.warn("divided by zero; the result is 0");
                        Val::Dec(0.0)
                    }
                    DividedBy => Val::Dec(x / y),
                    Remainder => Val::Dec(x % y),
                }
            }
            (Val::Vec3(p), Val::Vec3(q)) if matches!(op, Plus | Minus) => {
                let s = if matches!(op, Plus) { 1.0 } else { -1.0 };
                Val::Vec3([p[0] + s * q[0], p[1] + s * q[1], p[2] + s * q[2]])
            }
            (Val::Vec2(p), Val::Vec2(q)) if matches!(op, Plus | Minus) => {
                let s = if matches!(op, Plus) { 1.0 } else { -1.0 };
                Val::Vec2([p[0] + s * q[0], p[1] + s * q[1]])
            }
            (Val::Vec2(p), _) if b.num().is_some() && matches!(op, Times | DividedBy) => {
                let k = b.num().unwrap() as f32;
                if matches!(op, DividedBy) && k == 0.0 {
                    self.warn("divided by zero; the result is 0");
                    return Val::Vec2([0.0; 2]);
                }
                let k = if matches!(op, Times) { k } else { 1.0 / k };
                Val::Vec2([p[0] * k, p[1] * k])
            }
            (Val::Vec3(p), _) if b.num().is_some() && matches!(op, Times | DividedBy) => {
                let k = b.num().unwrap() as f32;
                if matches!(op, DividedBy) && k == 0.0 {
                    self.warn("divided by zero; the result is 0");
                    return Val::Vec3([0.0; 3]);
                }
                let k = if matches!(op, Times) { k } else { 1.0 / k };
                Val::Vec3([p[0] * k, p[1] * k, p[2] * k])
            }
            _ => {
                self.warn("can't do arithmetic on these values; left unchanged");
                a
            }
        }
    }
}

impl<'a, H: Host> PhraseEnv for Run<'a, H> {
    fn dt(&self) -> f32 {
        self.w.dt as f32
    }

    fn random(&mut self) -> u64 {
        self.w.next_random()
    }

    fn rotation_of(&mut self, entity: &Val) -> Option<[f32; 3]> {
        match entity {
            Val::Entity(Some(id)) if self.w.is_alive(*id) => Some(self.w.entities[*id].rot),
            _ => {
                self.warn("an entity was missing, so its direction counted as unturned");
                None
            }
        }
    }

    fn position_of(&mut self, value: &Val) -> [f32; 3] {
        self.position_of_val(value)
    }

    fn level_name(&self) -> String {
        self.w.host.level_name()
    }

    fn trace(&mut self, from: [f32; 3], to: [f32; 3], radius: f32) -> Val {
        self.w.trace(self.me, from, to, radius)
    }
}
