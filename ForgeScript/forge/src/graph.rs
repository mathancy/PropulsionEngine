//! The script graph: a node-and-wire view of a `.forge` file, and edits made through it.
//!
//! The text is always the source of truth. A graph is built from the parsed file (one node per
//! sentence, wires for execution order and for computed values), and every edit made in the graph is
//! turned back into ForgeScript, checked by the real parser and checker, and only then saved. An edit
//! that would leave the file broken is refused with the reason, and the file is left exactly as it was.
//! Comments stay with the line they were on, because the file is rewritten with the formatter.
//!
//! The pieces:
//! - **Statement and event nodes** (`h0`, `h0.0.2`): one per handler or statement. They have an
//!   execution order (`Next`, and one output per body of a block) and a text field per input pin.
//! - **Value nodes** (`h0.0.2.v.Amount`): a pin whose text is a phrase (`distance from A to B`) shows
//!   as a separate node wired into the pin. The pin text is still the single source; the value node
//!   is a view of it.
//! - **Ids** are paths: `h3` is the fourth handler, `h3.0.2` its third statement, `h3.0.2.1.0` the
//!   first statement in the second body of that statement (`otherwise`, say). Ids change when nodes
//!   are added or removed, so a host reloads the graph after every edit.

use crate::ast::*;
use crate::error::ForgeError;
use crate::format::{condition_text, format_file};
use crate::nodes::{condition_insts, def, item_inst, latch_names, stmt_inst, value_insts, Ctx, Inst, NodeClass, NODES};
use crate::parser::parse;
use crate::project::Project;
use crate::Manifest;
use std::fmt::Write as _;
use std::path::Path;

// ---------------------------------------------------------------------------------------------
// The model
// ---------------------------------------------------------------------------------------------

#[derive(Debug, Clone)]
pub struct GraphPin {
    pub name: String,
    pub text: String,
    /// `value`, `condition`, `choice`, or `flag` (shown but not editable).
    pub kind: &'static str,
    pub optional: bool,
    pub editable: bool,
    pub choices: Vec<String>,
    /// The pin names a variable: the editor shows a dropdown of `choices` (what this script can use there)
    /// instead of a text field.
    pub picks: bool,
    /// The pin holds `true` or `false`, and the editor shows a checkbox (the Value of a Set Variable whose Target is
    /// a boolean).
    pub toggle: bool,
    /// The id of the value node wired into this pin, if its text is a phrase.
    pub wired: Option<String>,
}

#[derive(Debug, Clone)]
pub struct ExecOut {
    pub label: String,
    /// The first node that runs next, if any.
    pub to: Option<String>,
}

#[derive(Debug, Clone)]
pub struct GraphNode {
    pub id: String,
    pub name: String,
    pub class: NodeClass,
    pub category: String,
    pub line: usize,
    pub pins: Vec<GraphPin>,
    pub execs: Vec<ExecOut>,
    /// For a statement: the node (or handler) whose body it is in, which of that node's bodies, and
    /// its position in it. Empty and -1 for handlers and value nodes.
    pub parent: String,
    pub slot: i32,
    pub index: i32,
    /// For a value node: the statement (or value node's statement) and pin it feeds.
    pub feeds: Option<(String, String)>,
    pub error: Option<String>,
    pub comment: Option<String>,
    /// Where the node was last put in the editor (grid units), if it was ever moved.
    pub pos: Option<(i32, i32)>,
}

#[derive(Debug, Clone)]
pub struct GraphVariable {
    pub name: String,
    /// The declaration as written in the header.
    pub text: String,
    /// `variable` or `constant`.
    pub kind: &'static str,
    /// `decimal`, `list of integer`, ...
    pub ty: String,
    /// The starting value (`45.0`, `"hi"`, `true`), or empty.
    pub value: String,
    /// The named parts of a vector or color (`x:0 y:1 z:0`), or empty.
    pub parts: String,
    pub exposed: bool,
}

/// Something the header says this script reaches outside itself for: another entity, a list of entities,
/// or another script.
#[derive(Debug, Clone)]
pub struct GraphReference {
    /// What the script calls it (`Player`, `Foes`, `Coin`).
    pub name: String,
    /// The declaration as written in the header.
    pub text: String,
    /// `entity`, `entities` or `file`.
    pub kind: &'static str,
    /// The script it names (`player.forge`), or empty.
    pub file: String,
    pub tag: String,
    /// `used` or `startup` (a file), or empty.
    pub when: String,
}

/// An enum or a struct a script can use as a type (its own, or the built-in `hit`).
#[derive(Debug, Clone)]
pub struct GraphType {
    pub name: String,
    /// `enum` or `struct`.
    pub kind: &'static str,
    /// An enum's values.
    pub values: Vec<String>,
    /// A struct's fields: name and type.
    pub fields: Vec<(String, String)>,
}

#[derive(Debug, Clone, Default)]
pub struct Graph {
    pub nodes: Vec<GraphNode>,
    pub variables: Vec<GraphVariable>,
    pub types: Vec<GraphType>,
    pub references: Vec<GraphReference>,
    /// The components the script says it needs (`needs component:Transform`).
    pub components: Vec<String>,
    /// Problems the checker found in this file: line and message.
    pub errors: Vec<(usize, String)>,
    /// What Compile reports: those problems in the editor's words, each tied to the node (or variable)
    /// it is about, plus warnings (a variable nothing uses).
    pub problems: Vec<Problem>,
}

#[derive(Debug, Clone)]
pub struct Problem {
    /// `error` (the script will fail at Play) or `warning`.
    pub severity: &'static str,
    pub line: usize,
    pub message: String,
    /// The node it is about, if it is on a node's line.
    pub node: Option<String>,
    /// The variable it is about, if it is on a variable's declaration.
    pub variable: Option<String>,
}

fn class_name(c: NodeClass) -> &'static str {
    match c {
        NodeClass::Event => "Event",
        NodeClass::Action => "Action",
        NodeClass::Flow => "Flow",
        NodeClass::Data => "Data",
    }
}

const OPERATIONS: &[&str] = &["add", "subtract", "multiply", "divide"];
const DIRECTIONS: &[&str] = &["forward", "back", "left", "right", "up", "down"];
const OPERATORS: &[&str] = &["is", "is not", "is above", "is below", "is at least", "is at most"];
const STATES: &[&str] = &["is pressing", "is not pressing", "starts pressing", "stops pressing"];
const CHECKS: &[&str] = &["missing", "present"];
const MATCHES: &[&str] = &["contains", "does not contain"];

fn choices_for(pin: &str) -> Vec<&'static str> {
    match pin {
        "Operation" => OPERATIONS.to_vec(),
        "Direction" => DIRECTIONS.to_vec(),
        "Operator" => OPERATORS.to_vec(),
        "State" => STATES.to_vec(),
        "Check" => CHECKS.to_vec(),
        "Match" => MATCHES.to_vec(),
        _ => Vec::new(),
    }
}

/// Whether a pin is an argument of a call or a field of a struct: it is named after what the callee or the struct
/// calls it, so it is a plain value even when the name is also one of a node's own (`State`, `Check`, `Direction`).
fn is_argument(node: &str, pin: &str) -> bool {
    def(node).is_some_and(|d| d.pins.contains(&"Argument?")) && !matches!(pin, "Action" | "Target" | "Struct")
}

fn pin_kind(pin: &str) -> &'static str {
    if !choices_for(pin).is_empty() {
        "choice"
    } else if pin == "Else" {
        "flag"
    } else if pin == "Condition" || pin.starts_with("Else If") || pin.starts_with("Condition ") {
        "condition"
    } else {
        "value"
    }
}

// ---------------------------------------------------------------------------------------------
// Parsing small pieces of text (through the real parser)
// ---------------------------------------------------------------------------------------------

fn parse_error(e: &ForgeError) -> String {
    e.message.clone()
}

fn one_line(text: &str) -> Result<(), String> {
    if text.contains('\n') || text.contains('\r') || text.contains('\t') {
        return Err("a value has to be on one line".to_string());
    }
    Ok(())
}

thread_local! {
    /// The enums and structs of the script being worked on, as header lines. A small piece of text is parsed on
    /// its own to read a pin (`make Held new Item Cost:5`), and the parser only knows `Item` if it is declared.
    static TYPES: std::cell::RefCell<String> = const { std::cell::RefCell::new(String::new()) };
}

/// Remember the script's enums and structs for the probes below.
fn set_types(file: &File) {
    let mut lines = String::new();
    for d in &file.header {
        match &d.kind {
            DeclKind::Enum { .. } => {
                let _ = writeln!(lines, "\t{}", crate::format::decl_line(&d.kind));
            }
            DeclKind::Struct { fields, .. } => {
                let _ = writeln!(lines, "\t{}", crate::format::decl_line(&d.kind));
                for (name, ty) in fields {
                    let _ = writeln!(lines, "\t\tfield name:{name} type:{}", crate::format::type_text_pub(ty));
                }
            }
            _ => {}
        }
    }
    TYPES.with(|t| *t.borrow_mut() = lines);
}

fn types() -> String {
    TYPES.with(|t| t.borrow().clone())
}

/// Parse statements from lines (a line may start with tabs for nesting), as if written in a handler.
fn probe_stmts(lines: &[String]) -> Result<Vec<Stmt>, String> {
    for l in lines {
        one_line(l.trim_start_matches('\t'))?;
    }
    let mut src = format!("header:\n\tneeds component:Transform\n{}\nimplement:\n\tevery frame:\n", types());
    for l in lines {
        src.push_str("\t\t");
        src.push_str(l);
        src.push('\n');
    }
    let file = parse(&src).map_err(|e| parse_error(&e))?;
    match file.implement.into_iter().next().map(|i| i.kind) {
        Some(ItemKind::Handler { body, .. }) => Ok(body),
        _ => Err("could not read that".to_string()),
    }
}

fn probe_one(sentence: &str) -> Result<Stmt, String> {
    let mut stmts = probe_stmts(&[sentence.to_string()])?;
    if stmts.len() != 1 {
        return Err("that is more than one sentence".to_string());
    }
    Ok(stmts.remove(0))
}

fn parse_value(text: &str) -> Result<Value, String> {
    let s = probe_one(&format!("remember Probe {text}"))?;
    match s.kind {
        StmtKind::Remember { name, value } if name == "Probe" => Ok(value),
        _ => Err("that is not a single value".to_string()),
    }
}

fn parse_condition(text: &str) -> Result<Condition, String> {
    let s = probe_one(&format!("if {text}, then finish"))?;
    match s.kind {
        StmtKind::If { mut branches, otherwise: None, .. }
            if branches.len() == 1 && matches!(branches[0].1.as_slice(), [Stmt { kind: StmtKind::Finish, .. }]) =>
        {
            Ok(branches.remove(0).0)
        }
        _ => Err("that is not a single condition".to_string()),
    }
}

fn parse_item(sentence: &str) -> Result<Item, String> {
    one_line(sentence)?;
    let src = format!("header:\n\tneeds component:Transform\n{}\nimplement:\n\t{sentence}\n\t\tfinish\n", types());
    let mut file = parse(&src).map_err(|e| parse_error(&e))?;
    if file.implement.len() != 1 {
        return Err("that is not a single handler".to_string());
    }
    Ok(file.implement.remove(0))
}

fn parse_decl(line: &str) -> Result<Decl, String> {
    let src = format!("header:\n{}\t{line}\n\nimplement:\n\twhen created:\n\t\tfinish\n", types());
    let mut file = parse(&src).map_err(|e| parse_error(&e))?;
    let declared = types().lines().filter(|l| l.starts_with('\t') && !l.starts_with("\t\t")).count();
    if file.header.len() != declared + 1 {
        return Err("that is not a single declaration".to_string());
    }
    Ok(file.header.remove(declared))
}

// ---------------------------------------------------------------------------------------------
// Sentences from pins
// ---------------------------------------------------------------------------------------------

fn pin<'a>(inst: &'a Inst, name: &str) -> &'a str {
    inst.pins.iter().find(|p| p.0 == name).map(|p| p.1.as_str()).unwrap_or("")
}

fn limits(inst: &Inst) -> String {
    let mut s = String::new();
    let max = pin(inst, "Max");
    let min = pin(inst, "Min");
    if !max.trim().is_empty() {
        let _ = write!(s, ", capped at {}", max.trim());
    }
    if !min.trim().is_empty() {
        let _ = write!(s, ", floored at {}", min.trim());
    }
    s
}

