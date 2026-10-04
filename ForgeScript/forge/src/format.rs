//! The formatter: prints a parsed file in one canonical style.
//!
//! Formatting never changes what a file means or loses a comment: parsing the output gives the
//! same tree (ignoring line numbers), and formatting twice gives the same text. Blank lines are
//! kept (at most one in a row), and comments move to the line of code they were above.

use crate::ast::*;
use crate::error::ForgeError;
use crate::parser::parse;

/// Parse `src` and print it in the canonical style.
pub fn format_source(src: &str) -> Result<String, ForgeError> {
    format_file(&parse(src)?)
}

/// Print a parsed file in the canonical style.
pub fn format_file(file: &File) -> Result<String, ForgeError> {
    if let Some(&line) = file.unattached.first() {
        return Err(ForgeError::new(
            line,
            1,
            "a comment above this line is somewhere the formatter can't keep it (above `otherwise:`, or above an \
             `entry` line). Move it onto the line it describes, then format again",
        ));
    }
    let mut p = Printer { out: String::new() };

    p.emit(0, "header:", &file.header_notes, true);
    for (i, d) in file.header.iter().enumerate() {
        p.emit(1, &decl_text(&d.kind), &d.notes, i == 0);
        match &d.kind {
            DeclKind::Variable { entries, .. } => {
                for e in entries {
                    p.emit(2, &format!("entry{}", parts_text(e)), &Notes::default(), true);
                }
            }
            DeclKind::Struct { fields, .. } => {
                for (name, ty) in fields {
                    p.emit(2, &format!("field name:{name} type:{}", type_text(ty)), &Notes::default(), true);
                }
            }
            DeclKind::Table { rows, .. } => {
                for r in rows {
                    let values: String = r.values.iter().map(|(k, t)| format!(" {k}:{}", term_text(t))).collect();
                    p.emit(2, &format!("row key:\"{}\"{values}", r.key), &Notes::default(), true);
                }
            }
            _ => {}
        }
    }

    p.out.push('\n');
    p.emit(0, "implement:", &file.implement_notes, true);
    if file.implement.is_empty() {
        p.emit(1, "do nothing", &Notes::default(), true);
    }
    for (i, item) in file.implement.iter().enumerate() {
        p.item(item, i == 0);
    }

    if !file.footer.is_empty() {
        p.out.push('\n');
        for c in &file.footer {
            p.out.push_str(&comment(c));
            p.out.push('\n');
        }
    }
    Ok(p.out)
}

struct Printer {
    out: String,
}

fn comment(text: &str) -> String {
    if text.is_empty() {
        "//".to_string()
    } else {
        format!("// {text}")
    }
}

impl Printer {
    /// One line of code with the comments and blank line that belong to it.
    fn emit(&mut self, depth: usize, text: &str, notes: &Notes, first_in_block: bool) {
        let indent = "\t".repeat(depth);
        if notes.blank_before && !first_in_block {
            self.out.push('\n');
        }
        for (i, c) in notes.leading.iter().enumerate() {
            if i > 0 && notes.gaps.contains(&i) {
                self.out.push('\n');
            }
            self.out.push_str(&indent);
            self.out.push_str(&comment(c));
            self.out.push('\n');
        }
        if !notes.leading.is_empty() && notes.gaps.contains(&notes.leading.len()) {
            self.out.push('\n');
        }
        self.out.push_str(&indent);
        self.out.push_str(text);
        if let Some(t) = &notes.trailing {
            self.out.push(' ');
            self.out.push_str(&comment(t));
        }
        self.out.push('\n');
    }

    fn item(&mut self, item: &Item, first: bool) {
        let (head, body) = match &item.kind {
            ItemKind::Handler { trigger, body } => (format!("{}:", trigger_text(trigger)), body),
            ItemKind::Action { name, params, gives, body } => {
                let mut s = format!("action {name}");
                for p in params {
                    s.push_str(&format!(" {}:{}", p.name, type_text(&p.ty)));
                }
                if let Some(g) = gives {
                    s.push_str(&format!(" gives {}", type_text(g)));
                }
                s.push(':');
                (s, body)
            }
        };
        self.emit(1, &head, &item.notes, first);
        self.block(2, body);
    }

