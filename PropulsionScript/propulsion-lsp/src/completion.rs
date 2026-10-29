//! Completion. The language is a fixed set of sentence patterns, so what can come next is
//! decided by the words already on the line: offer the names in scope, the words that can
//! continue the sentence, and snippets for whole lines.

use crate::hover::{signature, symbol_markdown};
use crate::resolve::{axes_of, resolve_member, Resolved};
use crate::text::index_position;
use crate::workspace::Workspace;
use propulsion::index::{External, ExternalKind, Index, Section, Symbol, SymbolKind, Tok, TokKind};
use lsp_types::{
    CompletionItem, CompletionItemKind, Documentation, InsertTextFormat, MarkupContent, MarkupKind, Position, Url,
};
use std::path::Path;

const TYPES: &[&str] = &["integer", "decimal", "boolean", "text", "vector2", "vector3", "color", "entity", "timer", "gate"];

/// `(label, snippet)` pairs. `$1`, `${2:text}` are tab stops; `$0` is where the cursor ends up.
const DECLARATIONS: &[(&str, &str)] = &[
    ("variable", "variable name:${1:Name} type:${2:integer} value:${3:0}"),
    ("variable exposed", "variable name:${1:Name} type:${2:decimal} value:${3:1.0} exposed:true"),
    ("constant", "constant name:${1:Name} type:${2:decimal} value:${3:1.0}"),
    ("needs component", "needs component:${1:Transform}"),
    ("needs system", "needs system:${1:Input}"),
    ("needs entity", "needs entity:${1:Player} file:\"${2:player.prop}\" tag:\"${3:player}\""),
    ("needs entities", "needs entities:${1:Foes} tag:\"${2:enemy}\""),
    ("needs file", "needs file:\"${1:file.prop}\" as ${2:Name}"),
    ("event", "event name:${1:Name}"),
    ("tag", "tag name:\"${1:name}\""),
];

const HANDLERS: &[(&str, &str)] = &[
    ("every frame", "every frame:\n\t$0"),
    ("every N seconds", "every ${1:2} seconds:\n\t$0"),
    ("when created", "when created:\n\t$0"),
    ("when destroyed", "when destroyed:\n\t$0"),
    ("when Self touches", "when Self touches ${1:File} as ${2:Other}:\n\t$0"),
    ("when Self stops touching", "when Self stops touching ${1:File} as ${2:Other}:\n\t$0"),
    ("when variable changes", "when ${1:Variable} changes:\n\t$0"),
    ("when input starts pressing", "when ${1:Input} starts pressing ${2:jump}:\n\t$0"),
    ("when input stops pressing", "when ${1:Input} stops pressing ${2:jump}:\n\t$0"),
    ("when Self hits", "when Self hits ${1:File} as ${2:Other}:\n\t$0"),
    ("when damaged", "when damaged as ${1:Amount} from ${2:Attacker}:\n\t$0"),
    ("when event", "when ${1:Event}:\n\t$0"),
    ("when file sends event", "when ${1:File} sends ${2:Event} as ${3:Name}:\n\t$0"),
    ("action", "action ${1:Name}:\n\t$0"),
    ("action with parameter", "action ${1:Name} ${2:amount}:${3:integer}:\n\t$0"),
    ("action that gives", "action ${1:Name} gives ${2:integer}:\n\t$0"),
];