/// The sentence (or, for a block, its first line) a statement node's pins make.
fn stmt_sentence(inst: &Inst) -> Result<String, String> {
    let p = |n: &str| pin(inst, n).trim().to_string();
    let need = |n: &str| -> Result<String, String> {
        let v = p(n);
        if v.is_empty() {
            Err(format!("`{n}` can't be empty"))
        } else {
            Ok(v)
        }
    };
    Ok(match inst.name {
        "Move Object" => format!("move {} {} by {}", need("Target")?, need("Direction")?, need("Amount")?),
        "Move Toward" => format!("move {} toward {} by {}", need("Target")?, need("Point")?, need("Amount")?),
        "Set Variable" | "Set Location" | "Set Rotation" | "Set Scale" => {
            format!("make {} {}{}", need("Target")?, need("Value")?, limits(inst))
        }
        "Rotate Object" | "Offset Location" | "Change Variable" => {
            let (target, amount) = (need("Target")?, need("Amount")?);
            match p("Operation").as_str() {
                "subtract" => format!("subtract {amount} from {target}{}", limits(inst)),
                "multiply" => format!("multiply {target} by {amount}"),
                "divide" => format!("divide {target} by {amount}"),
                _ => format!("add {amount} to {target}{}", limits(inst)),
            }
        }
        "Remove From List" => format!("remove {} from {}", need("Item")?, need("List")?),
        "Add Unique" => format!("add {} to {}, if new", need("Item")?, need("List")?),
        "Remove At" => format!("remove item {} from {}", need("Index")?, need("List")?),
        "Clear List" => format!("clear {}", need("List")?),
        "Open Gate" => format!("open {}", need("Gate")?),
        "Close Gate" => format!("close {}", need("Gate")?),
        "Toggle Gate" => format!("toggle {}", need("Gate")?),
        "Damage" => format!("damage {} by {}", need("Target")?, need("Amount")?),
        "Attach" => format!("attach {} to {}", need("Child")?, need("Parent")?),
        "Detach" => format!("detach {}", need("Child")?),
        "Send Message" => {
            let mut s = format!("tell {} to {}", need("Target")?, need("Action")?);
            for (name, text) in inst.pins.iter().filter(|(n, _)| n != "Action" && n != "Target") {
                let _ = write!(s, " {name}:{}", text.trim());
            }
            s
        }
        "Set Local" => format!("remember {} {}", need("Name")?, need("Value")?),
        "Call Action" | "Play Sound" | "Play Sound At" | "Play Sound Attached" | "Play Animation" | "Play Montage" | "Create Widget" | "Add To Viewport"
        | "Remove From Parent" | "Set Text" | "Open Level" | "Set Game Paused" | "System Action" => {
            let mut s = format!("do {}", need("Action")?);
            let target = p("Target");
            if !target.is_empty() && target != "Self" {
                let _ = write!(s, " on {target}");
            }
            for (name, text) in inst.pins.iter().filter(|(n, _)| n != "Action" && n != "Target") {
                let _ = write!(s, " {name}:{}", text.trim());
            }
            s
        }
        "Send Event" => format!("send {}", need("Event")?),
        "Reset Timer" => format!("reset {}", need("Timer")?),
        "Spawn Object" => format!("spawn {} at:{} as {}", need("File")?, need("Location")?, need("Name")?),
        "Destroy Object" => format!("destroy {}", need("Target")?),
        "Return Value" => format!("give back {}", need("Value")?),
        "Print Debug Message" => format!("print {}", need("Message")?),
        "Finish" => "finish".to_string(),
        "Break" => "stop loop".to_string(),
        "Continue" => "skip".to_string(),
        "Loop" => format!("for {} from {} to {}:", need("Variable")?, need("From")?, need("To")?),
        "Repeat" => format!("repeat {} times:", need("Count")?),
        "For Each" => format!("for each {} in {}:", need("Item")?, need("List")?),
        "While" => format!("while {}:", need("Condition")?),
        "Delay" => format!("after {} seconds:", need("Seconds")?),
        "Do Once" => format!("only once{}:", as_name(&p("Name"))),
        "Do N" => format!("first {} times{}:", need("Count")?, as_name(&p("Name"))),
        "Flip Flop" => format!("alternate{}:", as_name(&p("Name"))),
        "Gate" => format!("through {}:", need("Gate")?),
        "Switch" => format!("choose {}:", need("Value")?),
        "Timeline" => format!("animate {} from {} to {} over {} seconds:", need("Name")?, need("From")?, need("To")?, need("Seconds")?),
        other => return Err(format!("`{other}` can't be edited here")),
    })
}

/// ` as Greeting`, or nothing.
fn as_name(name: &str) -> String {
    if name.is_empty() {
        String::new()
    } else {
        format!(" as {name}")
    }
}

/// The sentence a handler node's pins make.
fn item_sentence(inst: &Inst) -> Result<String, String> {
    let p = |n: &str| pin(inst, n).trim().to_string();
    let need = |n: &str| -> Result<String, String> {
        let v = p(n);
        if v.is_empty() {
            Err(format!("`{n}` can't be empty"))
        } else {
            Ok(v)
        }
    };
    Ok(match inst.name {
        "On Begin Play" => "when created:".to_string(),
        "On Destroy" => "when destroyed:".to_string(),
        "On Tick" => {
            let s = p("Seconds");
            if s.is_empty() {
                "every frame:".to_string()
            } else {
                format!("every {s} seconds:")
            }
        }
        "When Action Pressed" => format!("when {} starts pressing {}:", need("System")?, need("Input")?),
        "When Action Released" => format!("when {} stops pressing {}:", need("System")?, need("Input")?),
        "On Overlap Begin" => format!("when Self touches {} as {}:", need("File")?, need("Name")?),
        "On Overlap End" => format!("when Self stops touching {} as {}:", need("File")?, need("Name")?),
        "On Variable Changed" => format!("when {} changes:", need("Variable")?),
        "On Event" => format!("when {}:", need("Event")?),
        "On Event From" => format!("when {} sends {} as {}:", need("File")?, need("Event")?, need("Name")?),
        "On Damaged" => {
            let source = p("Source");
            if source.is_empty() {
                format!("when damaged as {}:", need("Amount")?)
            } else {
                format!("when damaged as {} from {source}:", need("Amount")?)
            }
        }
        "On Hit" => format!("when Self hits {} as {}:", need("File")?, need("Name")?),
        _ => return Err("an action's name and parameters are edited in the text file".to_string()),
    })
}

/// The text of a value node: its pins put back into a phrase.
fn phrase(inst: &Inst) -> Result<String, String> {
    let p = |n: &str| pin(inst, n).trim().to_string();
    let need = |n: &str| -> Result<String, String> {
        let v = p(n);
        if v.is_empty() {
            Err(format!("`{n}` can't be empty"))
        } else {
            Ok(v)
        }
    };
    Ok(match inst.name {
        "Lerp" => format!("{} of the way from {} to {}", need("Alpha")?, need("From")?, need("To")?),
        "Get Axis" => format!("axis {} of {}", need("Input")?, need("System")?),
        "Distance" => format!("distance from {} to {}", need("From")?, need("To")?),
        "Count Of" => format!("count of {}", need("List")?),
        "Item Of" => format!("item {} of {}", need("Index")?, need("List")?),
        "Join Text" => format!("{} joined with {}", need("Left")?, need("Right")?),
        "To Text" => format!("text of {}", need("Value")?),
        "To Decimal" => format!("decimal of {}", need("Value")?),
        "To Integer" => format!("integer of {}", need("Value")?),
        "Absolute Value" => format!("absolute value of {}", need("Value")?),
        "Call Action Result" => {
            let mut s = format!("result of {}", need("Action")?);
            for (name, text) in inst.pins.iter().filter(|(n, _)| n != "Action") {
                let _ = write!(s, " {name}:{}", text.trim());
            }
            s
        }
        "Make Vector" => format!("vector x:{} y:{} z:{}", need("X")?, need("Y")?, need("Z")?),
        "Time Since Start" => "time since start".to_string(),
        "Add" => format!("{} plus {}", need("Left")?, need("Right")?),
        "Subtract" => format!("{} minus {}", need("Left")?, need("Right")?),
        "Multiply" => format!("{} times {}", need("Left")?, need("Right")?),
        "Divide" => format!("{} divided by {}", need("Left")?, need("Right")?),
        "Remainder" => format!("{} remainder after dividing by {}", need("Left")?, need("Right")?),
        "Per Second" => format!("{} per second", need("Value")?),
        "Compare" => format!("{} {} {}", need("Left")?, need("Operator")?, need("Right")?),
        "Input State" => format!("{} {} {}", need("System")?, need("State")?, need("Input")?),
        "Is Valid" => format!("{} is {}", need("Value")?, need("Check")?),
        "Data Table Row" => format!("row {} of {}", need("Key")?, need("Table")?),
        "Make Struct" => {
            let mut s = format!("new {}", need("Struct")?);
            for (name, text) in inst.pins.iter().filter(|(n, _)| n != "Struct") {
                let _ = write!(s, " {name}:{}", text.trim());
            }
            s
        }
        "Contains" => format!("{} {} {}", need("List")?, need("Match")?, need("Item")?),
        "Select" => format!("{} if {} otherwise {}", need("Yes")?, need("Condition")?, need("No")?),
        "Cast To" => format!("cast {} to {}", need("Value")?, need("File")?),
        "Format Text" => format!("format {}", need("Text")?),
        name if crate::phrases::find(name).is_some() => {
            let def = crate::phrases::find(name).expect("just checked");
            let mut args = Vec::new();
            for pin in def.pins() {
                args.push(need(pin)?);
            }
            def.render(&args)
        }
        "And" | "Or" => {
            let word = if inst.name == "And" { " and " } else { " or " };
            let parts: Vec<String> = inst.pins.iter().map(|(_, t)| t.trim().to_string()).collect();
            if parts.iter().any(|t| t.is_empty()) {
                return Err("every condition needs a value".to_string());
            }
            parts.join(word)
        }
        other => return Err(format!("`{other}` can't be edited here")),
    })
}

// ---------------------------------------------------------------------------------------------
// Value nodes
// ---------------------------------------------------------------------------------------------

fn single(c: &Condition, cmp: &Comparison) -> Condition {
    let _ = c;
    Condition { first: cmp.clone(), rest: Vec::new(), joiner: None }
}

/// The node a condition is (one comparison, or `And`/`Or` of several).
fn condition_inst(c: &Condition) -> Option<Inst> {
    if c.rest.is_empty() {
        let mut out = Vec::new();
        condition_insts(c, &mut out);
        return out.into_iter().next();
    }
    let name = if c.joiner == Some(Joiner::Or) { "Or" } else { "And" };
    let pins = std::iter::once(&c.first)
        .chain(c.rest.iter())
        .enumerate()
        .map(|(i, cmp)| (format!("Condition {}", i + 1), condition_text(&single(c, cmp))))
        .collect();
    Some(Inst { name, pins })
}