    fn block(&mut self, depth: usize, body: &[Stmt]) {
        if body.is_empty() {
            self.emit(depth, "do nothing", &Notes::default(), true);
        }
        for (i, s) in body.iter().enumerate() {
            self.stmt(depth, s, i == 0);
        }
    }

    fn stmt(&mut self, depth: usize, s: &Stmt, first: bool) {
        match &s.kind {
            StmtKind::If { branches, otherwise, inline } => {
                if *inline && branches.len() == 1 && branches[0].1.len() == 1 && otherwise.is_none() {
                    let text = format!("if {}, then {}", condition_text(&branches[0].0), simple_text(&branches[0].1[0].kind));
                    self.emit(depth, &text, &s.notes, first);
                    return;
                }
                for (i, (cond, body)) in branches.iter().enumerate() {
                    if i == 0 {
                        self.emit(depth, &format!("if {}:", condition_text(cond)), &s.notes, first);
                    } else {
                        self.emit(depth, &format!("otherwise if {}:", condition_text(cond)), &Notes::default(), true);
                    }
                    self.block(depth + 1, body);
                }
                if let Some(body) = otherwise {
                    self.emit(depth, "otherwise:", &Notes::default(), true);
                    self.block(depth + 1, body);
                }
            }
            StmtKind::Repeat { times, body } => {
                self.emit(depth, &format!("repeat {} times:", term_text(times)), &s.notes, first);
                self.block(depth + 1, body);
            }
            StmtKind::ForEach { var, list, body } => {
                self.emit(depth, &format!("for each {var} in {}:", term_text(list)), &s.notes, first);
                self.block(depth + 1, body);
            }
            StmtKind::ForRange { var, from, to, body } => {
                let text = format!("for {var} from {} to {}:", term_text(from), term_text(to));
                self.emit(depth, &text, &s.notes, first);
                self.block(depth + 1, body);
            }
            StmtKind::While { cond, body } => {
                self.emit(depth, &format!("while {}:", condition_text(cond)), &s.notes, first);
                self.block(depth + 1, body);
            }
            StmtKind::After { seconds, body } => {
                let text = match seconds {
                    Term::Duration(d) => format!("after {}:", duration_text(*d)),
                    other => format!("after {} seconds:", term_text(other)),
                };
                self.emit(depth, &text, &s.notes, first);
                self.block(depth + 1, body);
            }
            StmtKind::OnlyOnce { body, .. }
            | StmtKind::FirstTimes { body, .. }
            | StmtKind::Through { body, .. }
            | StmtKind::Animate { body, .. } => {
                self.emit(depth, &stmt_head(s), &s.notes, first);
                self.block(depth + 1, body);
            }
            StmtKind::Alternate { first: a, second: b, .. } => {
                self.emit(depth, &stmt_head(s), &s.notes, first);
                self.block(depth + 1, a);
                self.emit(depth, "otherwise:", &Notes::default(), true);
                self.block(depth + 1, b);
            }
            StmtKind::Choose { cases, otherwise, .. } => {
                self.emit(depth, &stmt_head(s), &s.notes, first);
                for case in cases {
                    self.emit(depth + 1, &case_text(case), &Notes::default(), true);
                    self.block(depth + 2, &case.body);
                }
                if let Some(body) = otherwise {
                    self.emit(depth + 1, "otherwise:", &Notes::default(), true);
                    self.block(depth + 2, body);
                }
            }
            simple => self.emit(depth, &simple_text(simple), &s.notes, first),
        }
    }
}

/// The first line of a handler or action, without its colon's body.
pub(crate) fn item_head(item: &Item) -> String {
    match &item.kind {
        ItemKind::Handler { trigger, .. } => format!("{}:", trigger_text(trigger)),
        ItemKind::Action { name, params, gives, .. } => {
            let mut s = format!("action {name}");
            for p in params {
                s.push_str(&format!(" {}:{}", p.name, type_text(&p.ty)));
            }
            if let Some(g) = gives {
                s.push_str(&format!(" gives {}", type_text(g)));
            }
            s.push(':');
            s
        }
    }
}