const STATEMENTS: &[(&str, &str)] = &[
    ("make", "make ${1:Target} ${2:value}"),
    ("add", "add ${1:value} to ${2:Target}"),
    ("subtract", "subtract ${1:value} from ${2:Target}"),
    ("multiply", "multiply ${1:Target} by ${2:value}"),
    ("divide", "divide ${1:Target} by ${2:value}"),
    ("remove", "remove ${1:value} from ${2:List}"),
    ("do", "do ${1:Action}"),
    ("do ... on", "do ${1:Action} on ${2:Thing}"),
    ("send", "send ${1:Event}"),
    ("remember", "remember ${1:Name} ${2:value}"),
    ("reset", "reset ${1:Timer}"),
    ("spawn", "spawn ${1:File} at:vector x:${2:0} y:${3:0} z:${4:0} as ${5:Name}"),
    ("move toward", "move ${1:Transform.Loc} toward ${2:Target} by ${3:Speed} per second"),
    ("move direction", "move ${1:Self} ${2|forward,back,left,right,up,down|} by ${3:Speed} per second"),
    ("print", "print ${1:value}"),
    ("destroy", "destroy ${1:Self}"),
    ("give back", "give back ${1:value}"),
    ("finish", "finish"),
    ("stop loop", "stop loop"),
    ("skip", "skip"),
    ("if", "if ${1:Name} is ${2:true}:\n\t$0"),
    ("if ... then", "if ${1:Name} is ${2:true}, then ${3:finish}"),
    ("otherwise if", "otherwise if ${1:Name} is ${2:true}:\n\t$0"),
    ("otherwise", "otherwise:\n\t$0"),
    ("repeat", "repeat ${1:3} times:\n\t$0"),
    ("for each", "for each ${1:Item} in ${2:List}:\n\t$0"),
    ("for from to", "for ${1:Index} from ${2:0} to ${3:4}:\n\t$0"),
    ("while", "while ${1:Name} is ${2:true}:\n\t$0"),
    ("after", "after ${1:1} seconds:\n\t$0"),
    ("add unique", "add ${1:value} to ${2:List}, if new"),
    ("remove item", "remove item ${1:0} from ${2:List}"),
    ("clear", "clear ${1:List}"),
    ("only once", "only once:\n\t$0"),
    ("first N times", "first ${1:3} times:\n\t$0"),
    ("alternate", "alternate:\n\t$1\notherwise:\n\t$0"),
    ("open", "open ${1:Gate}"),
    ("close", "close ${1:Gate}"),
    ("toggle", "toggle ${1:Gate}"),
    ("through", "through ${1:Gate}:\n\t$0"),
    ("choose", "choose ${1:Value}:\n\tcase ${2:1}:\n\t\t$3\n\totherwise:\n\t\t$0"),
    ("animate", "animate ${1:Progress} from ${2:0.0} to ${3:1.0} over ${4:1} seconds:\n\t$0"),
    ("damage", "damage ${1:Target} by ${2:1.0}"),
    ("attach", "attach ${1:Self} to ${2:Parent}"),
    ("detach", "detach ${1:Self}"),
    ("tell", "tell ${1:Target} to ${2:Action}"),
];

/// Phrases that can start a value.
const PHRASES: &[(&str, &str)] = &[
    ("distance from", "distance from ${1:A} to ${2:B}"),
    ("count of", "count of ${1:List}"),
    ("item of", "item ${1:0} of ${2:List}"),
    ("text of", "text of ${1:value}"),
    ("decimal of", "decimal of ${1:integer}"),
    ("integer of", "integer of ${1:decimal}"),
    ("absolute value of", "absolute value of ${1:value}"),
    ("result of", "result of ${1:Action}"),
    ("vector", "vector x:${1:0} y:${2:0} z:${3:0}"),
    ("time since start", "time since start"),
    ("axis of", "axis ${1:moveForward} of ${2:Input}"),
    ("of the way from", "${1:0.5} of the way from ${2:A} to ${3:B}"),
    ("pick", "${1:A} if ${2:Name} is ${3:true} otherwise ${4:B}"),
    ("cast to", "cast ${1:Entity} to ${2:File}"),
    ("format", "format \"${1:Score: {Score\\}}\""),
];

/// A phrase from the table in `phrases.rs`, as a snippet: its words, with a tab stop for each pin.
fn phrase_snippet(def: &propulsion::phrases::Phrase) -> String {
    use propulsion::phrases::Piece;
    let mut out = String::new();
    let mut stop = 0;
    let mut glue = false;
    for piece in def.pieces {
        match piece {
            Piece::Word(w) => {
                if !out.is_empty() {
                    out.push(' ');
                }
                out.push_str(w);
            }
            Piece::Key(k) => {
                if !out.is_empty() {
                    out.push(' ');
                }
                out.push_str(k);
                out.push(':');
                glue = true;
            }
            Piece::Arg(name) => {
                if !out.is_empty() && !glue {
                    out.push(' ');
                }
                glue = false;
                stop += 1;
                out.push_str(&format!("${{{stop}:{name}}}"));
            }
        }
    }
    out
}