/// The value node a pin's text makes, if the text is a phrase and not just a name or number.
fn data_inst(text: &str, condition_pin: bool) -> Option<Inst> {
    if condition_pin {
        return condition_inst(&parse_condition(text).ok()?);
    }
    match parse_value(text).ok()? {
        Value::Term(_) => None,
        Value::Condition(c) => condition_inst(&c),
        v => {
            let mut out = Vec::new();
            value_insts(&v, &mut out);
            out.into_iter().next()
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Building the graph
// ---------------------------------------------------------------------------------------------

fn comment_of(notes: &Notes) -> Option<String> {
    let mut parts: Vec<String> = notes.leading.iter().map(|l| l.trim().to_string()).collect();
    if let Some(t) = &notes.trailing {
        parts.push(t.trim().to_string());
    }
    let text = parts.join("\n");
    (!text.is_empty()).then_some(text)
}

fn bodies(kind: &StmtKind) -> Vec<(&'static str, &Vec<Stmt>)> {
    match kind {
        StmtKind::If { branches, otherwise, .. } => {
            let mut v: Vec<(&'static str, &Vec<Stmt>)> = Vec::new();
            for (i, (_, body)) in branches.iter().enumerate() {
                v.push((if i == 0 { "Then" } else { "Else if" }, body));
            }
            if let Some(o) = otherwise {
                v.push(("Otherwise", o));
            }
            v
        }
        StmtKind::After { body, .. } => vec![("Later", body)],
        StmtKind::Repeat { body, .. } | StmtKind::ForEach { body, .. } | StmtKind::ForRange { body, .. } | StmtKind::While { body, .. } => {
            vec![("Body", body)]
        }
        StmtKind::OnlyOnce { body, .. } | StmtKind::FirstTimes { body, .. } => vec![("Do", body)],
        StmtKind::Alternate { first, second, .. } => vec![("A", first), ("B", second)],
        StmtKind::Through { body, .. } => vec![("Open", body)],
        StmtKind::Animate { body, .. } => vec![("Update", body)],
        StmtKind::Choose { cases, otherwise, .. } => {
            let mut v: Vec<(&'static str, &Vec<Stmt>)> = Vec::new();
            for (i, c) in cases.iter().enumerate() {
                v.push((CASE_LABELS.get(i).copied().unwrap_or("Case"), &c.body));
            }
            if let Some(o) = otherwise {
                v.push(("Otherwise", o));
            }
            v
        }
        _ => Vec::new(),
    }
}

/// The label of each case's output on a Switch node (a node has a handful; the script can have more).
const CASE_LABELS: &[&str] = &["Case 1", "Case 2", "Case 3", "Case 4", "Case 5", "Case 6", "Case 7", "Case 8", "Case 9", "Case 10", "Case 11", "Case 12"];

fn body_slot_mut(kind: &mut StmtKind, slot: usize) -> Option<&mut Vec<Stmt>> {
    match kind {
        StmtKind::If { branches, otherwise, .. } => {
            if slot < branches.len() {
                Some(&mut branches[slot].1)
            } else if slot == branches.len() {
                otherwise.as_mut()
            } else {
                None
            }
        }
        StmtKind::After { body, .. }
        | StmtKind::Repeat { body, .. }
        | StmtKind::ForEach { body, .. }
        | StmtKind::ForRange { body, .. }
        | StmtKind::While { body, .. }
        | StmtKind::OnlyOnce { body, .. }
        | StmtKind::FirstTimes { body, .. }
        | StmtKind::Through { body, .. }
        | StmtKind::Animate { body, .. } => {
            if slot == 0 {
                Some(body)
            } else {
                None
            }
        }
        StmtKind::Alternate { first, second, .. } => match slot {
            0 => Some(first),
            1 => Some(second),
            _ => None,
        },
        StmtKind::Choose { cases, otherwise, .. } => {
            if slot < cases.len() {
                Some(&mut cases[slot].body)
            } else if slot == cases.len() {
                otherwise.as_mut()
            } else {
                None
            }
        }
        _ => None,
    }
}

/// Give each `Else If` pin a number (`Else If 1`, `Else If 2`), so pins can be told apart.
fn numbered(mut inst: Inst) -> Inst {
    let mut n = 0;
    for p in inst.pins.iter_mut().filter(|p| p.0 == "Else If") {
        n += 1;
        p.0 = format!("Else If {n}");
    }
    inst
}

fn graph_pins(
    name: &str,
    inst: &Inst,
    nodes_out: &mut Vec<GraphNode>,
    id: &str,
    line: usize,
    editable: bool,
    value_pos: &[(String, (i32, i32))],
    declared: &std::collections::HashSet<String>,
) -> Vec<GraphPin> {
    let mut pins: Vec<GraphPin> = Vec::new();
    for (pname, text) in &inst.pins {
        let kind = if is_argument(name, pname) { "value" } else { pin_kind(pname) };
        let mut wired = None;
        if kind == "value" || kind == "condition" {
            if let Some(di) = data_inst(text, kind == "condition") {
                let vid = format!("{id}.v.{pname}");
                wired = Some(vid.clone());
                let dpins = di
                    .pins
                    .iter()
                    .map(|(n, t)| GraphPin {
                        name: n.clone(),
                        text: t.clone(),
                        kind: if is_argument(di.name, n) { "value" } else { pin_kind(n) },
                        optional: false,
                        editable: true,
                        choices: if is_argument(di.name, n) { Vec::new() } else { choices_for(n).into_iter().map(String::from).collect() },
                        picks: false,
                        toggle: false,
                        wired: None,
                    })
                    .collect();
                let ddef = def(di.name);
                nodes_out.push(GraphNode {
                    id: vid,
                    name: di.name.to_string(),
                    class: NodeClass::Data,
                    category: ddef.map(|d| d.category.to_string()).unwrap_or_default(),
                    line,
                    pins: dpins,
                    execs: Vec::new(),
                    parent: String::new(),
                    slot: -1,
                    index: -1,
                    feeds: Some((id.to_string(), pname.clone())),
                    error: None,
                    comment: None,
                    pos: value_pos.iter().find(|v| &v.0 == pname).map(|v| v.1),
                });
            } else if kind == "value" && declared.contains(text.trim()) {
                // A variable (or entity) carried to this pin by a Get node: the pin names it, and the node is
                // shown because it was given a place in the layout. It is only a way of looking at that text.
                if let Some(pos) = value_pos.iter().find(|v| &v.0 == pname).map(|v| v.1) {
                    let vid = format!("{id}.v.{pname}");
                    wired = Some(vid.clone());
                    nodes_out.push(GraphNode {
                        id: vid,
                        name: format!("Get {}", text.trim()),
                        class: NodeClass::Data,
                        category: "Variables".to_string(),
                        line,
                        pins: Vec::new(),
                        execs: Vec::new(),
                        parent: String::new(),
                        slot: -1,
                        index: -1,
                        feeds: Some((id.to_string(), pname.clone())),
                        error: None,
                        comment: None,
                        pos: Some(pos),
                    });
                }
            }
        }
        pins.push(GraphPin {
            name: pname.clone(),
            text: text.clone(),
            kind,
            optional: false,
            editable: editable && kind != "flag",
            choices: if is_argument(name, pname) { Vec::new() } else { choices_for(pname).into_iter().map(String::from).collect() },
            picks: false,
            toggle: false,
            wired,
        });
    }
    // Optional pins that aren't set yet are still shown, so they can be filled in.
    if let Some(d) = def(name) {
        for declared in d.pins {
            if let Some(opt) = declared.strip_suffix('?') {
                if matches!(opt, "Max" | "Min" | "Seconds" | "Name" | "Source") && !pins.iter().any(|p| p.name == opt) {
                    pins.push(GraphPin { name: opt.to_string(), text: String::new(), kind: "value", optional: true, editable, choices: Vec::new(), picks: false, toggle: false, wired: None });
                }
            }
        }
    }
    pins
}

fn emit_list(list: &[Stmt], owner: &str, slot: usize, ctx: &Ctx, declared: &std::collections::HashSet<String>, out: &mut Vec<GraphNode>) -> Option<String> {
    let mut first = None;
    for (idx, s) in list.iter().enumerate() {
        let id = format!("{owner}.{slot}.{idx}");
        if first.is_none() {
            first = Some(id.clone());
        }
        let inst = numbered(stmt_inst(&s.kind, ctx));
        let d = def(inst.name);
        let position = out.len();
        out.push(GraphNode {
            id: id.clone(),
            name: inst.name.to_string(),
            class: d.map(|d| d.class).unwrap_or(NodeClass::Action),
            category: d.map(|d| d.category.to_string()).unwrap_or_default(),
            line: s.line,
            pins: Vec::new(),
            execs: Vec::new(),
            parent: owner.to_string(),
            slot: slot as i32,
            index: idx as i32,
            feeds: None,
            error: None,
            comment: comment_of(&s.notes),
            pos: s.notes.pos,
        });
        let pins = graph_pins(inst.name, &inst, out, &id, s.line, true, &s.notes.value_pos, declared);
        let mut execs = Vec::new();
        for (k, (label, body)) in bodies(&s.kind).into_iter().enumerate() {
            execs.push(ExecOut { label: label.to_string(), to: emit_list(body, &id, k, ctx, declared, out) });
        }
        execs.push(ExecOut { label: "Next".to_string(), to: (idx + 1 < list.len()).then(|| format!("{owner}.{slot}.{}", idx + 1)) });
        out[position].pins = pins;
        out[position].execs = execs;
    }
    first
}

fn variable_of(kind: &DeclKind, name: &str, what: &'static str, ty: &TypeExpr, value: &Option<Term>, parts: &Parts, exposed: bool) -> GraphVariable {
    GraphVariable {
        name: name.to_string(),
        text: crate::format::decl_line(kind),
        kind: what,
        ty: crate::format::type_text_pub(ty),
        value: value.as_ref().map(crate::format::term_text).unwrap_or_default(),
        parts: parts.iter().map(|(k, t)| format!("{k}:{}", crate::format::term_text(t))).collect::<Vec<_>>().join(" "),
        exposed,
    }
}

/// Build the graph for a parsed file. `errors` are the checker's problems, which mark nodes.
pub fn build(file: &File, errors: &[ForgeError]) -> Graph {
    set_types(file);
    let ctx = Ctx::of(file);
    let declared = carried_names(file);
    let mut nodes: Vec<GraphNode> = Vec::new();
    for (i, item) in file.implement.iter().enumerate() {
        let id = format!("h{i}");
        let inst = item_inst(item);
        let d = def(inst.name);
        let position = nodes.len();
        nodes.push(GraphNode {
            id: id.clone(),
            name: inst.name.to_string(),
            class: NodeClass::Event,
            category: d.map(|d| d.category.to_string()).unwrap_or_default(),
            line: item.line,
            pins: Vec::new(),
            execs: Vec::new(),
            parent: String::new(),
            slot: -1,
            index: -1,
            feeds: None,
            error: None,
            comment: comment_of(&item.notes),
            pos: item.notes.pos,
        });
        let editable = inst.name != "Define Action";
        let pins = graph_pins(inst.name, &inst, &mut nodes, &id, item.line, editable, &item.notes.value_pos, &declared);
        let body = match &item.kind {
            ItemKind::Handler { body, .. } | ItemKind::Action { body, .. } => body,
        };
        let first = emit_list(body, &id, 0, &ctx, &declared, &mut nodes);
        nodes[position].pins = pins;
        nodes[position].execs = vec![ExecOut { label: "Run".to_string(), to: first }];
    }
    for n in nodes.iter_mut() {
        if let Some(e) = errors.iter().find(|e| e.line == n.line) {
            n.error = Some(e.message.clone());
        }
    }
    let variables = file
        .header
        .iter()
        .filter_map(|d| match &d.kind {
            DeclKind::Variable { name, ty, value, parts, exposed, .. } => Some(variable_of(&d.kind, name, "variable", ty, value, parts, *exposed)),
            DeclKind::Constant { name, ty, value, parts } => Some(variable_of(&d.kind, name, "constant", ty, value, parts, false)),
            _ => None,
        })
        .collect();
    let components = file.header.iter().filter_map(|d| if let DeclKind::NeedsComponent(c) = &d.kind { Some(c.clone()) } else { None }).collect();
    // A pin that names a variable is picked from what the script can use there, not typed.
    for n in nodes.iter_mut() {
        let node = n.name.clone();
        for p in n.pins.iter_mut().filter(|p| p.editable && p.wired.is_none()) {
            if let Some(role) = pin_role(&node, &p.name) {
                p.picks = true;
                p.choices = names_for(file, role);
            }
        }
        // What a Set Variable sets a boolean to is true or false: a checkbox, not a text field.
        if node == "Set Variable" {
            let target = n.pins.iter().find(|p| p.name == "Target").map(|p| p.text.trim().to_string()).unwrap_or_default();
            if boolean_variables(file).contains(&target) {
                for p in n.pins.iter_mut().filter(|p| p.name == "Value" && p.editable && p.wired.is_none() && matches!(p.text.trim(), "true" | "false")) {
                    p.toggle = true;
                }
            }
        }
    }
    let references = file.header.iter().filter_map(reference_of).collect();
    let mut types: Vec<GraphType> = file
        .header
        .iter()
        .filter_map(|d| match &d.kind {
            DeclKind::Enum { name, values } => Some(GraphType { name: name.clone(), kind: "enum", values: values.clone(), fields: Vec::new() }),
            DeclKind::Struct { name, fields } => Some(GraphType {
                name: name.clone(),
                kind: "struct",
                values: Vec::new(),
                fields: fields.iter().map(|(n, t)| (n.clone(), crate::format::type_text_pub(t))).collect(),
            }),
            _ => None,
        })
        .collect();
    types.push(GraphType {
        name: "hit".to_string(),
        kind: "struct",
        values: Vec::new(),
        fields: [("Blocked", "boolean"), ("Entity", "entity"), ("Location", "vector3"), ("Normal", "vector3"), ("Distance", "decimal")]
            .iter()
            .map(|(n, t)| (n.to_string(), t.to_string()))
            .collect(),
    });
    Graph { nodes, variables, types, references, components, errors: errors.iter().map(|e| (e.line, e.message.clone())).collect(), problems: Vec::new() }
}

/// The names a Get node can carry to a pin: the script's variables and constants, and the entities it
/// reaches for. (A script it talks to is not a value.)
fn carried_names(file: &File) -> std::collections::HashSet<String> {
    file.header
        .iter()
        .filter_map(|d| match &d.kind {
            DeclKind::Variable { name, .. } | DeclKind::Constant { name, .. } | DeclKind::NeedsEntity { name, .. } | DeclKind::NeedsEntities { name, .. } => Some(name.clone()),
            _ => None,
        })
        .collect()
}

/// The names of the script's boolean variables.
fn boolean_variables(file: &File) -> std::collections::HashSet<String> {
    file.header
        .iter()
        .filter_map(|d| match &d.kind {
            DeclKind::Variable { name, ty: TypeExpr::Boolean, .. } => Some(name.clone()),
            _ => None,
        })
        .collect()
}

/// A Set Variable that has just been pointed at a boolean still holds the number it started with: that becomes
/// `false`, so that its Value is a checkbox that says what the script does.
fn fit_boolean_target(file: &mut File, id: &str) {
    let Ok(nid) = parse_id(id) else { return };
    if nid.steps.is_empty() || nid.pin.is_some() {
        return;
    }
    let booleans = boolean_variables(file);
    let ctx = Ctx::of(file);
    let Ok((list, idx)) = locate(file, &nid) else { return };
    if stmt_inst(&list[idx].kind, &ctx).name != "Set Variable" {
        return;
    }
    let target = stmt_pin_text(&list[idx], &ctx, "Target").unwrap_or_default();
    let value = stmt_pin_text(&list[idx], &ctx, "Value").unwrap_or_default();
    let number = value.trim().chars().next().is_some_and(|c| c.is_ascii_digit() || c == '-');
    if booleans.contains(target.trim()) && (number || value.trim() == UNSET) {
        let _ = set_stmt_pin(&mut list[idx], &ctx, "Value", "false");
    }
}

/// What a pin that names a variable accepts.
#[derive(Clone, Copy, PartialEq)]
enum Role {
    /// A list: a list variable, or a `needs entities` (which is a live list of entities).
    Lists,
    Timers,
    /// Something that can be set: any variable but a timer.
    Writable,
    /// Something that can change: any variable.
    Changeable,
    /// A gate (`variable name:Door type:gate`).
    Gates,
}

fn pin_role(node: &str, pin: &str) -> Option<Role> {
    match (node, pin) {
        ("For Each" | "Count Of" | "Item Of" | "Remove From List" | "Add Unique" | "Remove At" | "Clear List" | "Find In List" | "Contains", "List") => Some(Role::Lists),
        ("Open Gate" | "Close Gate" | "Toggle Gate" | "Gate", "Gate") => Some(Role::Gates),
        ("Reset Timer", "Timer") => Some(Role::Timers),
        ("Set Variable" | "Change Variable", "Target") => Some(Role::Writable),
        ("On Variable Changed", "Variable") => Some(Role::Changeable),
        _ => None,
    }
}

/// The names in this script's header that fit a pin: its own variables, and the entities it says it
/// reaches for. (Locals made by `remember` and loop names live inside a handler, so they are not offered.)
fn names_for(file: &File, role: Role) -> Vec<String> {
    let mut out = Vec::new();
    for d in &file.header {
        match &d.kind {
            DeclKind::Variable { name, ty, .. } => {
                let fits = match role {
                    Role::Lists => matches!(ty, TypeExpr::List(_)),
                    Role::Timers => *ty == TypeExpr::Timer,
                    Role::Writable => *ty != TypeExpr::Timer && *ty != TypeExpr::Gate,
                    Role::Changeable => true,
                    Role::Gates => *ty == TypeExpr::Gate,
                };
                if fits {
                    out.push(name.clone());
                }
            }
            DeclKind::Constant { name, ty, .. } if role == Role::Lists && matches!(ty, TypeExpr::List(_)) => out.push(name.clone()),
            _ => {}
        }
    }
    // A timer starts over with `reset`, and so does a named `only once`, `first N times` or `alternate`.
    if role == Role::Timers {
        out.extend(latch_names(file));
    }
    // What the script reaches for comes after its own variables.
    if role == Role::Lists {
        for d in &file.header {
            if let DeclKind::NeedsEntities { name, .. } = &d.kind {
                out.push(name.clone());
            }
        }
    }
    out
}

/// What a `needs file` is called in the script: its `as` name, or the file's name with a capital.
fn file_alias(file: &str, alias: &Option<String>) -> String {
    alias.clone().unwrap_or_else(|| {
        let stem = file.rsplit('/').next().unwrap_or(file).trim_end_matches(".forge");
        let mut c = stem.chars();
        c.next().map(|f| f.to_uppercase().collect::<String>() + c.as_str()).unwrap_or_default()
    })
}

fn reference_of(d: &Decl) -> Option<GraphReference> {
    let text = crate::format::decl_line(&d.kind);
    Some(match &d.kind {
        DeclKind::NeedsEntity { name, file, tag } => GraphReference { name: name.clone(), text, kind: "entity", file: file.clone(), tag: tag.clone(), when: String::new() },
        DeclKind::NeedsEntities { name, tag, file } => {
            GraphReference { name: name.clone(), text, kind: "entities", file: file.clone().unwrap_or_default(), tag: tag.clone(), when: String::new() }
        }
        DeclKind::NeedsFile { file, alias, when } => {
            GraphReference { name: file_alias(file, alias), text, kind: "file", file: file.clone(), tag: String::new(), when: when.clone().unwrap_or_default() }
        }
        _ => return None,
    })
}

// ---------------------------------------------------------------------------------------------
// Finding nodes by id
// ---------------------------------------------------------------------------------------------

#[derive(Debug, Clone, PartialEq)]
struct NodeId {
    item: usize,
    /// (body slot, index) steps from the handler down to the statement.
    steps: Vec<(usize, usize)>,
    /// For a value node: the pin it feeds.
    pin: Option<String>,
}

fn parse_id(id: &str) -> Result<NodeId, String> {
    let bad = || format!("`{id}` isn't a node in this script");
    let (stmt, pin) = match id.split_once(".v.") {
        Some((a, b)) => (a, Some(b.to_string())),
        None => (id, None),
    };
    let mut parts = stmt.split('.');
    let head = parts.next().ok_or_else(bad)?;
    let item = head.strip_prefix('h').and_then(|n| n.parse().ok()).ok_or_else(bad)?;
    let nums: Vec<usize> = parts.map(|p| p.parse().map_err(|_| bad())).collect::<Result<_, _>>()?;
    if nums.len() % 2 != 0 {
        return Err(bad());
    }
    let steps = nums.chunks(2).map(|c| (c[0], c[1])).collect();
    Ok(NodeId { item, steps, pin })
}

fn item_body(item: &mut Item) -> &mut Vec<Stmt> {
    match &mut item.kind {
        ItemKind::Handler { body, .. } | ItemKind::Action { body, .. } => body,
    }
}

/// The list a statement is in, and its index in it.
fn locate<'a>(file: &'a mut File, id: &NodeId) -> Result<(&'a mut Vec<Stmt>, usize), String> {
    let missing = || "that node isn't in the script any more".to_string();
    if id.steps.is_empty() {
        return Err(missing());
    }
    let item = file.implement.get_mut(id.item).ok_or_else(missing)?;
    if id.steps[0].0 != 0 {
        return Err(missing());
    }
    let mut list: &mut Vec<Stmt> = item_body(item);
    for k in 0..id.steps.len() {
        let idx = id.steps[k].1;
        if k + 1 == id.steps.len() {
            if idx >= list.len() {
                return Err(missing());
            }
            return Ok((list, idx));
        }
        let tmp = list;
        let stmt = tmp.get_mut(idx).ok_or_else(missing)?;
        list = body_slot_mut(&mut stmt.kind, id.steps[k + 1].0).ok_or_else(missing)?;
    }
    Err(missing())
}

/// The body `slot` of the node `parent` ("h0" for a handler, a statement id otherwise).
fn body_of<'a>(file: &'a mut File, parent: &str, slot: usize) -> Result<&'a mut Vec<Stmt>, String> {
    let id = parse_id(parent)?;
    if id.pin.is_some() {
        return Err("a value node has no body".to_string());
    }
    if id.steps.is_empty() {
        if slot != 0 {
            return Err("a handler has one body".to_string());
        }
        let item = file.implement.get_mut(id.item).ok_or("that handler isn't in the script any more")?;
        return Ok(item_body(item));
    }
    let (list, idx) = locate(file, &id)?;
    body_slot_mut(&mut list[idx].kind, slot).ok_or_else(|| "that node has no such body".to_string())
}

// ---------------------------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------------------------

fn keep_bodies(old: StmtKind, new: StmtKind) -> Result<StmtKind, String> {
    Ok(match (old, new) {
        (StmtKind::Repeat { body, .. }, StmtKind::Repeat { times, .. }) => StmtKind::Repeat { times, body },
        (StmtKind::ForEach { body, .. }, StmtKind::ForEach { var, list, .. }) => StmtKind::ForEach { var, list, body },
        (StmtKind::ForRange { body, .. }, StmtKind::ForRange { var, from, to, .. }) => StmtKind::ForRange { var, from, to, body },
        (StmtKind::While { body, .. }, StmtKind::While { cond, .. }) => StmtKind::While { cond, body },
        (StmtKind::After { body, .. }, StmtKind::After { seconds, .. }) => StmtKind::After { seconds, body },
        (StmtKind::OnlyOnce { body, .. }, StmtKind::OnlyOnce { name, .. }) => StmtKind::OnlyOnce { name, body },
        (StmtKind::FirstTimes { body, .. }, StmtKind::FirstTimes { count, name, .. }) => StmtKind::FirstTimes { count, name, body },
        (StmtKind::Alternate { first, second, .. }, StmtKind::Alternate { name, .. }) => StmtKind::Alternate { name, first, second },
        (StmtKind::Through { body, .. }, StmtKind::Through { gate, .. }) => StmtKind::Through { gate, body },
        (StmtKind::Animate { body, .. }, StmtKind::Animate { var, from, to, seconds, .. }) => StmtKind::Animate { var, from, to, seconds, body },
        (StmtKind::Choose { cases, otherwise, .. }, StmtKind::Choose { value, .. }) => StmtKind::Choose { value, cases, otherwise },
        _ => return Err("that edit would change what kind of node this is".to_string()),
    })
}

fn is_block(kind: &StmtKind) -> bool {
    matches!(
        kind,
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
            | StmtKind::Animate { .. }
    )
}

/// The lines that parse as a block of this node with an empty-ish body, to read the node's own sentence back.
fn block_probe(sentence: String, node: &str) -> Vec<String> {
    match node {
        "Flip Flop" => vec![sentence, "\tfinish".to_string(), "otherwise:".to_string(), "\tfinish".to_string()],
        "Switch" => vec![sentence, "\tcase 1:".to_string(), "\t\tfinish".to_string()],
        _ => vec![sentence, "\tfinish".to_string()],
    }
}

/// The values of a `case` as text (`1 or 2`), read back into terms.
fn parse_case_values(text: &str) -> Result<Vec<Term>, String> {
    let lines = vec!["choose 0:".to_string(), format!("\tcase {}:", text.trim()), "\t\tfinish".to_string()];
    match probe_stmts(&lines)?.into_iter().next().map(|s| s.kind) {
        Some(StmtKind::Choose { mut cases, .. }) if !cases.is_empty() => Ok(cases.remove(0).values),
        _ => Err("that is not a value a case can have".to_string()),
    }
}

/// Set one pin of a statement to new text.
fn set_stmt_pin(stmt: &mut Stmt, ctx: &Ctx, pin_name: &str, text: &str) -> Result<(), String> {
    let mut inst = stmt_inst(&stmt.kind, ctx);
    if let StmtKind::If { branches, otherwise, inline } = &mut stmt.kind {
        let cond = parse_condition(text.trim())?;
        let which = if pin_name == "Condition" {
            0
        } else if let Some(n) = pin_name.strip_prefix("Else If") {
            n.trim().parse::<usize>().unwrap_or(1)
        } else {
            return Err(format!("`{pin_name}` can't be edited here"));
        };
        let branch = branches.get_mut(which).ok_or("that node has no such condition")?;
        branch.0 = cond;
        let _ = (otherwise, inline);
        return Ok(());
    }
    if let StmtKind::Choose { cases, .. } = &mut stmt.kind {
        if let Some(n) = pin_name.strip_prefix("Case ") {
            let which = n.trim().parse::<usize>().unwrap_or(0).checked_sub(1).ok_or("that node has no such case")?;
            let case = cases.get_mut(which).ok_or("that node has no such case")?;
            case.values = parse_case_values(text)?;
            return Ok(());
        }
    }
    match inst.pins.iter_mut().find(|p| p.0 == pin_name) {
        Some(p) => p.1 = text.to_string(),
        None => inst.pins.push((pin_name.to_string(), text.to_string())),
    }
    let sentence = stmt_sentence(&inst)?;
    if is_block(&stmt.kind) {
        let stmts = probe_stmts(&block_probe(sentence, inst.name))?;
        let new = stmts.into_iter().next().ok_or("could not read that")?;
        let old = std::mem::replace(&mut stmt.kind, StmtKind::Finish);
        stmt.kind = keep_bodies(old, new.kind)?;
    } else {
        stmt.kind = probe_one(&sentence)?.kind;
    }
    Ok(())
}

fn set_item_pin(item: &mut Item, pin_name: &str, text: &str) -> Result<(), String> {
    let mut inst = item_inst(item);
    match inst.pins.iter_mut().find(|p| p.0 == pin_name) {
        Some(p) => p.1 = text.to_string(),
        None => inst.pins.push((pin_name.to_string(), text.to_string())),
    }
    let sentence = item_sentence(&inst)?;
    let new = parse_item(&sentence)?;
    match (&mut item.kind, new.kind) {
        (ItemKind::Handler { trigger, .. }, ItemKind::Handler { trigger: new_trigger, .. }) => {
            *trigger = new_trigger;
            Ok(())
        }
        _ => Err("an action's name and parameters are edited in the text file".to_string()),
    }
}

/// The current text of a statement's pin.
fn stmt_pin_text(stmt: &Stmt, ctx: &Ctx, pin_name: &str) -> Option<String> {
    let inst = numbered(stmt_inst(&stmt.kind, ctx));
    inst.pins.iter().find(|p| p.0 == pin_name).map(|p| p.1.clone())
}

/// The text a statement's (or handler's) pin holds right now.
fn pin_text_now(file: &mut File, id: &str, pin: &str) -> Option<String> {
    let ctx = Ctx::of(file);
    let nid = parse_id(id).ok()?;
    if nid.steps.is_empty() {
        let item = file.implement.get(nid.item)?;
        item_inst(item).pins.iter().find(|p| p.0 == pin).map(|p| p.1.clone())
    } else {
        let (list, idx) = locate(file, &nid).ok()?;
        stmt_pin_text(&list[idx], &ctx, pin)
    }
}

fn set_pin(file: &mut File, id: &str, pin_name: &str, text: &str) -> Result<(), String> {
    let nid = parse_id(id)?;
    let ctx = Ctx::of(file);
    if nid.steps.is_empty() && nid.pin.is_none() {
        let item = file.implement.get_mut(nid.item).ok_or("that handler isn't in the script any more")?;
        return set_item_pin(item, pin_name, text);
    }
    if let Some(parent_pin) = nid.pin {
        // A value node: put its pins back into a phrase and set that as the text of the pin it feeds.
        let parent_id = id.split_once(".v.").map(|p| p.0).unwrap_or(id).to_string();
        let pnid = parse_id(&parent_id)?;
        let current = if pnid.steps.is_empty() {
            let item = file.implement.get(pnid.item).ok_or("that handler isn't in the script any more")?;
            item_inst(item).pins.iter().find(|p| p.0 == parent_pin).map(|p| p.1.clone())
        } else {
            let (list, idx) = locate(file, &pnid)?;
            stmt_pin_text(&list[idx], &ctx, &parent_pin)
        }
        .ok_or("that value isn't in the script any more")?;
        let mut data = data_inst(&current, pin_kind(&parent_pin) == "condition").ok_or("that value isn't a computed value any more")?;
        match data.pins.iter_mut().find(|p| p.0 == pin_name) {
            Some(p) => p.1 = text.to_string(),
            None => return Err(format!("this node has no `{pin_name}` pin")),
        }
        let new_text = phrase(&data)?;
        return set_pin(file, &parent_id, &parent_pin, &new_text);
    }
    let (list, idx) = locate(file, &nid)?;
    set_stmt_pin(&mut list[idx], &ctx, pin_name, text)
}

// ---- defaults for new nodes ----

struct Info {
    vars: Vec<(String, TypeExpr)>,
    all_names: Vec<String>,
    events: Vec<String>,
    actions: Vec<(String, Vec<Param>, Option<TypeExpr>)>,
    files: Vec<String>,
    structs: Vec<String>,
    tables: Vec<String>,
}

fn info(file: &File) -> Info {
    let mut i = Info { vars: Vec::new(), all_names: Vec::new(), events: Vec::new(), actions: Vec::new(), files: Vec::new(), structs: Vec::new(), tables: Vec::new() };
    for d in &file.header {
        match &d.kind {
            DeclKind::Variable { name, ty, .. } => {
                i.vars.push((name.clone(), ty.clone()));
                i.all_names.push(name.clone());
            }
            DeclKind::Constant { name, .. } | DeclKind::NeedsEntity { name, .. } | DeclKind::NeedsEntities { name, .. } => i.all_names.push(name.clone()),
            DeclKind::Event(e) => i.events.push(e.clone()),
            DeclKind::NeedsFile { file, alias, .. } => i.files.push(file_alias(file, alias)),
            DeclKind::Struct { name, .. } => i.structs.push(name.clone()),
            DeclKind::Table { name, .. } => i.tables.push(name.clone()),
            _ => {}
        }
    }
    for item in &file.implement {
        if let ItemKind::Action { name, params, gives, .. } = &item.kind {
            i.actions.push((name.clone(), params.clone(), gives.clone()));
        }
    }
    i
}

fn default_for(ty: &TypeExpr) -> Option<&'static str> {
    Some(match ty {
        TypeExpr::Integer => "0",
        TypeExpr::Decimal => "0.0",
        TypeExpr::Boolean => "false",
        TypeExpr::Text => "\"\"",
        TypeExpr::Vector2 => "vector x:0 y:0",
        TypeExpr::Vector3 => "vector x:0 y:0 z:0",
        _ => return None,
    })
}

fn fresh_name(i: &Info, base: &str) -> String {
    let mut n = base.to_string();
    let mut k = 2;
    while i.all_names.iter().any(|x| x == &n) {
        n = format!("{base}{k}");
        k += 1;
    }
    n
}

/// What a pin that has no variable plugged in yet is called in the script. The node editor shows it as
/// an empty pin, and the checker reports it ("isn't plugged in"), so it works like an unconnected input
/// in Blueprints: the node is placed freely, and Compile says what is missing.
pub const UNSET: &str = "unset";

/// The lines (the first is the sentence; later ones are its body) a new node starts as. A node that
/// needs a variable (a list for `For Each`, a timer for `Reset Timer`) starts with it unplugged; it is
/// never filled in with whatever the script happens to have. So the only refusals left are ones with no
/// text to show.
fn default_lines(name: &str, file: &File) -> Result<Vec<String>, String> {
    let i = info(file);
    let s = |t: &str| Ok(vec![t.to_string()]);
    let block = |t: &str| Ok(vec![t.to_string(), "\tfinish".to_string()]);
    match name {
        "Move Object" => s("move Self forward by 1.0"),
        "Move Toward" => s("move Transform.Loc toward Transform.Loc by 1.0"),
        "Set Variable" => s("make unset 0"),
        "Set Location" => s("make Transform.Loc.Y 0.0"),
        "Set Rotation" => s("make Transform.Rot.Y 0.0"),
        "Set Scale" => s("make Transform.Scale.X 1.0"),
        "Rotate Object" => s("add 45.0 per second to Transform.Rot.Y"),
        "Offset Location" => s("add 1.0 to Transform.Loc.Y"),
        "Change Variable" => s("add 1 to unset"),
        "Remove From List" => s("remove 0 from unset"),
        "Add Unique" => s("add 0 to unset, if new"),
        "Remove At" => s("remove item 0 from unset"),
        "Clear List" => s("clear unset"),
        "Open Gate" => s("open unset"),
        "Close Gate" => s("close unset"),
        "Toggle Gate" => s("toggle unset"),
        "Damage" => s("damage Self by 1.0"),
        "Attach" => s("attach Self to Self"),
        "Detach" => s("detach Self"),
        "Send Message" => s("tell Self to Action"),
        "Set Local" => s(&format!("remember {} 0", fresh_name(&i, "Value"))),
        "Call Action" => match i.actions.first() {
            Some((n, params, _)) => {
                let mut line = format!("do {n}");
                for p in params {
                    let d = default_for(&p.ty).ok_or("that action needs an argument this node can't fill in; add it by editing the text")?;
                    line.push_str(&format!(" {}:{d}", p.name));
                }
                s(&line)
            }
            None => s("do Action"),
        },
        "Play Sound" => s("do Play on Audio sound:\"sound.wav\""),
        "System Action" => s("do Hide on UI element:\"element\""),
        "Play Sound At" => s("do Play on Audio sound:\"sound.wav\" at:Transform.Loc"),
        "Play Sound Attached" => s("do Play on Audio sound:\"sound.wav\" attachedTo:Self"),
        "Play Animation" => s("do Play on Animation animation:\"animation\""),
        "Play Montage" => s("do PlayMontage on Animation montage:\"montage\""),
        "Create Widget" => s("do Create on UI widget:\"widget\""),
        "Add To Viewport" => s("do Show on UI element:\"widget\""),
        "Remove From Parent" => s("do Remove on UI element:\"widget\""),
        "Set Text" => s("do SetText on UI element:\"element\" text:\"\""),
        "Open Level" => s("do OpenLevel on Game level:\"level\""),
        "Set Game Paused" => s("do SetPaused on Game paused:true"),
        "Send Event" => match i.events.first() {
            Some(e) => s(&format!("send {e}")),
            None => s("send Event"),
        },
        "Reset Timer" => s("reset unset"),
        "Spawn Object" => match i.files.first() {
            Some(f) => s(&format!("spawn {f} at:Transform.Loc as {}", fresh_name(&i, "Spawned"))),
            None => s(&format!("spawn Prefab at:Transform.Loc as {}", fresh_name(&i, "Spawned"))),
        },
        "Destroy Object" => s("destroy Self"),
        "Return Value" => s("give back 0"),
        "Print Debug Message" => s("print \"Hello\""),
        "Finish" => s("finish"),
        "Break" => s("stop loop"),
        "Continue" => s("skip"),
        "Branch" => block("if true is true:"),
        "Loop" => block(&format!("for {} from 0 to 4:", fresh_name(&i, "Index"))),
        "Repeat" => block("repeat 3 times:"),
        "For Each" => block(&format!("for each {} in unset:", fresh_name(&i, "Item"))),
        "While" => block("while false is true:"),
        "Delay" => block("after 1 seconds:"),
        "Do Once" => block("only once:"),
        "Do N" => block("first 3 times:"),
        "Flip Flop" => Ok(vec!["alternate:".to_string(), "\tfinish".to_string(), "otherwise:".to_string(), "\tfinish".to_string()]),
        "Gate" => block("through unset:"),
        "Switch" => Ok(vec!["choose 0:".to_string(), "\tcase 1:".to_string(), "\t\tfinish".to_string()]),
        "Timeline" => block(&format!("animate {} from 0.0 to 1.0 over 1 seconds:", fresh_name(&i, "Progress"))),
        other => Err(format!("`{other}` can't be added from here")),
    }
}

fn default_trigger(name: &str, file: &File) -> Result<String, String> {
    let i = info(file);
    Ok(match name {
        "On Begin Play" => "when created:".to_string(),
        "On Destroy" => "when destroyed:".to_string(),
        "On Tick" => "every frame:".to_string(),
        "When Action Pressed" => "when Input starts pressing jump:".to_string(),
        "When Action Released" => "when Input stops pressing jump:".to_string(),
        "On Overlap Begin" => match i.files.first() {
            Some(f) => format!("when Self touches {f} as Visitor:"),
            None => "when Self touches Other as Visitor:".to_string(),
        },
        "On Overlap End" => match i.files.first() {
            Some(f) => format!("when Self stops touching {f} as Visitor:"),
            None => "when Self stops touching Other as Visitor:".to_string(),
        },
        "On Variable Changed" => "when unset changes:".to_string(),
        "On Event" => match i.events.first() {
            Some(e) => format!("when {e}:"),
            None => "when Event:".to_string(),
        },
        "On Damaged" => "when damaged as Amount from Attacker:".to_string(),
        "On Hit" => match i.files.first() {
            Some(f) => format!("when Self hits {f} as Obstacle:"),
            None => "when Self hits Other as Obstacle:".to_string(),
        },
        other => return Err(format!("`{other}` can't be added from here")),
    })
}

fn default_phrase(name: &str, file: &File) -> Result<String, String> {
    let i = info(file);
    Ok(match name {
        "Lerp" => "0.5 of the way from 0.0 to 1.0".to_string(),
        "Get Axis" => "axis moveForward of Input".to_string(),
        "Distance" => "distance from Self to Self".to_string(),
        "Count Of" => "count of unset".to_string(),
        "Item Of" => "item 0 of unset".to_string(),
        "Join Text" => "\"\" joined with \"\"".to_string(),
        "To Text" => "text of 0".to_string(),
        "To Decimal" => "decimal of 0".to_string(),
        "To Integer" => "integer of 0.0".to_string(),
        "Absolute Value" => "absolute value of 0".to_string(),
        "Call Action Result" => match i.actions.iter().find(|(_, p, g)| g.is_some() && p.iter().all(|p| default_for(&p.ty).is_some())) {
            Some((n, params, _)) => {
                let mut s = format!("result of {n}");
                for p in params {
                    s.push_str(&format!(" {}:{}", p.name, default_for(&p.ty).unwrap_or("0")));
                }
                s
            }
            None => "result of Action".to_string(),
        },
        "Make Vector" => "vector x:0 y:0 z:0".to_string(),
        "Time Since Start" => "time since start".to_string(),
        "Add" => "0 plus 0".to_string(),
        "Subtract" => "0 minus 0".to_string(),
        "Multiply" => "0 times 0".to_string(),
        "Divide" => "0 divided by 1".to_string(),
        "Remainder" => "0 remainder after dividing by 1".to_string(),
        "Per Second" => "1.0 per second".to_string(),
        "Compare" => "0 is above 0".to_string(),
        "Input State" => "Input is pressing jump".to_string(),
        "Is Valid" => "Self is present".to_string(),
        "And" => "true is true and true is true".to_string(),
        "Or" => "true is true or true is true".to_string(),
        "Contains" => "unset contains 0".to_string(),
        "Select" => "1 if true is true otherwise 0".to_string(),
        "Cast To" => format!("cast Self to {}", i.files.first().map(String::as_str).unwrap_or("Other")),
        "Make Struct" => match i.structs.first() {
            Some(s) => format!("new {s}"),
            None => return Err("declare a struct first, in the text: `struct name:Item`, with a `field name:Cost type:integer` line under it".to_string()),
        },
        "Data Table Row" => match i.tables.first() {
            Some(t) => format!("row \"\" of {t}"),
            None => return Err("declare a table first, in the text: `table name:Items type:Item`, with `row key:\"Sword\"` lines under it".to_string()),
        },
        "Line Trace" => "trace from Self to Self".to_string(),
        "Sphere Trace" => "sweep from Self to Self radius 0.5".to_string(),
        "Format Text" => "format \"Hello\"".to_string(),
        "Clamp" => "clamp 0.0 between 0.0 and 1.0".to_string(),
        "Map Range" => "map 0.0 from 0.0 to 1.0 into 0.0 to 1.0".to_string(),
        "Random Decimal" => "random decimal from 0.0 to 1.0".to_string(),
        "Random Integer" => "random integer from 1 to 6".to_string(),
        "Normalize" => "normalized Transform.Loc".to_string(),
        "Vector Length" => "length of Transform.Loc".to_string(),
        "Dot Product" => "dot product of Transform.Loc and Transform.Loc".to_string(),
        "Make Rotator" => "rotation pitch:0.0 yaw:0.0 roll:0.0".to_string(),
        "Look At Rotation" => "rotation looking from Self to Self".to_string(),
        "Forward Vector" => "forward of Self".to_string(),
        "Right Vector" => "right of Self".to_string(),
        "Up Vector" => "up of Self".to_string(),
        "Not" => "not false".to_string(),
        "Find In List" => "position of 0 in unset".to_string(),
        "Ease" => "ease 0.0 toward 1.0 at 5.0".to_string(),
        "Turn" => "turn Transform.Rot toward Transform.Rot at 90.0".to_string(),
        "Level Name" => "level name".to_string(),
        other => return Err(format!("`{other}` can't be computed here")),
    })
}

/// Make sure the header has what a node needs (`needs system:Audio`).
fn ensure_prelude(file: &mut File, node: &str) -> Result<(), String> {
    let Some(d) = def(node) else { return Ok(()) };
    for line in d.prelude {
        let decl = parse_decl(line)?;
        if !file.header.iter().any(|h| h.kind == decl.kind) {
            file.header.push(decl);
        }
    }
    Ok(())
}

fn new_stmt(kind: StmtKind) -> Stmt {
    Stmt { kind, line: 0, notes: Notes::default() }
}

fn add(file: &mut File, node: &str, parent: &str, slot: usize, index: usize) -> Result<(), String> {
    let d = def(node).ok_or_else(|| format!("there is no node called `{node}`"))?;
    match d.class {
        NodeClass::Event => {
            let sentence = default_trigger(node, file)?;
            let item = parse_item(&sentence)?;
            ensure_prelude(file, node)?;
            file.implement.push(Item { kind: item.kind, line: 0, notes: Notes { blank_before: true, ..Notes::default() } });
            Ok(())
        }
        NodeClass::Data => Err("A value node is added by plugging it into a pin.".to_string()),
        NodeClass::Action | NodeClass::Flow => {
            let lines = default_lines(node, file)?;
            let mut stmts = probe_stmts(&lines)?;
            let stmt = stmts.drain(..).next().ok_or("could not make that node")?;
            ensure_prelude(file, node)?;
            let list = body_of(file, parent, slot)?;
            let at = index.min(list.len());
            list.insert(at, new_stmt(stmt.kind));
            Ok(())
        }
    }
}

const MARK: &str = "__moving__";

/// The (body slot, index) steps down to the marked statement. `slot` is the body of its parent that
/// `list` is (0 for a handler's own body).
fn find_marked(list: &[Stmt], slot: usize, path: &mut Vec<(usize, usize)>) -> bool {
    for (idx, s) in list.iter().enumerate() {
        path.push((slot, idx));
        if s.notes.trailing.as_deref() == Some(MARK) {
            return true;
        }
        for (k, (_, body)) in bodies(&s.kind).into_iter().enumerate() {
            if find_marked(body, k, path) {
                return true;
            }
        }
        path.pop();
    }
    false
}

/// Deleting is always possible: a block left with nothing in it is written as `do nothing`, and a
/// script left with no handlers as `implement:` over `do nothing`.
fn delete(file: &mut File, id: &str) -> Result<(), String> {
    let nid = parse_id(id)?;
    if nid.pin.is_some() {
        return Err("Use \"unplug\" to remove a value node.".to_string());
    }
    if nid.steps.is_empty() {
        if nid.item >= file.implement.len() {
            return Err("that handler isn't in the script any more".to_string());
        }
        file.implement.remove(nid.item);
        return Ok(());
    }
    let (list, idx) = locate(file, &nid)?;
    list.remove(idx);
    Ok(())
}

fn move_node(file: &mut File, id: &str, parent: &str, slot: usize, index: usize) -> Result<(), String> {
    let nid = parse_id(id)?;
    if nid.pin.is_some() || nid.steps.is_empty() {
        return Err("Only statements can be moved.".to_string());
    }
    // Moving a node into itself (or something inside it) isn't possible.
    let pid = parse_id(parent)?;
    if pid.item == nid.item && pid.steps.len() >= nid.steps.len() && pid.steps[..nid.steps.len()] == nid.steps[..] {
        return Err("A node can't be moved inside itself.".to_string());
    }
    // Mark the node, insert a copy at the target (paths there are still the old ones), then
    // remove the marked original, wherever it ended up.
    let (list, idx) = locate(file, &nid)?;
    list[idx].notes.trailing = Some(MARK.to_string());
    let mut moved = list[idx].clone();
    moved.notes.trailing = None;
    let target = body_of(file, parent, slot)?;
    let at = index.min(target.len());
    target.insert(at, moved);

    let mut path = Vec::new();
    let item = file.implement.get_mut(nid.item).ok_or("could not move that node")?;
    if !find_marked(item_body(item), 0, &mut path) {
        return Err("could not move that node".to_string());
    }
    let marked = NodeId { item: nid.item, steps: path, pin: None };
    let (list, idx) = locate(file, &marked)?;
    list.remove(idx);
    Ok(())
}

fn plug(file: &mut File, stmt_id: &str, pin_name: &str, node: &str) -> Result<(), String> {
    let d = def(node).ok_or_else(|| format!("there is no node called `{node}`"))?;
    if d.class != NodeClass::Data {
        return Err("Only value nodes can be plugged into a pin.".to_string());
    }
    let text = default_phrase(node, file)?;
    ensure_prelude(file, node)?;
    set_pin(file, stmt_id, pin_name, &text)
}

fn unplug(file: &mut File, data_id: &str) -> Result<(), String> {
    let nid = parse_id(data_id)?;
    let pin_name = nid.pin.clone().ok_or("That isn't a value node.")?;
    let parent_id = data_id.split_once(".v.").map(|p| p.0).unwrap_or(data_id).to_string();
    // Replace the phrase with the first plain name or number inside it; the checker decides if it fits.
    let ctx = Ctx::of(file);
    let pnid = parse_id(&parent_id)?;
    let current = if pnid.steps.is_empty() {
        let item = file.implement.get(pnid.item).ok_or("that handler isn't in the script any more")?;
        item_inst(item).pins.iter().find(|p| p.0 == pin_name).map(|p| p.1.clone())
    } else {
        let (list, idx) = locate(file, &pnid)?;
        stmt_pin_text(&list[idx], &ctx, &pin_name)
    }
    .ok_or("that value isn't in the script any more")?;
    // A Get node is only a view of a name in the pin: removing it leaves the pin unplugged.
    if data_inst(&current, pin_kind(&pin_name) == "condition").is_none() && carried_names(file).contains(current.trim()) {
        return set_pin(file, &parent_id, &pin_name, UNSET);
    }
    let data = data_inst(&current, pin_kind(&pin_name) == "condition").ok_or("that isn't a computed value any more")?;
    let replacement = data
        .pins
        .iter()
        .map(|p| p.1.trim().to_string())
        .find(|t| matches!(parse_value(t), Ok(Value::Term(_))) && !CHOICE_WORDS.contains(&t.as_str()))
        .unwrap_or_else(|| "0".to_string());
    set_pin(file, &parent_id, &pin_name, &replacement)
}

const CHOICE_WORDS: &[&str] = &[
    "is", "is not", "is above", "is below", "is at least", "is at most", "missing", "present", "is pressing", "starts pressing", "contains", "does not contain",
];

fn add_case(file: &mut File, id: &str, which: &str) -> Result<(), String> {
    let nid = parse_id(id)?;
    let (list, idx) = locate(file, &nid)?;
    let StmtKind::Choose { cases, otherwise, .. } = &mut list[idx].kind else {
        return Err("Only a Switch node has cases.".to_string());
    };
    let finish = vec![new_stmt(StmtKind::Finish)];
    match which {
        "case" => {
            if cases.len() >= CASE_LABELS.len() {
                return Err(format!("A Switch node has room for {} cases.", CASE_LABELS.len()));
            }
            // The next whole number nobody has used yet.
            let next = cases.iter().flat_map(|c| c.values.iter()).filter_map(|v| if let Term::Int(i) = v { Some(*i) } else { None }).max().map_or(0, |m| m + 1);
            cases.push(Case { values: vec![Term::Int(next)], body: finish });
        }
        "otherwise" => {
            if otherwise.is_some() {
                return Err("This Switch already has an `otherwise`.".to_string());
            }
            *otherwise = Some(finish);
        }
        other => return Err(format!("`{other}` isn't a kind of case")),
    }
    Ok(())
}

fn add_branch(file: &mut File, id: &str, which: &str) -> Result<(), String> {
    if matches!(which, "case") || (which == "otherwise" && is_switch(file, id)) {
        return add_case(file, id, which);
    }
    let nid = parse_id(id)?;
    let (list, idx) = locate(file, &nid)?;
    let StmtKind::If { branches, otherwise, inline } = &mut list[idx].kind else {
        return Err("Only a Branch node has else-if and otherwise.".to_string());
    };
    *inline = false;
    let finish = vec![new_stmt(StmtKind::Finish)];
    match which {
        "else_if" => {
            if otherwise.is_some() {
                return Err("Add else-if before `otherwise`: remove `otherwise` first.".to_string());
            }
            let cond = parse_condition("true is true")?;
            branches.push((cond, finish));
        }
        "otherwise" => {
            if otherwise.is_some() {
                return Err("This branch already has an `otherwise`.".to_string());
            }
            *otherwise = Some(finish);
        }
        other => return Err(format!("`{other}` isn't a kind of branch")),
    }
    Ok(())
}

fn is_switch(file: &mut File, id: &str) -> bool {
    let Ok(nid) = parse_id(id) else { return false };
    matches!(locate(file, &nid), Ok((list, idx)) if matches!(list[idx].kind, StmtKind::Choose { .. }))
}

fn remove_branch(file: &mut File, id: &str, slot: usize) -> Result<(), String> {
    let nid = parse_id(id)?;
    let (list, idx) = locate(file, &nid)?;
    if let StmtKind::Choose { cases, otherwise, .. } = &mut list[idx].kind {
        if slot == cases.len() && otherwise.is_some() {
            *otherwise = None;
        } else if slot < cases.len() && cases.len() > 1 {
            cases.remove(slot);
        } else {
            return Err("A Switch keeps at least one case; delete the node instead.".to_string());
        }
        return Ok(());
    }
    let StmtKind::If { branches, otherwise, inline } = &mut list[idx].kind else {
        return Err("Only a Branch node has else-if and otherwise.".to_string());
    };
    *inline = false;
    if slot == branches.len() && otherwise.is_some() {
        *otherwise = None;
    } else if slot > 0 && slot < branches.len() {
        branches.remove(slot);
    } else {
        return Err("The first condition can't be removed; delete the node instead.".to_string());
    }
    Ok(())
}

/// Apply one edit to a parsed file.
// ---- variables ----

fn variable_decl(text: &str) -> Result<Decl, String> {
    one_line(text)?;
    let d = parse_decl(text.trim())?;
    match d.kind {
        DeclKind::Variable { .. } | DeclKind::Constant { .. } => Ok(d),
        _ => Err("That is not a variable or a constant.".to_string()),
    }
}

fn declared_name(kind: &DeclKind) -> Option<&str> {
    match kind {
        DeclKind::Variable { name, .. } | DeclKind::Constant { name, .. } => Some(name),
        DeclKind::NeedsEntity { name, .. } | DeclKind::NeedsEntities { name, .. } => Some(name),
        _ => None,
    }
}

fn is_variable(kind: &DeclKind) -> bool {
    matches!(kind, DeclKind::Variable { .. } | DeclKind::Constant { .. })
}

fn variable_index(file: &File, name: &str) -> Result<usize, String> {
    file.header
        .iter()
        .position(|d| is_variable(&d.kind) && declared_name(&d.kind) == Some(name))
        .ok_or_else(|| format!("`{name}` is not a variable in this script"))
}

// ---- references to other entities and scripts ----

fn reference_decl(text: &str) -> Result<Decl, String> {
    one_line(text)?;
    let d = parse_decl(text.trim())?;
    match d.kind {
        DeclKind::NeedsEntity { .. } | DeclKind::NeedsEntities { .. } | DeclKind::NeedsFile { .. } => Ok(d),
        _ => Err("That is not a reference to another entity or script.".to_string()),
    }
}

fn reference_name(kind: &DeclKind) -> Option<String> {
    reference_of(&Decl { kind: kind.clone(), line: 0, notes: Notes::default() }).map(|r| r.name)
}

fn reference_index(file: &File, name: &str) -> Result<usize, String> {
    file.header
        .iter()
        .position(|d| reference_name(&d.kind).as_deref() == Some(name))
        .ok_or_else(|| format!("`{name}` is not something this script reaches for"))
}

/// Where a variable or a reference is in the header, by the name the script uses for it.
fn named_index(file: &File, name: &str) -> Result<usize, String> {
    variable_index(file, name).or_else(|_| reference_index(file, name))
}

/// `add_reference`: `needs entity:Player file:"player.forge" tag:"player"`, `needs entities:Foes tag:"enemy"` or
/// `needs file:"coin.forge" as Coin`. It goes after the other references.
fn add_reference(file: &mut File, text: &str) -> Result<(), String> {
    let mut d = reference_decl(text)?;
    let name = reference_name(&d.kind).unwrap_or_default();
    if name == UNSET {
        return Err(format!("`{UNSET}` is what an unplugged pin is called, so this can't have that name."));
    }
    if file.header.iter().any(|h| declared_name(&h.kind) == Some(name.as_str()) || reference_name(&h.kind).as_deref() == Some(name.as_str())) {
        return Err(format!("`{name}` is already declared in this script."));
    }
    let at = file
        .header
        .iter()
        .rposition(|h| reference_name(&h.kind).is_some())
        .or_else(|| file.header.iter().rposition(|h| matches!(h.kind, DeclKind::NeedsComponent(_) | DeclKind::NeedsSystem(_))))
        .map(|i| i + 1)
        .unwrap_or(0);
    d.notes.blank_before = false;
    file.header.insert(at, d);
    Ok(())
}

/// `set_reference`: replace a reference's declaration (its file, tag, or when it loads), keeping its name.
fn set_reference(file: &mut File, name: &str, text: &str) -> Result<(), String> {
    let at = reference_index(file, name)?;
    let mut d = reference_decl(text)?;
    if reference_name(&d.kind).as_deref() != Some(name) {
        return Err("To change a name, rename it; that also updates the nodes that use it.".to_string());
    }
    d.notes = file.header[at].notes.clone();
    d.line = file.header[at].line;
    file.header[at] = d;
    Ok(())
}

fn delete_reference(file: &mut File, name: &str) -> Result<(), String> {
    let at = reference_index(file, name)?;
    file.header.remove(at);
    Ok(())
}

/// `add_variable`: a declaration like `variable name:Speed type:decimal value:45.0`. It goes into the
/// header after the other variables.
fn add_variable(file: &mut File, text: &str) -> Result<(), String> {
    let mut d = variable_decl(text)?;
    let name = declared_name(&d.kind).unwrap_or_default().to_string();
    if name == UNSET {
        return Err(format!("`{UNSET}` is what an unplugged pin is called, so a variable can't have that name."));
    }
    if file.header.iter().any(|h| declared_name(&h.kind) == Some(name.as_str())) {
        return Err(format!("`{name}` is already declared in this script."));
    }
    let after_last = file.header.iter().rposition(|h| is_variable(&h.kind)).map(|i| i + 1);
    d.notes.blank_before = after_last.is_none() && !file.header.is_empty();
    file.header.insert(after_last.unwrap_or(file.header.len()), d);
    Ok(())
}

/// `set_variable`: replace a variable's declaration (type, value, exposed), keeping its name.
fn set_variable(file: &mut File, name: &str, text: &str) -> Result<(), String> {
    let at = variable_index(file, name)?;
    let mut d = variable_decl(text)?;
    if declared_name(&d.kind) != Some(name) {
        return Err("To change a variable's name, rename it; that also updates the nodes that use it.".to_string());
    }
    let old = &file.header[at];
    // Entries of a list stay if it is still the same list.
    if let (DeclKind::Variable { ty: old_ty, entries: old_entries, .. }, DeclKind::Variable { ty, entries, .. }) = (&old.kind, &mut d.kind) {
        if old_ty == ty {
            *entries = old_entries.clone();
        }
    }
    d.notes = old.notes.clone();
    d.line = old.line;
    file.header[at] = d;
    Ok(())
}

fn delete_variable(file: &mut File, name: &str) -> Result<(), String> {
    let at = variable_index(file, name)?;
    file.header.remove(at);
    Ok(())
}

/// `rename_variable`: change the name in its declaration and everywhere the script uses it. Done on
/// the script's words (not its text), so names inside strings, comments, and after a dot (fields) are
/// left alone; the checker has the last word.
fn rename_variable(file: &mut File, old: &str, new: &str) -> Result<(), String> {
    named_index(file, old)?;
    let new = new.trim();
    if new == old {
        return Ok(());
    }
    // A name the language accepts (this also rejects spaces and dots).
    parse_decl(&format!("variable name:{new} type:integer")).map_err(|_| format!("`{new}` is not a name the script can use (letters and digits, no spaces)."))?;
    if new == UNSET {
        return Err(format!("`{UNSET}` is what an unplugged pin is called, so a variable can't have that name."));
    }
    if file.header.iter().any(|h| declared_name(&h.kind) == Some(new)) {
        return Err(format!("`{new}` is already declared in this script."));
    }

    let text = format_file(file).map_err(|e| e.message)?;
    let tokens = crate::lexer::lex(&text).map_err(|e| e.message)?;
    let mut spots: Vec<(usize, usize)> = Vec::new();
    for (i, t) in tokens.iter().enumerate() {
        // `format "Score: {Score}"`: a name between braces is a use of it, though it is inside a text.
        if let crate::lexer::Tok::Text(text) = &t.tok {
            if i > 0 && matches!(&tokens[i - 1].tok, crate::lexer::Tok::Word(w) if w == "format") {
                for (at, name) in crate::format::placeholder_names(text) {
                    if name == old {
                        spots.push((t.line, t.col + 1 + at));
                    }
                }
            }
            continue;
        }
        let crate::lexer::Tok::Word(w) = &t.tok else { continue };
        if w != old {
            continue;
        }
        let after_dot = i > 0 && tokens[i - 1].tok == crate::lexer::Tok::Dot;
        // `key:value` pairs: the word before the colon is a key, unless the colon ends the line.
        let is_key = matches!(tokens.get(i + 1).map(|t| &t.tok), Some(crate::lexer::Tok::Colon))
            && !matches!(tokens.get(i + 2).map(|t| &t.tok), Some(crate::lexer::Tok::Newline) | None);
        if !after_dot && !is_key {
            spots.push((t.line, t.col));
        }
    }
    let mut lines: Vec<Vec<char>> = text.lines().map(|l| l.chars().collect()).collect();
    spots.sort_unstable_by(|a, b| b.cmp(a));
    let width = old.chars().count();
    for (line, col) in spots {
        let chars = lines.get_mut(line - 1).ok_or("could not rename")?;
        let start = col - 1;
        if chars.get(start..start + width).map(|s| s.iter().collect::<String>()) != Some(old.to_string()) {
            return Err("could not rename (the script's text moved under the rename)".to_string());
        }
        chars.splice(start..start + width, new.chars());
    }
    let renamed: String = lines.into_iter().map(|l| l.into_iter().collect::<String>() + "\n").collect();
    let mut fresh = parse(&renamed).map_err(|e| e.message)?;
    // Every node keeps its position: the structure did not change, only words.
    let placed: Vec<(String, Option<(i32, i32)>, Vec<(String, (i32, i32))>)> =
        walk(file).into_iter().map(|(id, notes, _)| (id, notes.pos, notes.value_pos.clone())).collect();
    for (id, pos, value_pos) in placed {
        if let Some(notes) = notes_mut(&mut fresh, &id) {
            notes.pos = pos;
            notes.value_pos = value_pos;
        }
    }
    *file = fresh;
    Ok(())
}

pub fn apply(file: &mut File, op: &str, a: &[&str]) -> Result<(), String> {
    set_types(file);
    let arg = |i: usize| a.get(i).copied().unwrap_or("");
    let num = |i: usize| -> Result<usize, String> { arg(i).trim().parse().map_err(|_| format!("`{}` isn't a position", arg(i))) };
    match op {
        "set_pin" | "unplug" => {
            // The pin this edit is about: a value node's id says which pin it feeds.
            let target = match arg(0).split_once(".v.") {
                Some((parent, pin)) => Some((parent.to_string(), pin.to_string())),
                None if op == "set_pin" => Some((arg(0).to_string(), arg(1).to_string())),
                None => None,
            };
            let computed = |file: &mut File, t: &Option<(String, String)>| {
                t.as_ref().and_then(|(id, pin)| pin_text_now(file, id, pin).map(|text| data_inst(&text, pin_kind(pin) == "condition").is_some())).unwrap_or(false)
            };
            let was_computed = computed(file, &target);
            let result = if op == "unplug" { unplug(file, arg(0)) } else { set_pin(file, arg(0), arg(1), arg(2)) };
            if result.is_ok() && op == "set_pin" && arg(1) == "Target" {
                fit_boolean_target(file, arg(0));
            }
            // A place given to a computed value is not the place of a plain name that replaces it.
            if result.is_ok() && was_computed && !computed(file, &target) {
                if let Some((id, pin)) = &target {
                    if let Some(n) = notes_mut(file, id) {
                        n.value_pos.retain(|v| &v.0 != pin);
                    }
                }
            }
            result
        }
        "add" => add(file, arg(0), arg(1), num(2).unwrap_or(0), num(3).unwrap_or(usize::MAX)),
        "delete" => delete(file, arg(0)),
        "move" => move_node(file, arg(0), arg(1), num(2)?, num(3).unwrap_or(usize::MAX)),
        "plug" => plug(file, arg(0), arg(1), arg(2)),
        "add_branch" => add_branch(file, arg(0), arg(1)),
        "remove_branch" => remove_branch(file, arg(0), num(1)?),
        "add_variable" => add_variable(file, arg(0)),
        "set_variable" => set_variable(file, arg(0), arg(1)),
        "delete_variable" => delete_variable(file, arg(0)),
        "rename_variable" | "rename_reference" => rename_variable(file, arg(0), arg(1)),
        "add_reference" => add_reference(file, arg(0)),
        "set_reference" => set_reference(file, arg(0), arg(1)),
        "delete_reference" => delete_reference(file, arg(0)),
        other => Err(format!("`{other}` isn't something the graph can do")),
    }
}

// ---------------------------------------------------------------------------------------------
// Where the nodes are
// ---------------------------------------------------------------------------------------------
//
// A node's position is kept in a layout file beside the script (`door.forge` has `door.forge.layout`),
// not in the script, so the script stays plain. A position belongs to a line: it is carried on the
// line's `Notes` while the script is open, so adding, moving and deleting nodes keep every other
// node where it was. On disk a line is found again by its text (and, among identical lines, by
// order), so a script edited in a text editor keeps the positions of the lines that didn't change.

/// `door.forge` -> `door.forge.layout`.
pub fn layout_path(script: &Path) -> std::path::PathBuf {
    let name = script.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    script.with_file_name(format!("{name}.layout"))
}

#[derive(Debug, Clone)]
struct Placed {
    text: String,
    nth: usize,
    /// Set for the value node on this pin of the line.
    pin: Option<String>,
    pos: (i32, i32),
}

const LAYOUT_HEADER: &str = "forge-layout 1";

fn read_layout(path: &Path) -> Vec<Placed> {
    let Ok(text) = std::fs::read_to_string(layout_path(path)) else { return Vec::new() };
    let mut lines = text.lines();
    if lines.next().map(str::trim) != Some(LAYOUT_HEADER) {
        return Vec::new();
    }
    let mut out = Vec::new();
    for line in lines {
        let f: Vec<&str> = line.split('\t').collect();
        let num = |i: usize| f.get(i).and_then(|v| v.parse::<i32>().ok());
        match (f.first().copied(), num(1), num(2), num(3)) {
            (Some("node"), Some(x), Some(y), Some(nth)) if f.len() == 5 => {
                out.push(Placed { text: f[4].to_string(), nth: nth as usize, pin: None, pos: (x, y) });
            }
            (Some("value"), Some(x), Some(y), Some(nth)) if f.len() == 6 => {
                out.push(Placed { text: f[5].to_string(), nth: nth as usize, pin: Some(f[4].to_string()), pos: (x, y) });
            }
            _ => {}
        }
    }
    out
}

/// Every handler and statement in graph order: its id, its notes, and the line it is on.
fn walk(file: &File) -> Vec<(String, &Notes, usize)> {
    fn list<'a>(stmts: &'a [Stmt], owner: &str, slot: usize, out: &mut Vec<(String, &'a Notes, usize)>) {
        for (idx, s) in stmts.iter().enumerate() {
            let id = format!("{owner}.{slot}.{idx}");
            out.push((id.clone(), &s.notes, s.line));
            for (k, (_, body)) in bodies(&s.kind).into_iter().enumerate() {
                list(body, &id, k, out);
            }
        }
    }
    let mut out = Vec::new();
    for (i, item) in file.implement.iter().enumerate() {
        let id = format!("h{i}");
        out.push((id.clone(), &item.notes, item.line));
        let body = match &item.kind {
            ItemKind::Handler { body, .. } | ItemKind::Action { body, .. } => body,
        };
        list(body, &id, 0, &mut out);
    }
    out
}

fn line_text(src: &str, line: usize) -> String {
    src.lines().nth(line.saturating_sub(1)).unwrap_or("").trim().to_string()
}

fn notes_mut<'a>(file: &'a mut File, id: &str) -> Option<&'a mut Notes> {
    let nid = parse_id(id).ok()?;
    if nid.pin.is_some() {
        return None;
    }
    if nid.steps.is_empty() {
        return file.implement.get_mut(nid.item).map(|i| &mut i.notes);
    }
    let (list, idx) = locate(file, &nid).ok()?;
    Some(&mut list[idx].notes)
}