/// The first line of a statement: the whole sentence, or the line that opens a block.
pub(crate) fn stmt_head(s: &Stmt) -> String {
    match &s.kind {
        StmtKind::If { branches, otherwise, inline } => {
            if *inline && branches.len() == 1 && branches[0].1.len() == 1 && otherwise.is_none() {
                format!("if {}, then {}", condition_text(&branches[0].0), simple_text(&branches[0].1[0].kind))
            } else {
                format!("if {}:", condition_text(&branches[0].0))
            }
        }
        StmtKind::Repeat { times, .. } => format!("repeat {} times:", term_text(times)),
        StmtKind::ForEach { var, list, .. } => format!("for each {var} in {}:", term_text(list)),
        StmtKind::ForRange { var, from, to, .. } => format!("for {var} from {} to {}:", term_text(from), term_text(to)),
        StmtKind::While { cond, .. } => format!("while {}:", condition_text(cond)),
        StmtKind::After { seconds, .. } => match seconds {
            Term::Duration(d) => format!("after {}:", duration_text(*d)),
            other => format!("after {} seconds:", term_text(other)),
        },
        StmtKind::OnlyOnce { name, .. } => format!("only once{}:", latch_text(name)),
        StmtKind::FirstTimes { count, name, .. } => format!("first {} times{}:", term_text(count), latch_text(name)),
        StmtKind::Alternate { name, .. } => format!("alternate{}:", latch_text(name)),
        StmtKind::Through { gate, .. } => format!("through {gate}:"),
        StmtKind::Choose { value, .. } => format!("choose {}:", term_text(value)),
        StmtKind::Animate { var, from, to, seconds, .. } => {
            let how_long = match seconds {
                Term::Duration(d) => duration_text(*d),
                other => format!("{} seconds", term_text(other)),
            };
            format!("animate {var} from {} to {} over {how_long}:", term_text(from), term_text(to))
        }
        other => simple_text(other),
    }
}

fn latch_text(name: &Option<String>) -> String {
    name.as_ref().map(|n| format!(" as {n}")).unwrap_or_default()
}

/// The names written between braces in the text of a `format` (`"Score: {Score}"`): the character position of each
/// name inside the text, and the name. Only the first part of a field (`Transform` in `{Transform.Loc.X}`) is a name.
pub fn placeholder_names(text: &str) -> Vec<(usize, String)> {
    let chars: Vec<char> = text.chars().collect();
    let mut out = Vec::new();
    let mut i = 0;
    while i < chars.len() {
        if chars[i] == '{' {
            let mut j = i + 1;
            while j < chars.len() && chars[j] != '}' {
                j += 1;
            }
            let inner = &chars[i + 1..j.min(chars.len())];
            let lead = inner.iter().take_while(|c| c.is_whitespace()).count();
            let name: String = inner[lead..].iter().take_while(|c| c.is_alphanumeric() || **c == '_').collect();
            if !name.is_empty() {
                out.push((i + 1 + lead, name));
            }
            i = j;
        }
        i += 1;
    }
    out
}

/// `case 1 or 2:`
pub(crate) fn case_text(case: &Case) -> String {
    format!("case {}:", case_values_text(case))
}

/// `1 or 2`
pub(crate) fn case_values_text(case: &Case) -> String {
    case.values.iter().map(term_text).collect::<Vec<_>>().join(" or ")
}

/// `type_text`, for other modules.
pub(crate) fn type_text_pub(t: &TypeExpr) -> String {
    type_text(t)
}

fn type_text(t: &TypeExpr) -> String {
    match t {
        TypeExpr::Integer => "integer".into(),
        TypeExpr::Decimal => "decimal".into(),
        TypeExpr::Boolean => "boolean".into(),
        TypeExpr::Text => "text".into(),
        TypeExpr::Vector2 => "vector2".into(),
        TypeExpr::Vector3 => "vector3".into(),
        TypeExpr::Color => "color".into(),
        TypeExpr::Entity => "entity".into(),
        TypeExpr::Timer => "timer".into(),
        TypeExpr::Gate => "gate".into(),
        TypeExpr::Enum(n) | TypeExpr::Struct(n) => n.clone(),
        TypeExpr::List(inner) => format!("list of {}", type_text(inner)),
    }
}

fn parts_text(parts: &Parts) -> String {
    parts.iter().map(|(k, t)| format!(" {k}:{}", term_text(t))).collect()
}