const ARITHMETIC: &[&str] = &["plus", "minus", "times", "divided by", "remainder after dividing by", "joined with", "per second"];
const COMPARISONS: &[&str] = &["above", "below", "at least", "at most", "not", "missing", "present", "pressing", "true", "false"];
const DIRECTIONS: &[&str] = &["forward", "back", "left", "right", "up", "down"];

struct Items(Vec<CompletionItem>);

impl Items {
    fn word(&mut self, label: &str, kind: CompletionItemKind, sort: &str) {
        self.0.push(CompletionItem {
            label: label.to_string(),
            kind: Some(kind),
            sort_text: Some(format!("{sort}{label}")),
            ..CompletionItem::default()
        });
    }

    fn snippet(&mut self, label: &str, snippet: &str, sort: &str) {
        self.0.push(CompletionItem {
            label: label.to_string(),
            kind: Some(CompletionItemKind::SNIPPET),
            insert_text: Some(snippet.to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            sort_text: Some(format!("{sort}{label}")),
            ..CompletionItem::default()
        });
    }

    fn symbol(&mut self, s: &Symbol, ws: &Workspace) {
        let kind = match s.kind {
            SymbolKind::Constant => CompletionItemKind::CONSTANT,
            SymbolKind::Action => CompletionItemKind::FUNCTION,
            SymbolKind::Event => CompletionItemKind::EVENT,
            SymbolKind::File | SymbolKind::System => CompletionItemKind::MODULE,
            SymbolKind::Component => CompletionItemKind::CLASS,
            SymbolKind::Entity | SymbolKind::Entities => CompletionItemKind::INTERFACE,
            _ => CompletionItemKind::VARIABLE,
        };
        let detail = match (&s.ty, s.kind) {
            (Some(t), _) => format!("{}: {t}", s.kind.label()),
            (None, SymbolKind::Action) => signature(&s.name, &s.params, s.gives.as_deref()),
            _ => s.kind.label().to_string(),
        };
        self.0.push(CompletionItem {
            label: s.name.clone(),
            kind: Some(kind),
            detail: Some(detail),
            documentation: Some(Documentation::MarkupContent(MarkupContent {
                kind: MarkupKind::Markdown,
                value: symbol_markdown(s, &ws.manifest),
            })),
            sort_text: Some(format!("1{}", s.name)),
            ..CompletionItem::default()
        });
    }
}

/// The result of working out the context: items, and whether the context was specific enough to
/// show them when the editor only asked because a space was typed.
pub struct Completions {
    pub items: Vec<CompletionItem>,
    pub specific: bool,
}

pub fn complete(ws: &Workspace, uri: &Url, position: Position) -> Result<Completions, String> {
    let (path, index) = ws.index_of(uri)?;
    let (line, col) = index_position(index.line_text(position.line as usize + 1), position);
    Ok(complete_at(ws, &path, &index, line, col))
}

pub fn complete_at(ws: &Workspace, path: &Path, index: &Index, line: usize, col: usize) -> Completions {
    let toks = index.line_tokens(line);
    let nothing = Completions { items: Vec::new(), specific: true };

    // Inside a quoted text, or in a trailing comment, there is nothing to complete.
    if in_text(toks, col) || comment_start(index.line_text(line)).is_some_and(|at| at < col - 1) {
        return nothing;
    }

    // The word being typed is not context; the tokens before it are.
    let typing = toks.iter().position(|t| t.kind == TokKind::Word && t.span.col < col && col <= t.span.col + t.span.len);
    let before: Vec<&Tok> = match typing {
        Some(i) => toks[..i].iter().collect(),
        None => toks.iter().filter(|t| ends_before(t, col)).collect(),
    };

    let section = index.section_at(line);
    let mut out = Items(Vec::new());

    // 1. The start of a line.
    if before.is_empty() {
        let indent = index.indent(line);
        match (section, indent) {
            (_, 0) => {
                out.snippet("header:", "header:\n\t$0", "2");
                out.snippet("implement:", "implement:\n\t$0", "2");
            }
            (Section::Header, 1) => DECLARATIONS.iter().for_each(|(l, s)| out.snippet(l, s, "2")),
            (Section::Header, _) => out.snippet("entry", "entry x:${1:0} y:${2:0} z:${3:0}", "2"),
            (Section::Implement, 1) => HANDLERS.iter().for_each(|(l, s)| out.snippet(l, s, "2")),
            (Section::Implement, _) => STATEMENTS.iter().for_each(|(l, s)| out.snippet(l, s, "2")),
            (Section::None, _) => {}
        }
        return Completions { items: out.0, specific: true };
    }

    let first = before[0].text.as_str();
    let prev = *before.last().unwrap();
    let prev2 = before.len().checked_sub(2).map(|i| before[i]);
    let word_before = |w: &str| before.iter().any(|t| t.kind == TokKind::Word && t.text == w);
    let specific;

    if prev.kind == TokKind::Dot {
        specific = true;
        field_items(ws, index, &before, &mut out);
    } else if prev.kind == TokKind::Colon && prev2.is_some_and(|t| t.kind == TokKind::Word) {
        specific = true;
        let key = prev2.unwrap().text.as_str();
        match key {
            "type" => type_items(&mut out),
            // A parameter's type: `action Hit amount:|`. (The colon that ends the line is not one.)
            _ if first == "action" && before.get(1).is_some_and(|name| name.text != key) && !TYPES.contains(&key) => type_items(&mut out),
            "component" => {
                for name in ws.manifest.component_names() {
                    out.word(name, CompletionItemKind::CLASS, "1");
                }
            }
            "system" => {
                for name in ws.manifest.system_names() {
                    out.word(name, CompletionItemKind::MODULE, "1");
                }
            }
            "when" if first == "needs" => ["used", "startup"].iter().for_each(|w| out.word(w, CompletionItemKind::ENUM_MEMBER, "1")),
            "exposed" => ["true", "false"].iter().for_each(|w| out.word(w, CompletionItemKind::KEYWORD, "1")),
            "name" | "file" | "tag" | "entity" | "entities" | "value" | "as" | "x" | "y" | "z" | "r" | "g" | "b" | "a" if matches!(first, "variable" | "constant" | "needs" | "event" | "tag" | "entry") => {
                // Names and literals in a declaration are written, not chosen.
                return nothing;
            }
            _ if matches!(first, "when" | "every" | "if" | "otherwise" | "for" | "while" | "repeat" | "after" | "action") && is_line_end(toks, col) => return nothing,
            _ => value_items(ws, index, line, &mut out),
        }
    } else if prev.kind == TokKind::Word && prev.text == "on" && word_before("do") {
        specific = true;
        for (_, s) in index.visible_at(line) {
            if matches!(s.kind, SymbolKind::File | SymbolKind::System | SymbolKind::Entity | SymbolKind::Local) && s.decl.line != line {
                out.symbol(s, ws);
            }
        }
    } else if let Some(args) = do_arguments(ws, path, index, line, &before) {
        // `do ...`: the action name, `on`, and then the arguments that are still missing.
        specific = true;
        match args {
            DoStage::Action => {
                for (_, s) in index.visible_at(line).into_iter().filter(|(_, s)| s.kind == SymbolKind::Action) {
                    out.symbol(s, ws);
                }
            }
            DoStage::Arguments { params, can_say_on, given } => {
                if can_say_on {
                    out.word("on", CompletionItemKind::KEYWORD, "0");
                }
                for (name, ty) in params.iter().filter(|(n, _)| !given.contains(n)) {
                    out.0.push(CompletionItem {
                        label: format!("{name}:"),
                        kind: Some(CompletionItemKind::PROPERTY),
                        detail: Some(ty.clone()),
                        insert_text: Some(format!("{name}:")),
                        sort_text: Some(format!("0{name}")),
                        ..CompletionItem::default()
                    });
                }
            }
        }
    } else {
        match prev.text.as_str() {
            "remember" | "as" | "for" | "each" if prev.kind == TokKind::Word => return nothing,
            "gives" => {
                specific = true;
                type_items(&mut out);
            }
            "of" if prev2.is_some_and(|t| t.text == "list") => {
                specific = true;
                type_items(&mut out);
            }
            "otherwise" => {
                specific = true;
                out.word("if", CompletionItemKind::KEYWORD, "0");
            }
            "give" => {
                specific = true;
                out.word("back", CompletionItemKind::KEYWORD, "0");
            }
            "send" => {
                specific = true;
                symbols_of(index, line, &[SymbolKind::Event], ws, &mut out);
            }
            "reset" => {
                specific = true;
                for (_, s) in index.visible_at(line) {
                    if s.kind == SymbolKind::Variable && s.ty.as_deref() == Some("timer") {
                        out.symbol(s, ws);
                    }
                }
            }
            "spawn" => {
                specific = true;
                symbols_of(index, line, &[SymbolKind::File], ws, &mut out);
            }
            "destroy" => {
                specific = true;
                out.word("Self", CompletionItemKind::KEYWORD, "0");
                symbols_of(index, line, &[SymbolKind::Entity, SymbolKind::Local, SymbolKind::Variable], ws, &mut out);
            }
            "when" if first == "when" => {
                specific = true;
                for w in ["created", "destroyed", "Self touches", "Self stops touching"] {
                    out.word(w, CompletionItemKind::KEYWORD, "0");
                }
                symbols_of(index, line, &[SymbolKind::Event, SymbolKind::Variable, SymbolKind::File, SymbolKind::System], ws, &mut out);
            }
            "every" => {
                specific = true;
                out.word("frame", CompletionItemKind::KEYWORD, "0");
            }
            "pressing" => {
                specific = true;
                input_items(ws, index, line, false, &mut out);
            }
            "axis" => {
                specific = true;
                input_items(ws, index, line, true, &mut out);
            }
            "is" => {
                specific = true;
                COMPARISONS.iter().for_each(|w| out.word(w, CompletionItemKind::KEYWORD, "0"));
                value_items(ws, index, line, &mut out);
            }
            "move" if first == "move" => {
                specific = true;
                value_items(ws, index, line, &mut out);
            }
            // Words that are followed by a value.
            "make" | "add" | "subtract" | "multiply" | "divide" | "remove" | "print" | "then" | "if" | "while" | "and" | "or"
            | "to" | "from" | "by" | "of" | "in" | "at" | "toward" | "plus" | "minus" | "times" | "with" | "back" | "after"
            | "repeat" | "above" | "below" | "least" | "most"
                if prev.kind == TokKind::Word =>
            {
                specific = false;
                value_items(ws, index, line, &mut out);
            }
            _ if matches!(prev.kind, TokKind::Word | TokKind::Number | TokKind::Text) => {
                // After a value: what can come next in this kind of sentence.
                specific = true;
                after_value(first, &before, &mut out);
            }
            _ => {
                specific = false;
                value_items(ws, index, line, &mut out);
            }
        }
    }
    Completions { items: out.0, specific }
}

/// Whether a token is entirely before the cursor. A text token's span is the part inside the
/// quotes, so the closing quote is one more column.
fn ends_before(t: &Tok, col: usize) -> bool {
    let end = t.span.col + t.span.len;
    if t.kind == TokKind::Text {
        end < col
    } else {
        end <= col
    }
}

/// The character index where a `//` comment starts, ignoring `//` inside a quoted text.
fn comment_start(text: &str) -> Option<usize> {
    let chars: Vec<char> = text.chars().collect();
    let mut in_quotes = false;
    for (i, c) in chars.iter().enumerate() {
        match c {
            '"' => in_quotes = !in_quotes,
            '/' if !in_quotes && chars.get(i + 1) == Some(&'/') => return Some(i),
            _ => {}
        }
    }
    None
}

fn in_text(toks: &[Tok], col: usize) -> bool {
    // A text token's span is the part inside the quotes; the cursor is inside if it is between them.
    toks.iter().any(|t| t.kind == TokKind::Text && t.span.col <= col && col <= t.span.col + t.span.len)
}

fn is_line_end(toks: &[Tok], col: usize) -> bool {
    toks.iter().all(|t| t.span.col + t.span.len <= col)
}

fn type_items(out: &mut Items) {
    for t in TYPES {
        out.word(t, CompletionItemKind::TYPE_PARAMETER, "1");
    }
    out.snippet("list of", "list of ${1:integer}", "1");
}

fn symbols_of(index: &Index, line: usize, kinds: &[SymbolKind], ws: &Workspace, out: &mut Items) {
    for (_, s) in index.visible_at(line) {
        if kinds.contains(&s.kind) && s.decl.line != line {
            out.symbol(s, ws);
        }
    }
}

/// Everything that can stand for a value: the names in scope, `Self`, the literals, and the phrases.
fn value_items(ws: &Workspace, index: &Index, line: usize, out: &mut Items) {
    for (_, s) in index.visible_at(line) {
        let usable = matches!(
            s.kind,
            SymbolKind::Variable
                | SymbolKind::Constant
                | SymbolKind::Parameter
                | SymbolKind::Local
                | SymbolKind::Loop
                | SymbolKind::Entity
                | SymbolKind::Entities
                | SymbolKind::Component
                | SymbolKind::System
        );
        if usable && s.decl.line != line {
            out.symbol(s, ws);
        }
    }
    out.word("Self", CompletionItemKind::KEYWORD, "1");
    out.word("true", CompletionItemKind::KEYWORD, "3");
    out.word("false", CompletionItemKind::KEYWORD, "3");
    for (label, snippet) in PHRASES {
        out.snippet(label, snippet, "3");
    }
    for def in propulsion::phrases::PHRASES {
        out.snippet(&def.pieces.iter().filter_map(|p| if let propulsion::phrases::Piece::Word(w) = p { Some(*w) } else { None }).collect::<Vec<_>>().join(" "), &phrase_snippet(def), "3");
    }
}

/// Words that can follow a value, depending on the statement.
fn after_value(first: &str, before: &[&Tok], out: &mut Items) {
    let said = |w: &str| before.iter().any(|t| t.kind == TokKind::Word && t.text == w);
    let mut next: Vec<&str> = Vec::new();
    match first {
        "add" if !said("to") => next.push("to"),
        "subtract" | "remove" if !said("from") => next.push("from"),
        "multiply" | "divide" if !said("by") => next.push("by"),
        "move" => {
            if !said("toward") && !DIRECTIONS.iter().any(|d| said(d)) {
                next.push("toward");
                next.extend(DIRECTIONS);
            } else if !said("by") {
                next.push("by");
            }
        }
        "for" if !said("from") && !said("in") => next.extend(["from", "in"]),
        "repeat" if !said("times") => next.push("times"),
        "after" if !said("seconds") => next.push("seconds"),
        "if" | "while" | "otherwise" => {
            if !said("is") {
                next.push("is");
            }
            next.extend(["and", "or"]);
            if said("is") && !said("then") {
                next.push(",");
            }
        }
        "make" | "remember" | "print" | "give" => {}
        _ => {}
    }
    for w in &next {
        if *w == "," {
            out.snippet(", then", ", then ", "0");
        } else {
            out.word(w, CompletionItemKind::KEYWORD, "0");
        }
    }
    if matches!(first, "make" | "add" | "subtract" | "remember" | "print" | "give" | "move" | "if" | "while" | "otherwise") {
        for w in ARITHMETIC {
            out.word(w, CompletionItemKind::OPERATOR, "2");
        }
    }
    if matches!(first, "add" | "subtract" | "make") {
        for w in ["capped at", "floored at"] {
            out.word(w, CompletionItemKind::OPERATOR, "2");
        }
    }
}

enum DoStage {
    /// `do |`: the name of an action.
    Action,
    /// `do Name |` or `do Name on Thing |`.
    Arguments { params: Vec<(String, String)>, can_say_on: bool, given: Vec<String> },
}

/// If the line is a `do` statement, what is expected next.
fn do_arguments(ws: &Workspace, path: &Path, index: &Index, line: usize, before: &[&Tok]) -> Option<DoStage> {
    let at = before.iter().position(|t| t.kind == TokKind::Word && t.text == "do")?;
    let rest = &before[at + 1..];
    let action = match rest.first() {
        None => return Some(DoStage::Action),
        Some(t) if t.kind == TokKind::Word => *t,
        Some(_) => return None,
    };
    let on = rest.get(1).is_some_and(|t| t.text == "on");
    let owner = if on { rest.get(2).filter(|t| t.kind == TokKind::Word) } else { None };
    let given: Vec<String> = rest
        .windows(2)
        .filter(|w| w[0].kind == TokKind::Word && w[1].kind == TokKind::Colon)
        .map(|w| w[0].text.clone())
        .collect();

    let params: Vec<(String, String)> = match (on, owner) {
        (false, _) => {
            let id = index.action(&action.text)?;
            index.symbol(id).params.iter().map(|p| (p.name.clone(), p.ty.clone())).collect()
        }
        (true, Some(owner)) => {
            let owner_id = index.visible_at(line).into_iter().find(|(_, s)| s.name == owner.text).map(|(id, _)| id)?;
            let ext = External { span: action.span, owner: owner_id, member: action.text.clone(), kind: ExternalKind::Action };
            match resolve_member(ws, path, index, &ext)? {
                Resolved::Remote(r) => r.index.symbol(r.symbol).params.iter().map(|p| (p.name.clone(), p.ty.clone())).collect(),
                Resolved::Engine(a) => a.params.iter().map(|(n, t)| (n.clone(), t.to_string())).collect(),
            }
        }
        (true, None) => return None,
    };
    Some(DoStage::Arguments { params, can_say_on: !on, given })
}

/// `Transform.` offers the component's fields; `Transform.Loc.` offers its axes.
fn field_items(ws: &Workspace, index: &Index, before: &[&Tok], out: &mut Items) {
    // Walk back over `Name . Name . ` to find the path so far.
    let mut parts: Vec<&str> = Vec::new();
    let mut i = before.len();
    while i >= 2 && before[i - 1].kind == TokKind::Dot && before[i - 2].kind == TokKind::Word {
        parts.push(before[i - 2].text.as_str());
        i -= 2;
    }
    parts.reverse();
    let Some(component) = parts.first() else { return };
    if !index.symbols.iter().any(|s| s.name == *component && s.kind == SymbolKind::Component) {
        return;
    }
    let Some(fields) = ws.manifest.components.get(*component) else { return };
    match parts.len() {
        1 => {
            for (name, ty) in fields {
                out.0.push(CompletionItem {
                    label: name.clone(),
                    kind: Some(CompletionItemKind::FIELD),
                    detail: Some(ty.to_string()),
                    sort_text: Some(format!("1{name}")),
                    ..CompletionItem::default()
                });
            }
        }
        2 => {
            if let Some((_, ty)) = fields.iter().find(|(n, _)| n == parts[1]) {
                for axis in axes_of(ty) {
                    out.word(axis, CompletionItemKind::FIELD, "1");
                }
            }
        }
        _ => {}
    }
}

/// Input names (`jump`) or axes (`moveForward`) of the systems the file needs.
fn input_items(ws: &Workspace, index: &Index, line: usize, axes: bool, out: &mut Items) {
    for (_, s) in index.visible_at(line) {
        if s.kind != SymbolKind::System {
            continue;
        }
        if let Some(def) = ws.manifest.systems.get(&s.name) {
            for n in if axes { &def.axes } else { &def.inputs } {
                out.word(n, CompletionItemKind::ENUM_MEMBER, "1");
            }
        }
    }
}