/// Put the positions read from the layout file on the lines of `file`, which was parsed from `src`.
fn attach_layout(file: &mut File, src: &str, placed: &[Placed]) {
    if placed.is_empty() {
        return;
    }
    let mut seen: std::collections::HashMap<String, usize> = std::collections::HashMap::new();
    let mut targets: Vec<(String, String, usize)> = Vec::new();
    for (id, _, line) in walk(file) {
        let text = line_text(src, line);
        let nth = *seen.entry(text.clone()).and_modify(|n| *n += 1).or_insert(0);
        targets.push((id, text, nth));
    }
    for (id, text, nth) in targets {
        let Some(notes) = notes_mut(file, &id) else { continue };
        for p in placed.iter().filter(|p| p.text == text && p.nth == nth) {
            match &p.pin {
                None => notes.pos = Some(p.pos),
                Some(pin) => notes.value_pos.push((pin.clone(), p.pos)),
            }
        }
    }
}

/// The layout file's text for `file`, whose script text is `src` (same lines, same order). None if
/// nothing has a position.
fn layout_text(file: &File, src: &str) -> Option<String> {
    let reparsed = parse(src).ok()?;
    let ours = walk(file);
    let theirs = walk(&reparsed);
    if ours.len() != theirs.len() {
        return None;
    }
    // Value nodes that exist right now; a position for one that was unplugged is dropped.
    let live: std::collections::HashSet<String> = build(file, &[]).nodes.into_iter().filter(|n| n.feeds.is_some()).map(|n| n.id).collect();
    let mut seen: std::collections::HashMap<String, usize> = std::collections::HashMap::new();
    let mut out = String::new();
    for ((id, notes, _), (_, _, line)) in ours.iter().zip(theirs.iter()) {
        let text = line_text(src, *line);
        let nth = *seen.entry(text.clone()).and_modify(|n| *n += 1).or_insert(0);
        if let Some((x, y)) = notes.pos {
            let _ = writeln!(out, "node\t{x}\t{y}\t{nth}\t{text}");
        }
        for (pin, (x, y)) in &notes.value_pos {
            if live.contains(&format!("{id}.v.{pin}")) {
                let _ = writeln!(out, "value\t{x}\t{y}\t{nth}\t{pin}\t{text}");
            }
        }
    }
    (!out.is_empty()).then(|| format!("{LAYOUT_HEADER}\n{out}"))
}