fn decl_text(kind: &DeclKind) -> String {
    match kind {
        DeclKind::Variable { name, ty, value, parts, exposed, .. } => {
            let mut s = format!("variable name:{name} type:{}", type_text(ty));
            if let Some(v) = value {
                s.push_str(&format!(" value:{}", term_text(v)));
            }
            s.push_str(&parts_text(parts));
            if *exposed {
                s.push_str(" exposed:true");
            }
            s
        }
        DeclKind::Constant { name, ty, value, parts } => {
            let mut s = format!("constant name:{name} type:{}", type_text(ty));
            if let Some(v) = value {
                s.push_str(&format!(" value:{}", term_text(v)));
            }
            s.push_str(&parts_text(parts));
            s
        }
        DeclKind::NeedsComponent(n) => format!("needs component:{n}"),
        DeclKind::NeedsSystem(n) => format!("needs system:{n}"),
        DeclKind::NeedsEntity { name, file, tag } => format!("needs entity:{name} file:\"{file}\" tag:\"{tag}\""),
        DeclKind::NeedsEntities { name, tag, file } => {
            let mut s = format!("needs entities:{name} tag:\"{tag}\"");
            if let Some(f) = file {
                s.push_str(&format!(" file:\"{f}\""));
            }
            s
        }
        DeclKind::NeedsFile { file, alias, when } => {
            let mut s = format!("needs file:\"{file}\"");
            if let Some(a) = alias {
                s.push_str(&format!(" as {a}"));
            }
            if let Some(w) = when {
                s.push_str(&format!(" when:{w}"));
            }
            s
        }
        DeclKind::Tag(t) => format!("tag name:\"{t}\""),
        DeclKind::Event(n) => format!("event name:{n}"),
        DeclKind::Enum { name, values } => format!("enum name:{name} values:{}", values.join(" ")),
        DeclKind::Struct { name, .. } => format!("struct name:{name}"),
        DeclKind::Table { name, ty, .. } => format!("table name:{name} type:{ty}"),
    }
}

fn trigger_text(t: &Trigger) -> String {
    match t {
        Trigger::Created => "when created".into(),
        Trigger::Destroyed => "when destroyed".into(),
        Trigger::EveryFrame => "every frame".into(),
        Trigger::EverySeconds(n) => format!("every {}", duration_text(*n)),
        Trigger::Touches { file, name } => format!("when Self touches {file} as {name}"),
        Trigger::StopsTouching { file, name } => format!("when Self stops touching {file} as {name}"),
        Trigger::Changes(v) => format!("when {v} changes"),
        Trigger::Input { system, input, kind } => {
            let verb = if *kind == PressKind::Started { "starts" } else { "stops" };
            format!("when {system} {verb} pressing {input}")
        }
        Trigger::Sends { file, event, name } => format!("when {file} sends {event} as {name}"),
        Trigger::Event(n) => format!("when {n}"),
        Trigger::Damaged { amount, source } => match source {
            Some(s) => format!("when damaged as {amount} from {s}"),
            None => format!("when damaged as {amount}"),
        },
        Trigger::Hits { file, name } => format!("when Self hits {file} as {name}"),
    }
}

fn number_text(n: f64) -> String {
    format!("{n}")
}

fn duration_text(secs: f64) -> String {
    if secs == 1.0 {
        "1 second".to_string()
    } else {
        format!("{} seconds", number_text(secs))
    }
}

pub(crate) fn term_text(t: &Term) -> String {
    match t {
        Term::Int(i) => i.to_string(),
        Term::Decimal(d) => format!("{d:?}"),
        Term::Text(s) => format!("\"{s}\""),
        Term::Bool(b) => b.to_string(),
        Term::Name(n) => n.clone(),
        Term::Path { segments, owner } => {
            let mut s = segments.join(".");
            if let Some(o) = owner {
                s.push_str(&format!(" of {o}"));
            }
            s
        }
        Term::Duration(d) => duration_text(*d),
    }
}

fn args_text(args: &[Arg]) -> String {
    args.iter().map(|a| format!(" {}:{}", a.name, value_text(&a.value))).collect()
}

fn op_text(op: ArithOp) -> &'static str {
    match op {
        ArithOp::Plus => "plus",
        ArithOp::Minus => "minus",
        ArithOp::Times => "times",
        ArithOp::DividedBy => "divided by",
        ArithOp::Remainder => "remainder after dividing by",
    }
}

pub(crate) fn value_text(v: &Value) -> String {
    match v {
        Value::Term(t) => term_text(t),
        Value::PerSecond(t) => format!("{} per second", term_text(t)),
        Value::Distance { from, to } => format!("distance from {} to {}", term_text(from), term_text(to)),
        Value::CountOf(t) => format!("count of {}", term_text(t)),
        Value::ItemOf { index, list } => format!("item {} of {}", term_text(index), term_text(list)),
        Value::JoinedWith { left, right } => format!("{} joined with {}", term_text(left), term_text(right)),
        Value::TextOf(t) => format!("text of {}", term_text(t)),
        Value::DecimalOf(t) => format!("decimal of {}", term_text(t)),
        Value::IntegerOf(t) => format!("integer of {}", term_text(t)),
        Value::AbsoluteValueOf(t) => format!("absolute value of {}", term_text(t)),
        Value::ResultOf { action, args } => format!("result of {action}{}", args_text(args)),
        Value::Vector { x, y, z } => format!("vector x:{} y:{} z:{}", term_text(x), term_text(y), term_text(z)),
        Value::TimeSinceStart => "time since start".into(),
        Value::Lerp { t, from, to } => format!("{} of the way from {} to {}", term_text(t), term_text(from), term_text(to)),
        Value::Axis { system, input } => format!("axis {input} of {system}"),
        Value::Arith { left, op, right } => format!("{} {} {}", term_text(left), op_text(*op), term_text(right)),
        Value::Condition(c) => condition_text(c),
        Value::Phrase { node, args } => match crate::phrases::find(node) {
            Some(def) => def.render(&args.iter().map(term_text).collect::<Vec<_>>()),
            None => (*node).to_string(),
        },
        Value::NewStruct { ty, args } => format!("new {ty}{}", args_text(args)),
        Value::RowOf { key, table } => format!("row {} of {table}", term_text(key)),
        Value::Pick { yes, cond, no } => format!("{} if {} otherwise {}", term_text(yes), condition_text(cond), term_text(no)),
        Value::CastTo { value, file } => format!("cast {} to {file}", term_text(value)),
        Value::Format { parts } => {
            let mut s = String::from("format \"");
            for p in parts {
                match p {
                    FormatPart::Text(t) => s.push_str(t),
                    FormatPart::Value(t) => s.push_str(&format!("{{{}}}", term_text(t))),
                }
            }
            s.push('"');
            s
        }
    }
}

fn comparison_text(c: &Comparison) -> String {
    match c {
        Comparison::Compare { left, op, right } => {
            let word = match op {
                CmpOp::Eq => "is",
                CmpOp::NotEq => "is not",
                CmpOp::Above => "is above",
                CmpOp::Below => "is below",
                CmpOp::AtLeast => "is at least",
                CmpOp::AtMost => "is at most",
            };
            format!("{} {word} {}", value_text(left), value_text(right))
        }
        Comparison::Pressing { system, input, kind } => match kind {
            PressKind::Held => format!("{system} is pressing {input}"),
            PressKind::NotHeld => format!("{system} is not pressing {input}"),
            PressKind::Started => format!("{system} starts pressing {input}"),
            PressKind::Stopped => format!("{system} stops pressing {input}"),
        },
        Comparison::Presence { value, missing } => {
            format!("{} is {}", value_text(value), if *missing { "missing" } else { "present" })
        }
        Comparison::Contains { list, item, negated } => {
            format!("{} {} {}", value_text(list), if *negated { "does not contain" } else { "contains" }, value_text(item))
        }
    }
}

pub(crate) fn condition_text(c: &Condition) -> String {
    let joiner = if c.joiner == Some(Joiner::Or) { " or " } else { " and " };
    let mut s = comparison_text(&c.first);
    for r in &c.rest {
        s.push_str(joiner);
        s.push_str(&comparison_text(r));
    }
    s
}