fn write_layout(path: &Path, file: &File, src: &str) {
    match layout_text(file, src) {
        Some(text) => {
            let _ = std::fs::write(layout_path(path), text);
        }
        None => {
            let _ = std::fs::remove_file(layout_path(path));
        }
    }
}

/// `set_positions`: lines of `id<TAB>x<TAB>y`, one per node that was moved.
fn set_positions(file: &mut File, payload: &str) -> Result<(), String> {
    for line in payload.lines().filter(|l| !l.trim().is_empty()) {
        let f: Vec<&str> = line.split('\t').collect();
        let (Some(id), Some(x), Some(y)) =
            (f.first(), f.get(1).and_then(|v| v.trim().parse::<i32>().ok()), f.get(2).and_then(|v| v.trim().parse::<i32>().ok()))
        else {
            return Err(format!("`{line}` isn't a node position"));
        };
        match id.split_once(".v.") {
            Some((parent, pin)) => {
                let notes = notes_mut(file, parent).ok_or_else(|| format!("`{id}` isn't a node in this script"))?;
                notes.value_pos.retain(|v| v.0 != pin);
                notes.value_pos.push((pin.to_string(), (x, y)));
            }
            None => notes_mut(file, id).ok_or_else(|| format!("`{id}` isn't a node in this script"))?.pos = Some((x, y)),
        }
    }
    Ok(())
}