fn limit_text(l: &Limit) -> String {
    let mut s = String::new();
    if let Some(t) = &l.capped {
        s.push_str(&format!(", capped at {}", term_text(t)));
    }
    if let Some(t) = &l.floored {
        s.push_str(&format!(", floored at {}", term_text(t)));
    }
    s
}

/// The text of a statement that fits on one line.
pub(crate) fn simple_text(kind: &StmtKind) -> String {
    match kind {
        StmtKind::Make { target, value, limit } => format!("make {} {}{}", term_text(target), value_text(value), limit_text(limit)),
        StmtKind::Add { value, target, limit } => format!("add {} to {}{}", value_text(value), term_text(target), limit_text(limit)),
        StmtKind::Subtract { value, target, limit } => {
            format!("subtract {} from {}{}", value_text(value), term_text(target), limit_text(limit))
        }
        StmtKind::Multiply { target, value } => format!("multiply {} by {}", term_text(target), value_text(value)),
        StmtKind::Divide { target, value } => format!("divide {} by {}", term_text(target), value_text(value)),
        StmtKind::Remove { value, list } => format!("remove {} from {}", value_text(value), term_text(list)),
        StmtKind::RemoveAt { index, list } => format!("remove item {} from {}", term_text(index), term_text(list)),
        StmtKind::AddUnique { value, list } => format!("add {} to {}, if new", value_text(value), term_text(list)),
        StmtKind::Clear(list) => format!("clear {}", term_text(list)),
        StmtKind::GateChange { gate, change } => {
            let word = match change {
                GateChange::Open => "open",
                GateChange::Close => "close",
                GateChange::Toggle => "toggle",
            };
            format!("{word} {gate}")
        }
        StmtKind::Damage { target, amount } => format!("damage {} by {}", term_text(target), value_text(amount)),
        StmtKind::Attach { child, parent } => format!("attach {} to {}", term_text(child), term_text(parent)),
        StmtKind::Detach(child) => format!("detach {}", term_text(child)),
        StmtKind::Tell { target, action, args } => format!("tell {} to {action}{}", term_text(target), args_text(args)),
        StmtKind::Do { action, on, args } => {
            let mut s = format!("do {action}");
            if let Some(o) = on {
                s.push_str(&format!(" on {o}"));
            }
            s.push_str(&args_text(args));
            s
        }
        StmtKind::Send(e) => format!("send {e}"),
        StmtKind::Remember { name, value } => format!("remember {name} {}", value_text(value)),
        StmtKind::Reset(t) => format!("reset {t}"),
        StmtKind::Spawn { file, at, name } => format!("spawn {file} at:{} as {name}", value_text(at)),
        StmtKind::Move { target, toward, by } => {
            format!("move {} toward {} by {}", term_text(target), value_text(toward), value_text(by))
        }
        StmtKind::MoveDir { target, dir, by } => {
            let d = match dir {
                Direction::Forward => "forward",
                Direction::Back => "back",
                Direction::Left => "left",
                Direction::Right => "right",
                Direction::Up => "up",
                Direction::Down => "down",
            };
            format!("move {} {d} by {}", term_text(target), value_text(by))
        }
        StmtKind::Print(v) => format!("print {}", value_text(v)),
        StmtKind::Destroy(t) => format!("destroy {}", term_text(t)),
        StmtKind::GiveBack(v) => format!("give back {}", value_text(v)),
        StmtKind::Finish => "finish".into(),
        StmtKind::StopLoop => "stop loop".into(),
        StmtKind::Skip => "skip".into(),
        // Block statements are printed by `Printer::stmt`; they never reach here as one-liners.
        StmtKind::If { .. }
        | StmtKind::Repeat { .. }
        | StmtKind::ForEach { .. }
        | StmtKind::ForRange { .. }
        | StmtKind::While { .. }
        | StmtKind::After { .. }
        | StmtKind::OnlyOnce { .. }
        | StmtKind::FirstTimes { .. }
        | StmtKind::Alternate { .. }
        | StmtKind::Through { .. }
        | StmtKind::Choose { .. }
        | StmtKind::Animate { .. } => unreachable!("block statements are printed with their bodies"),
    }
}

/// A declaration as one line of header text (for showing variables in the graph editor).
pub(crate) fn decl_line(kind: &DeclKind) -> String {
    decl_text(kind)
}