// ---------------------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------------------

/// The checker's problems for one file on disk.
fn diagnostics(path: &Path) -> Result<Vec<ForgeError>, String> {
    let project = Project::load(path)?;
    let key = Project::key_for(path);
    Ok(project.check(&Manifest::builtin()).into_iter().filter(|r| r.path == key).flat_map(|r| r.errors).collect())
}

/// The checker's complaint in the editor's words: (what to show on the node, what to list in Compile).
fn friendly(message: &str, node: &GraphNode) -> (String, String) {
    let short = if message.contains(&format!("`{UNSET}`")) {
        let pin = node.pins.iter().find(|p| p.text.split_whitespace().any(|w| w == UNSET)).map(|p| p.name.clone()).unwrap_or_else(|| "a pin".to_string());
        format!("{pin} isn't plugged in yet")
    } else if let Some(rest) = message.strip_prefix("I don't know `") {
        let name = rest.split('`').next().unwrap_or("");
        format!("`{name}` isn't declared: declare a variable called {name}, or plug in one that is")
    } else {
        message.to_string()
    };
    let full = format!("{}: {short}", node.name);
    (short, full)
}

/// Work out what Compile reports for a loaded graph.
fn compile_problems(g: &mut Graph, file: &File, src: &str) {
    let mut problems: Vec<Problem> = Vec::new();
    for (line, message) in g.errors.clone() {
        let node = g.nodes.iter().position(|n| n.line == line && n.feeds.is_none());
        let variable = file.header.iter().find(|d| d.line == line).and_then(|d| if is_variable(&d.kind) { declared_name(&d.kind).map(str::to_string) } else { reference_name(&d.kind) });
        let (text, node_id) = match node {
            Some(i) => {
                let (short, full) = friendly(&message, &g.nodes[i]);
                g.nodes[i].error = Some(short);
                (full, Some(g.nodes[i].id.clone()))
            }
            None => (message.clone(), None),
        };
        problems.push(Problem { severity: "error", line, message: text, node: node_id, variable });
    }
    // A variable nothing uses (one marked exposed is for the Objects placed in a level to set, so it is left alone).
    if let Ok(tokens) = crate::lexer::lex(src) {
        for d in &file.header {
            let DeclKind::Variable { name, exposed, .. } = &d.kind else { continue };
            if *exposed {
                continue;
            }
            let used = tokens.iter().any(|t| t.line != d.line && matches!(&t.tok, crate::lexer::Tok::Word(w) if w == name));
            if !used {
                problems.push(Problem { severity: "warning", line: d.line, message: format!("`{name}` is declared but nothing uses it"), node: None, variable: Some(name.clone()) });
            }
        }
    }
    g.problems = problems;
}

/// Read a file and build its graph.
pub fn load(path: &Path) -> Result<Graph, String> {
    let src = std::fs::read_to_string(path).map_err(|e| format!("can't read {}: {e}", path.display()))?;
    let mut file = parse(&src).map_err(|e| e.render(&src, &path.to_string_lossy()))?;
    attach_layout(&mut file, &src, &read_layout(path));
    let errors = diagnostics(path).unwrap_or_default();
    let mut graph = build(&file, &errors);
    compile_problems(&mut graph, &file, &src);
    Ok(graph)
}

/// Whether `text` is a script the graph can show (it parses), and if not where it stopped:
/// (line, column, message), both 1-based. This is the same bar every graph edit has to clear, so a
/// text that passes here is one `set_source` will accept. The checker's problems (an unplugged pin,
/// an undeclared name) are not errors here: the graph shows them on their nodes.
pub fn check_source(text: &str) -> Result<(), (usize, usize, String)> {
    parse(text).map(|_| ()).map_err(|e| (e.line, e.col, e.message))
}

/// Replace the whole script at `path` with `text` (what typing in the code pane does). The text is
/// written as it is, not re-formatted, so what was typed stays where it was typed; the next edit
/// made in the graph formats it like any other. It must parse, or the file is left as it was. The
/// layout file is left alone: a node keeps its place as long as its line's text is unchanged.
pub fn set_source(path: &Path, text: &str) -> Result<Graph, String> {
    parse(text).map_err(|e| e.render(text, &path.to_string_lossy()))?;
    // Written beside the script and renamed over it, like every other edit, so the file is never half-written.
    let name = path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    let candidate = path.with_file_name(format!(".{name}.graph-edit"));
    std::fs::write(&candidate, text).map_err(|e| format!("can't write {}: {e}", candidate.display()))?;
    if let Err(e) = std::fs::rename(&candidate, path) {
        let _ = std::fs::remove_file(&candidate);
        return Err(format!("can't write {}: {e}", path.display()));
    }
    load(path)
}

/// Apply an edit to the script at `path`, check the result, and save it. If the edit would leave the
/// script with a new problem, the file is left as it was and the reason is the error.
pub fn edit(path: &Path, op: &str, args: &[&str]) -> Result<Graph, String> {
    // Replacing the text does not start from the old text, which may not even parse.
    if op == "set_source" {
        return set_source(path, args.first().copied().unwrap_or(""));
    }
    let original = std::fs::read_to_string(path).map_err(|e| format!("can't read {}: {e}", path.display()))?;
    let mut file = parse(&original).map_err(|e| e.render(&original, &path.to_string_lossy()))?;
    attach_layout(&mut file, &original, &read_layout(path));

    // Moving nodes changes where they are drawn, not the script: only the layout file is written.
    if op == "set_positions" {
        set_positions(&mut file, args.first().copied().unwrap_or(""))?;
        write_layout(path, &file, &original);
        return load(path);
    }

    let before: Vec<String> = diagnostics(path).unwrap_or_default().into_iter().map(|e| e.message).collect();

    apply(&mut file, op, args)?;
    let text = format_file(&file).map_err(|e| e.message)?;

    // Check the result in a copy next to the script (so `needs file` paths still resolve), and only
    // replace the script once it passes. A refused edit never touches the real file at all.
    let name = path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    let candidate = path.with_file_name(format!(".{name}.graph-edit"));
    std::fs::write(&candidate, &text).map_err(|e| format!("can't write {}: {e}", candidate.display()))?;
    // Placing and editing nodes is free, like in Blueprints: the result only has to be a script that
    // parses (a field that can't be turned into a sentence is refused). What the checker finds wrong, an
    // unplugged pin or an undeclared name, is shown on the node and listed by Compile. A rename is
    // different: it must not make anything worse.
    let result = if op == "rename_variable" || op == "rename_reference" { check_candidate(&candidate, &before) } else { parse(&text).map(|_| ()).map_err(|e| e.message) };
    if let Err(reason) = result {
        let _ = std::fs::remove_file(&candidate);
        return Err(reason);
    }
    if let Err(e) = std::fs::rename(&candidate, path) {
        let _ = std::fs::remove_file(&candidate);
        return Err(format!("can't write {}: {e}", path.display()));
    }
    write_layout(path, &file, &text);
    load(path)
}

/// Pins that name something (a variable being made, an action, a system) or that the user's variable would not
/// be "going into": a Get node's wire is never offered for these.
const NOT_FOR_A_VARIABLE: &[&str] = &["Action", "Target", "Name", "Variable", "Event", "File", "System", "Struct", "Argument", "Parameter", "Gives"];

/// What goes into `pin` to carry `variable` there. A condition has to compare something (`if Alive:` is not
/// ForgeScript), so a variable carried to one is asked whether it is true.
pub fn fit_text(pin: &str, variable: &str) -> String {
    if pin_kind(pin) == "condition" {
        format!("{variable} is true")
    } else {
        variable.to_string()
    }
}

/// Which nodes that aren't in the script yet could take `variable` in one of their pins, as JSON:
/// `[{"node":"Branch","pin":"Condition","category":"Flow","text":"Alive is true"}]` (`text` is what to put in the pin).
/// The checker says so: each statement node is put at the
/// end of the script's last handler in a copy, once alone and once with the variable in the pin, and the pin fits if
/// the second copy has no problem the first didn't. (Value nodes are not asked about: they only exist plugged into a
/// pin.) Nothing is written to the script.
pub fn fits(path: &Path, variable: &str) -> Result<String, String> {
    let original = std::fs::read_to_string(path).map_err(|e| format!("can't read {}: {e}", path.display()))?;
    let name = path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    let candidate = path.with_file_name(format!(".{name}.graph-fit"));

    // What the checker says about the script with `node` added (and the variable in `pin`, if one is given).
    let problems = |node: &str, pin: Option<&str>| -> Result<Vec<String>, String> {
        let mut file = parse(&original).map_err(|e| e.message)?;
        if !file.implement.iter().any(|i| matches!(i.kind, ItemKind::Handler { .. })) {
            apply(&mut file, "add", &["On Begin Play", "", "0", "0"])?;
        }
        let host = file.implement.iter().rposition(|i| matches!(i.kind, ItemKind::Handler { .. })).unwrap_or(0);
        let parent = format!("h{host}");
        let at = body_of(&mut file, &parent, 0)?.len();
        apply(&mut file, "add", &[node, &parent, "0", &at.to_string()])?;
        if let Some(pin) = pin {
            apply(&mut file, "set_pin", &[&format!("{parent}.0.{at}"), pin, &fit_text(pin, variable)])?;
        }
        let text = format_file(&file).map_err(|e| e.message)?;
        std::fs::write(&candidate, &text).map_err(|e| format!("can't write {}: {e}", candidate.display()))?;
        Ok(diagnostics(&candidate)?.into_iter().map(|e| e.message).collect())
    };

    let mut found: Vec<String> = Vec::new();
    for d in NODES.iter().filter(|d| matches!(d.class, NodeClass::Action | NodeClass::Flow)) {
        let Ok(plain) = problems(d.name, None) else { continue };
        for pin in d.pins.iter().filter(|p| !p.ends_with('?')) {
            if NOT_FOR_A_VARIABLE.contains(pin) || !matches!(pin_kind(pin), "value" | "condition") || (d.name == "For Each" && *pin == "Item") {
                continue;
            }
            let Ok(with) = problems(d.name, Some(pin)) else { continue };
            if with.iter().all(|m| plain.contains(m)) {
                found.push(format!("{{\"node\":{},\"pin\":{},\"category\":{},\"text\":{}}}", esc(d.name), esc(pin), esc(d.category), esc(&fit_text(pin, variable))));
            }
        }
    }
    let _ = std::fs::remove_file(&candidate);
    Ok(format!("[{}]", found.join(",")))
}

/// Whether the candidate script has any problem that the original didn't already have.
fn check_candidate(candidate: &Path, before: &[String]) -> Result<(), String> {
    let after = diagnostics(candidate)?;
    // Problems that were already there don't block an edit; new ones do.
    let mut remaining: Vec<String> = before.to_vec();
    let mut fresh: Vec<&ForgeError> = Vec::new();
    for e in &after {
        match remaining.iter().position(|m| m == &e.message) {
            Some(i) => {
                remaining.remove(i);
            }
            None => fresh.push(e),
        }
    }
    if let Some(e) = fresh.first() {
        return Err(e.message.clone());
    }
    Ok(())
}

// ---------------------------------------------------------------------------------------------
// JSON (written by hand; the library has no dependencies)
// ---------------------------------------------------------------------------------------------

fn esc(s: &str) -> String {
    let mut out = String::from("\"");
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => {
                let _ = write!(out, "\\u{:04x}", c as u32);
            }
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

fn opt(s: &Option<String>) -> String {
    match s {
        Some(t) => esc(t),
        None => "null".to_string(),
    }
}

impl Graph {
    pub fn to_json(&self) -> String {
        let mut j = String::from("{\"nodes\":[");
        for (i, n) in self.nodes.iter().enumerate() {
            if i > 0 {
                j.push(',');
            }
            let _ = write!(
                j,
                "{{\"id\":{},\"name\":{},\"class\":{},\"category\":{},\"line\":{},\"parent\":{},\"slot\":{},\"index\":{},\"error\":{},\"comment\":{},\"pos\":{},",
                esc(&n.id),
                esc(&n.name),
                esc(class_name(n.class)),
                esc(&n.category),
                n.line,
                esc(&n.parent),
                n.slot,
                n.index,
                opt(&n.error),
                opt(&n.comment),
                match n.pos {
                    Some((x, y)) => format!("[{x},{y}]"),
                    None => "null".to_string(),
                }
            );
            match &n.feeds {
                Some((id, pin)) => {
                    let _ = write!(j, "\"feeds\":{{\"node\":{},\"pin\":{}}},", esc(id), esc(pin));
                }
                None => j.push_str("\"feeds\":null,"),
            }
            j.push_str("\"pins\":[");
            for (k, p) in n.pins.iter().enumerate() {
                if k > 0 {
                    j.push(',');
                }
                let choices = p.choices.iter().map(|c| esc(c)).collect::<Vec<_>>().join(",");
                let _ = write!(
                    j,
                    "{{\"name\":{},\"text\":{},\"kind\":{},\"optional\":{},\"editable\":{},\"picks\":{},\"toggle\":{},\"choices\":[{}],\"wired\":{}}}",
                    esc(&p.name),
                    esc(&p.text),
                    esc(p.kind),
                    p.optional,
                    p.editable,
                    p.picks,
                    p.toggle,
                    choices,
                    opt(&p.wired)
                );
            }
            j.push_str("],\"execs\":[");
            for (k, e) in n.execs.iter().enumerate() {
                if k > 0 {
                    j.push(',');
                }
                let _ = write!(j, "{{\"label\":{},\"to\":{}}}", esc(&e.label), opt(&e.to));
            }
            j.push_str("]}");
        }
        j.push_str("],\"components\":[");
        j.push_str(&self.components.iter().map(|c| esc(c)).collect::<Vec<_>>().join(","));
        j.push_str("],\"variables\":[");
        for (i, v) in self.variables.iter().enumerate() {
            if i > 0 {
                j.push(',');
            }
            let _ = write!(
                j,
                "{{\"name\":{},\"text\":{},\"kind\":{},\"type\":{},\"value\":{},\"parts\":{},\"exposed\":{}}}",
                esc(&v.name),
                esc(&v.text),
                esc(v.kind),
                esc(&v.ty),
                esc(&v.value),
                esc(&v.parts),
                v.exposed
            );
        }
        j.push_str("],\"types\":[");
        for (i, t) in self.types.iter().enumerate() {
            if i > 0 {
                j.push(',');
            }
            let values = t.values.iter().map(|v| esc(v)).collect::<Vec<_>>().join(",");
            let fields = t.fields.iter().map(|(n, ty)| format!("{{\"name\":{},\"type\":{}}}", esc(n), esc(ty))).collect::<Vec<_>>().join(",");
            let _ = write!(j, "{{\"name\":{},\"kind\":{},\"values\":[{values}],\"fields\":[{fields}]}}", esc(&t.name), esc(t.kind));
        }
        j.push_str("],\"references\":[");
        for (i, r) in self.references.iter().enumerate() {
            if i > 0 {
                j.push(',');
            }
            let _ = write!(
                j,
                "{{\"name\":{},\"text\":{},\"kind\":{},\"file\":{},\"tag\":{},\"when\":{}}}",
                esc(&r.name),
                esc(&r.text),
                esc(r.kind),
                esc(&r.file),
                esc(&r.tag),
                esc(&r.when)
            );
        }
        j.push_str("],\"problems\":[");
        for (i, p) in self.problems.iter().enumerate() {
            if i > 0 {
                j.push(',');
            }
            let _ = write!(
                j,
                "{{\"severity\":{},\"line\":{},\"message\":{},\"node\":{},\"variable\":{}}}",
                esc(p.severity),
                p.line,
                esc(&p.message),
                opt(&p.node),
                opt(&p.variable)
            );
        }
        j.push_str("],\"errors\":[");
        for (i, (line, message)) in self.errors.iter().enumerate() {
            if i > 0 {
                j.push(',');
            }
            let _ = write!(j, "{{\"line\":{line},\"message\":{}}}", esc(message));
        }
        j.push_str("]}");
        j
    }
}

const CONDITION_NODES: &[&str] = &["Compare", "Input State", "Is Valid", "Contains", "And", "Or"];

/// Every node a graph editor can offer, as JSON: name, class, category, sentence, and for value
/// nodes whether they make a condition or a value.
pub fn catalog_json() -> String {
    let mut j = String::from("[");
    let mut first = true;
    for n in NODES.iter().filter(|n| n.name != "Define Action") {
        if !first {
            j.push(',');
        }
        first = false;
        let produces = if n.class == NodeClass::Data { if CONDITION_NODES.contains(&n.name) { "condition" } else { "value" } } else { "" };
        let _ = write!(
            j,
            "{{\"name\":{},\"class\":{},\"category\":{},\"pattern\":{},\"produces\":{}}}",
            esc(n.name),
            esc(class_name(n.class)),
            esc(n.category),
            esc(n.pattern),
            esc(produces)
        );
    }
    j.push(']');
    j
}
