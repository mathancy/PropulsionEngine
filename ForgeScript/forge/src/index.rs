//! A symbol index for editors: where every name is declared, and where it is used.
//!
//! The index is built from the text alone, one line at a time, so it keeps working on a file that
//! is half-typed (where `parse` stops at the first error). It leans on two facts about the
//! language: there is one statement per line, and a name's scope follows from indentation. A test
//! compares it with the real parser on every example, so the two can't drift apart.

use std::collections::HashSet;

/// Words that can't be used as names. A rename refuses them.
pub const KEYWORDS: &[&str] = &[
    "header", "implement", "variable", "constant", "needs", "event", "tag", "entry", "action", "gives", "when", "every",
    "after", "if", "otherwise", "then", "while", "repeat", "for", "each", "in", "from", "to", "by", "at", "as", "on", "of",
    "make", "add", "subtract", "multiply", "divide", "remove", "do", "send", "remember", "reset", "spawn", "move", "print",
    "destroy", "give", "back", "finish", "stop", "skip", "loop", "is", "not", "and", "or", "plus", "minus", "times",
    "true", "false", "Self", "missing", "present", "integer", "decimal", "boolean", "text", "vector2", "vector3", "color",
    "entity", "timer", "list", "gate", "clear", "only", "once", "first", "times", "alternate", "through", "choose", "case",
    "animate", "over", "damage", "damaged", "hits", "attach", "detach", "tell", "contains", "contain", "does", "open", "close",
    "toggle", "new", "item", "cast", "format", "between", "into", "looking", "product", "position", "ease", "turn", "level",
    "name", "random", "normalized", "length", "dot", "rotation", "forward", "right", "up", "map", "clamp",
];

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum SymbolKind {
    Variable,
    Constant,
    /// An action's parameter.
    Parameter,
    /// A `remember`ed value, or the entity named by `as` in a handler or `spawn`.
    Local,
    /// The name in `for each Foe in ...` / `for Index from ...`.
    Loop,
    /// `needs entity:Player ...`
    Entity,
    /// `needs entities:Foes ...`
    Entities,
    /// `needs file:"..." as Hud`
    File,
    System,
    Component,
    Event,
    Action,
    /// The name in `only once as Greeting:`, which `reset Greeting` starts over.
    Latch,
}

impl SymbolKind {
    /// The word to show for this kind of name.
    pub fn label(self) -> &'static str {
        match self {
            SymbolKind::Variable => "variable",
            SymbolKind::Constant => "constant",
            SymbolKind::Parameter => "parameter",
            SymbolKind::Local => "local",
            SymbolKind::Loop => "loop variable",
            SymbolKind::Entity => "entity",
            SymbolKind::Entities => "entities",
            SymbolKind::File => "file",
            SymbolKind::System => "system",
            SymbolKind::Component => "component",
            SymbolKind::Event => "event",
            SymbolKind::Action => "action",
            SymbolKind::Latch => "once",
        }
    }
}

/// A stretch of one line. `line` and `col` are 1-based, and `col` and `len` count characters
/// (the same as `ForgeError`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Span {
    pub line: usize,
    pub col: usize,
    pub len: usize,
}

impl Span {
    /// Whether a cursor at `(line, col)` is on this span. A cursor just after the last character counts.
    pub fn contains(&self, line: usize, col: usize) -> bool {
        self.line == line && col >= self.col && col <= self.col + self.len
    }
}

#[derive(Debug, Clone)]
pub struct Param {
    pub name: String,
    pub ty: String,
}

#[derive(Debug, Clone)]
pub struct Symbol {
    pub name: String,
    pub kind: SymbolKind,
    pub decl: Span,
    /// The first and last line (1-based, inclusive) where the name can be used.
    pub scope: (usize, usize),
    /// The type as written, like `integer` or `list of vector3`.
    pub ty: Option<String>,
    /// The starting value as written (`value:90.0`).
    pub value: Option<String>,
    pub exposed: bool,
    /// For files and entities, the path in `file:"..."`. For a name made by `as`, the file it stands for.
    pub file: Option<String>,
    pub tag: Option<String>,
    /// For an action: its parameters and what it gives.
    pub params: Vec<Param>,
    pub gives: Option<String>,
    /// Comments directly above the declaration, and one at the end of its line.
    pub doc: Option<String>,
}

impl Symbol {
    fn new(name: &str, kind: SymbolKind, decl: Span, scope: (usize, usize)) -> Symbol {
        Symbol {
            name: name.to_string(),
            kind,
            decl,
            scope,
            ty: None,
            value: None,
            exposed: false,
            file: None,
            tag: None,
            params: Vec::new(),
            gives: None,
            doc: None,
        }
    }
}

/// A place a symbol's name is written. The declaration itself is one.
#[derive(Debug, Clone, Copy)]
pub struct Occurrence {
    pub span: Span,
    pub symbol: usize,
    pub declaration: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ExternalKind {
    Action,
    Event,
}

/// A name that belongs to something else: the `TakeDamage` in `do TakeDamage on Player`, which is
/// an action of another file or of an engine system. Resolving it needs the other file.
#[derive(Debug, Clone)]
pub struct External {
    pub span: Span,
    /// The symbol it is called on (`Player`, `Audio`, a file name).
    pub owner: usize,
    pub member: String,
    pub kind: ExternalKind,
}

/// A field path like `Transform.Loc.X`. Each part has its own span.
#[derive(Debug, Clone)]
pub struct PathRef {
    pub segments: Vec<(String, Span)>,
}

/// The text of a `file:"..."` pair. `span` covers the text inside the quotes.
#[derive(Debug, Clone)]
pub struct FileRef {
    pub span: Span,
    pub path: String,
}

/// A handler (`every frame:`, `when created:`, ...) and the lines it covers.
#[derive(Debug, Clone)]
pub struct Handler {
    pub text: String,
    pub line: usize,
    pub end: usize,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Section {
    None,
    Header,
    Implement,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TokKind {
    Word,
    Number,
    Text,
    Colon,
    Comma,
    Dot,
    Other,
}

/// One piece of a line. For `Text`, `text` and `span` are the part inside the quotes.
#[derive(Debug, Clone)]
pub struct Tok {
    pub kind: TokKind,
    pub text: String,
    pub span: Span,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum LineKind {
    Blank,
    Comment,
    Code,
}

#[derive(Debug, Clone)]
struct Line {
    raw: String,
    indent: usize,
    kind: LineKind,
    toks: Vec<Tok>,
    trailing: Option<String>,
}

#[derive(Debug, Clone, Default)]
pub struct Index {
    pub symbols: Vec<Symbol>,
    pub occurrences: Vec<Occurrence>,
    pub externals: Vec<External>,
    pub paths: Vec<PathRef>,
    pub file_refs: Vec<FileRef>,
    pub handlers: Vec<Handler>,
    lines: Vec<Line>,
}

impl Index {
    pub fn build(src: &str) -> Index {
        let lines: Vec<Line> = src.lines().enumerate().map(|(i, raw)| scan_line(raw, i + 1)).collect();
        let mut index = Index { lines, ..Index::default() };
        index.declare();
        index.resolve();
        index
    }

    // ---- queries ----

    pub fn symbol(&self, id: usize) -> &Symbol {
        &self.symbols[id]
    }

    pub fn occurrence_at(&self, line: usize, col: usize) -> Option<&Occurrence> {
        self.occurrences.iter().find(|o| o.span.contains(line, col))
    }

    pub fn external_at(&self, line: usize, col: usize) -> Option<&External> {
        self.externals.iter().find(|e| e.span.contains(line, col))
    }

    /// The field path under the cursor, and which part of it.
    pub fn path_at(&self, line: usize, col: usize) -> Option<(&PathRef, usize)> {
        self.paths.iter().find_map(|p| p.segments.iter().position(|(_, s)| s.contains(line, col)).map(|i| (p, i)))
    }

    pub fn file_ref_at(&self, line: usize, col: usize) -> Option<&FileRef> {
        self.file_refs.iter().find(|f| f.span.contains(line, col))
    }

    pub fn references(&self, symbol: usize) -> impl Iterator<Item = &Occurrence> {
        self.occurrences.iter().filter(move |o| o.symbol == symbol)
    }

    /// Every name that can be used on `line`.
    pub fn visible_at(&self, line: usize) -> Vec<(usize, &Symbol)> {
        self.symbols.iter().enumerate().filter(|(_, s)| s.scope.0 <= line && line <= s.scope.1).collect()
    }

    /// A name usable anywhere in the file: declared in the header, or an action.
    pub fn global(&self, name: &str) -> Option<usize> {
        self.symbols.iter().position(|s| s.name == name && s.scope.0 == 1 && !is_local(s.kind))
    }

    pub fn action(&self, name: &str) -> Option<usize> {
        self.symbols.iter().position(|s| s.name == name && s.kind == SymbolKind::Action)
    }

    pub fn line_count(&self) -> usize {
        self.lines.len()
    }

    pub fn line_text(&self, line: usize) -> &str {
        self.lines.get(line.wrapping_sub(1)).map_or("", |l| l.raw.as_str())
    }

    pub fn line_tokens(&self, line: usize) -> &[Tok] {
        self.lines.get(line.wrapping_sub(1)).map_or(&[], |l| l.toks.as_slice())
    }

    pub fn indent(&self, line: usize) -> usize {
        self.lines.get(line.wrapping_sub(1)).map_or(0, |l| l.indent)
    }

    /// Which section a line is in: after `header:`, after `implement:`, or neither.
    pub fn section_at(&self, line: usize) -> Section {
        let mut section = Section::None;
        for l in self.lines.iter().take(line) {
            if l.kind == LineKind::Code && l.indent == 0 {
                section = match l.toks.first().map(|t| t.text.as_str()) {
                    Some("header") => Section::Header,
                    Some("implement") => Section::Implement,
                    _ => Section::None,
                };
            }
        }
        section
    }

    /// The last line a block that starts at `line` covers: everything after it that is indented deeper.
    pub fn block_end(&self, line: usize) -> usize {
        end_before_indent(&self.lines, line - 1, self.lines[line - 1].indent)
    }

    // ---- building ----

    fn declare(&mut self) {
        let lines = std::mem::take(&mut self.lines);
        let mut section = Section::None;
        for i in 0..lines.len() {
            let line = &lines[i];
            if line.kind != LineKind::Code {
                continue;
            }
            if line.indent == 0 {
                section = match line.toks.first().map(|t| t.text.as_str()) {
                    Some("header") => Section::Header,
                    Some("implement") => Section::Implement,
                    _ => Section::None,
                };
                continue;
            }
            match section {
                Section::Header if line.indent == 1 => self.declare_header(&lines, i),
                Section::Implement if line.indent == 1 => self.declare_item(&lines, i),
                Section::Implement => self.declare_statement(&lines, i),
                _ => {}
            }
        }
        self.lines = lines;
    }

    fn add(&mut self, mut symbol: Symbol, lines: &[Line], line_index: usize) -> usize {
        symbol.doc = doc_for(lines, line_index);
        self.occurrences.push(Occurrence { span: symbol.decl, symbol: self.symbols.len(), declaration: true });
        self.symbols.push(symbol);
        self.symbols.len() - 1
    }

    fn whole_file(&self, lines: &[Line]) -> (usize, usize) {
        (1, lines.len().max(1))
    }

    fn declare_header(&mut self, lines: &[Line], i: usize) {
        let toks = &lines[i].toks;
        let Some(kw) = toks.first().filter(|t| t.kind == TokKind::Word) else { return };
        let pairs = key_values(toks);
        let scope = self.whole_file(lines);
        let value_tok = |key: &str| pairs.iter().find(|(k, _)| *k == key).and_then(|(_, at)| toks.get(*at));
        let name_of = |key: &str| value_tok(key).filter(|t| t.kind == TokKind::Word);
        let text_of = |key: &str| value_tok(key).filter(|t| t.kind == TokKind::Text);

        for (key, at) in &pairs {
            if *key == "file" {
                if let Some(t) = toks.get(*at).filter(|t| t.kind == TokKind::Text) {
                    self.file_refs.push(FileRef { span: t.span, path: t.text.clone() });
                }
            }
        }

        match kw.text.as_str() {
            "variable" | "constant" => {
                let Some(name) = name_of("name") else { return };
                let kind = if kw.text == "variable" { SymbolKind::Variable } else { SymbolKind::Constant };
                let mut s = Symbol::new(&name.text, kind, name.span, scope);
                s.ty = pairs.iter().find(|(k, _)| *k == "type").map(|(_, at)| type_text(toks, *at));
                s.value = value_tok("value").map(|t| match t.kind {
                    TokKind::Text => format!("\"{}\"", t.text),
                    _ => t.text.clone(),
                });
                s.exposed = value_tok("exposed").is_some_and(|t| t.text == "true");
                self.add(s, lines, i);
            }
            "event" => {
                if let Some(name) = name_of("name") {
                    self.add(Symbol::new(&name.text, SymbolKind::Event, name.span, scope), lines, i);
                }
            }
            "needs" => {
                let Some((first, _)) = pairs.first() else { return };
                match *first {
                    "component" | "system" => {
                        if let Some(name) = name_of(first) {
                            let kind = if *first == "component" { SymbolKind::Component } else { SymbolKind::System };
                            self.add(Symbol::new(&name.text, kind, name.span, scope), lines, i);
                        }
                    }
                    "entity" | "entities" => {
                        if let Some(name) = name_of(first) {
                            let kind = if *first == "entity" { SymbolKind::Entity } else { SymbolKind::Entities };
                            let mut s = Symbol::new(&name.text, kind, name.span, scope);
                            s.file = text_of("file").map(|t| t.text.clone());
                            s.tag = text_of("tag").map(|t| t.text.clone());
                            if kind == SymbolKind::Entities {
                                s.ty = Some("list of entity".to_string());
                            }
                            self.add(s, lines, i);
                        }
                    }
                    "file" => {
                        // `needs file:"coin.forge" as Coin`: the name comes after a bare `as`.
                        let alias = toks.iter().enumerate().find_map(|(k, t)| {
                            let bare_as = t.kind == TokKind::Word
                                && t.text == "as"
                                && toks.get(k + 1).is_none_or(|n| n.kind != TokKind::Colon);
                            bare_as.then(|| toks.get(k + 1)).flatten().filter(|n| n.kind == TokKind::Word)
                        });
                        if let Some(alias) = alias {
                            let mut s = Symbol::new(&alias.text, SymbolKind::File, alias.span, scope);
                            s.file = text_of("file").map(|t| t.text.clone());
                            self.add(s, lines, i);
                        }
                    }
                    _ => {}
                }
            }
            _ => {}
        }
    }

    fn declare_item(&mut self, lines: &[Line], i: usize) {
        let line = &lines[i];
        let toks = &line.toks;
        let ln = i + 1;
        let end = end_before_indent(lines, i, line.indent);
        let Some(kw) = toks.first().filter(|t| t.kind == TokKind::Word) else { return };

        match kw.text.as_str() {
            "action" => {
                let Some(name) = toks.get(1).filter(|t| t.kind == TokKind::Word) else { return };
                let mut action = Symbol::new(&name.text, SymbolKind::Action, name.span, self.whole_file(lines));
                let mut params: Vec<(Param, Span)> = Vec::new();
                let mut j = 2;
                while let Some(t) = toks.get(j) {
                    if t.kind != TokKind::Word {
                        break;
                    }
                    if t.text == "gives" && !is_key(toks, j) {
                        action.gives = Some(type_text(toks, j + 1));
                        j += 1 + type_len(toks, j + 1);
                    } else if is_key(toks, j) {
                        let ty = type_text(toks, j + 2);
                        params.push((Param { name: t.text.clone(), ty }, t.span));
                        j += 2 + type_len(toks, j + 2);
                    } else {
                        break;
                    }
                }
                action.params = params.iter().map(|(p, _)| p.clone()).collect();
                self.add(action, lines, i);
                for (p, span) in params {
                    let mut s = Symbol::new(&p.name, SymbolKind::Parameter, span, (ln, end));
                    s.ty = Some(p.ty);
                    self.add(s, lines, i);
                }
                self.handlers.push(Handler { text: header_text(line), line: ln, end });
            }
            "when" | "every" => {
                // `when ... as Name:` names the entity involved.
                let alias = toks.iter().enumerate().find_map(|(k, t)| {
                    let bare_as = t.kind == TokKind::Word && t.text == "as";
                    bare_as.then(|| toks.get(k + 1).map(|n| (k, n))).flatten().filter(|(_, n)| n.kind == TokKind::Word)
                });
                if let Some((k, name)) = alias {
                    let damaged = toks.get(1).is_some_and(|t| t.text == "damaged");
                    let mut s = Symbol::new(&name.text, SymbolKind::Local, name.span, (ln, end));
                    s.ty = Some(if damaged { "decimal" } else { "entity" }.to_string());
                    s.file = self.file_of_words(&toks[..k]);
                    self.add(s, lines, i);
                    // `when damaged as Amount from Attacker:` names the one who did it too.
                    if damaged {
                        let from = toks.iter().enumerate().find_map(|(j, t)| {
                            (t.kind == TokKind::Word && t.text == "from").then(|| toks.get(j + 1)).flatten().filter(|n| n.kind == TokKind::Word)
                        });
                        if let Some(source) = from {
                            let mut s = Symbol::new(&source.text, SymbolKind::Local, source.span, (ln, end));
                            s.ty = Some("entity".to_string());
                            self.add(s, lines, i);
                        }
                    }
                }
                self.handlers.push(Handler { text: header_text(line), line: ln, end });
            }
            _ => {}
        }
    }

    fn declare_statement(&mut self, lines: &[Line], i: usize) {
        let line = &lines[i];
        let toks = &line.toks;
        let ln = i + 1;
        let Some(kw) = toks.first().filter(|t| t.kind == TokKind::Word) else { return };
        // A name made by `remember` or `spawn` lasts until the end of the block it is in.
        let to_block_end = end_before_indent(lines, i, line.indent.saturating_sub(1));
        let word_at = |k: usize| toks.get(k).filter(|t| t.kind == TokKind::Word);

        match kw.text.as_str() {
            "remember" => {
                if let Some(name) = word_at(1) {
                    self.add(Symbol::new(&name.text, SymbolKind::Local, name.span, (ln, to_block_end)), lines, i);
                }
            }
            "for" => {
                let each = word_at(1).is_some_and(|t| t.text == "each");
                let name = if each { word_at(2) } else { word_at(1).filter(|_| word_at(2).is_some_and(|t| t.text == "from")) };
                if let Some(name) = name {
                    let end = end_before_indent(lines, i, line.indent);
                    let mut s = Symbol::new(&name.text, SymbolKind::Loop, name.span, (ln, end));
                    if !each {
                        s.ty = Some("integer".to_string());
                    }
                    self.add(s, lines, i);
                }
            }
            "animate" => {
                if let Some(name) = word_at(1) {
                    let end = end_before_indent(lines, i, line.indent);
                    self.add(Symbol::new(&name.text, SymbolKind::Loop, name.span, (ln, end)), lines, i);
                }
            }
            "only" | "first" | "alternate" => {
                let name = toks.iter().enumerate().find_map(|(k, t)| {
                    let bare_as = t.kind == TokKind::Word && t.text == "as";
                    bare_as.then(|| word_at(k + 1)).flatten()
                });
                if let Some(name) = name {
                    // A name for the statement, usable anywhere in the file (`reset Greeting`).
                    self.add(Symbol::new(&name.text, SymbolKind::Latch, name.span, self.whole_file(lines)), lines, i);
                }
            }
            "spawn" => {
                let alias = toks.iter().enumerate().find_map(|(k, t)| {
                    let bare_as = t.kind == TokKind::Word && t.text == "as";
                    bare_as.then(|| word_at(k + 1)).flatten()
                });
                if let Some(name) = alias {
                    let mut s = Symbol::new(&name.text, SymbolKind::Local, name.span, (ln, to_block_end));
                    s.ty = Some("entity".to_string());
                    s.file = word_at(1).and_then(|f| self.file_of_words(std::slice::from_ref(f)));
                    self.add(s, lines, i);
                }
            }
            _ => {}
        }
    }

    /// The file a name stands for, if one of these words is a file name from the header.
    fn file_of_words(&self, words: &[Tok]) -> Option<String> {
        words.iter().filter(|t| t.kind == TokKind::Word).find_map(|t| {
            self.symbols.iter().find(|s| s.name == t.text && s.kind == SymbolKind::File).and_then(|s| s.file.clone())
        })
    }

    fn resolve(&mut self) {
        let lines = std::mem::take(&mut self.lines);
        let declared: HashSet<(usize, usize)> = self.occurrences.iter().map(|o| (o.span.line, o.span.col)).collect();

        for (i, line) in lines.iter().enumerate() {
            if line.kind != LineKind::Code || line.indent == 0 {
                continue;
            }
            let ln = i + 1;
            let toks = &line.toks;
            let mut k = 0;
            while k < toks.len() {
                let t = &toks[k];
                let next_kind = toks.get(k + 1).map(|n| n.kind);
                let prev_is_dot = k > 0 && toks[k - 1].kind == TokKind::Dot;
                // `format "Score: {Score}"`: the names inside the braces are uses of those names.
                if t.kind == TokKind::Text && k > 0 && toks[k - 1].kind == TokKind::Word && toks[k - 1].text == "format" {
                    for (name, span) in format_names(t) {
                        self.reference(&name, span, ln, None);
                    }
                    k += 1;
                    continue;
                }
                if t.kind != TokKind::Word || declared.contains(&(ln, t.span.col)) || is_key(toks, k) || prev_is_dot {
                    k += 1;
                    continue;
                }

                // `Transform.Loc.X`: the first part is a name, the rest are fields.
                if next_kind == Some(TokKind::Dot) {
                    let mut segments = vec![(t.text.clone(), t.span)];
                    let mut at = k + 1;
                    while toks.get(at).is_some_and(|d| d.kind == TokKind::Dot) {
                        match toks.get(at + 1).filter(|w| w.kind == TokKind::Word) {
                            Some(w) => segments.push((w.text.clone(), w.span)),
                            None => break,
                        }
                        at += 2;
                    }
                    self.reference(&t.text, t.span, ln, None);
                    self.paths.push(PathRef { segments });
                    k = at;
                    continue;
                }

                // `do Action [on Thing]`
                if t.text == "do" {
                    if let Some(action) = toks.get(k + 1).filter(|a| a.kind == TokKind::Word) {
                        let on = toks.get(k + 2).is_some_and(|w| w.kind == TokKind::Word && w.text == "on");
                        match toks.get(k + 3).filter(|w| on && w.kind == TokKind::Word) {
                            Some(owner) => self.external(owner, action, ln, ExternalKind::Action),
                            None if !on => self.reference(&action.text, action.span, ln, Some(SymbolKind::Action)),
                            None => {}
                        }
                        k += 2;
                        continue;
                    }
                }
                // `tell Foe to TakeDamage amount:5`: the action belongs to whatever script Foe runs.
                if t.text == "tell" && k == 0 {
                    if let (Some(owner), Some(to), Some(action)) = (
                        toks.get(k + 1).filter(|w| w.kind == TokKind::Word),
                        toks.get(k + 2).filter(|w| w.kind == TokKind::Word && w.text == "to"),
                        toks.get(k + 3).filter(|w| w.kind == TokKind::Word),
                    ) {
                        let _ = to;
                        self.reference(&owner.text, owner.span, ln, None);
                        self.external(owner, action, ln, ExternalKind::Action);
                        k += 4;
                        continue;
                    }
                }
                // `result of Action`
                if t.text == "result" && toks.get(k + 1).is_some_and(|w| w.text == "of") {
                    if let Some(action) = toks.get(k + 2).filter(|a| a.kind == TokKind::Word) {
                        self.reference(&action.text, action.span, ln, Some(SymbolKind::Action));
                        k += 3;
                        continue;
                    }
                }
                // `when Hud sends Closed as Panel`
                if t.text == "sends" && k > 0 {
                    if let (Some(owner), Some(event)) = (
                        toks.get(k - 1).filter(|w| w.kind == TokKind::Word),
                        toks.get(k + 1).filter(|w| w.kind == TokKind::Word),
                    ) {
                        self.external(owner, event, ln, ExternalKind::Event);
                        k += 2;
                        continue;
                    }
                }
                if is_phrase_head(toks, k) {
                    k += 1;
                    continue;
                }
                self.reference(&t.text, t.span, ln, None);
                k += 1;
            }
        }
        self.lines = lines;
    }

    /// Record a use of `name` if it is a name that can be used on `line`.
    fn reference(&mut self, name: &str, span: Span, line: usize, only: Option<SymbolKind>) {
        if let Some(id) = self.lookup(name, line, only) {
            self.occurrences.push(Occurrence { span, symbol: id, declaration: false });
        }
    }

    fn external(&mut self, owner: &Tok, member: &Tok, line: usize, kind: ExternalKind) {
        if let Some(id) = self.lookup(&owner.text, line, None) {
            self.externals.push(External { span: member.span, owner: id, member: member.text.clone(), kind });
        }
    }

    fn lookup(&self, name: &str, line: usize, only: Option<SymbolKind>) -> Option<usize> {
        self.symbols
            .iter()
            .position(|s| s.name == name && s.scope.0 <= line && line <= s.scope.1 && only.is_none_or(|k| s.kind == k))
    }
}

/// The last line of the block that starts at `lines[i]`: the line before the next line of code
/// that is indented `threshold` or less.
fn end_before_indent(lines: &[Line], i: usize, threshold: usize) -> usize {
    let mut last = i + 1;
    for (j, l) in lines.iter().enumerate().skip(i + 1) {
        if l.kind == LineKind::Code {
            if l.indent <= threshold {
                break;
            }
            last = j + 1;
        }
    }
    last
}

/// A name that exists only inside one handler, action, or block.
fn is_local(kind: SymbolKind) -> bool {
    matches!(kind, SymbolKind::Parameter | SymbolKind::Local | SymbolKind::Loop)
}

/// Phrases begin with a fixed word, but the word can also be a name (`count`, `text`), so look at
/// what follows, the same way the parser does.
fn is_phrase_head(toks: &[Tok], k: usize) -> bool {
    let word_is = |at: usize, w: &str| toks.get(at).is_some_and(|t| t.kind == TokKind::Word && t.text == w);
    let kind_is = |at: usize, kind: TokKind| toks.get(at).is_some_and(|t| t.kind == kind);
    if table_phrase_head(toks, k) {
        return true;
    }
    match toks[k].text.as_str() {
        "distance" => word_is(k + 1, "from"),
        "count" => word_is(k + 1, "of"),
        "text" | "decimal" | "integer" => word_is(k + 1, "of"),
        "absolute" => word_is(k + 1, "value") && word_is(k + 2, "of"),
        "axis" => kind_is(k + 1, TokKind::Word) && word_is(k + 2, "of") && kind_is(k + 3, TokKind::Word),
        "time" => word_is(k + 1, "since") && word_is(k + 2, "start"),
        "item" => word_is(k + 2, "of"),
        "vector" => kind_is(k + 1, TokKind::Word) && kind_is(k + 2, TokKind::Colon),
        _ => false,
    }
}

/// Whether `toks[k]` is the key of a `key:value` pair. A colon that ends the line (`every frame:`,
/// `when Defeated:`, the last `:` of `action Hit amount:integer:`) opens a block, so it is not one.
fn is_key(toks: &[Tok], k: usize) -> bool {
    toks[k].kind == TokKind::Word
        && toks.get(k + 1).is_some_and(|c| c.kind == TokKind::Colon)
        && toks.get(k + 2).is_some()
}

/// `key:value` pairs on a line: each key, and the index of its value token.
fn key_values(toks: &[Tok]) -> Vec<(&str, usize)> {
    (0..toks.len()).filter(|&i| is_key(toks, i)).map(|i| (toks[i].text.as_str(), i + 2)).collect()
}

/// How many tokens a type takes starting at `at`: words up to the next key or the end.
fn type_len(toks: &[Tok], at: usize) -> usize {
    let mut n = 0;
    while let Some(t) = toks.get(at + n) {
        if t.kind != TokKind::Word || is_key(toks, at + n) || (n > 0 && t.text == "gives") {
            break;
        }
        n += 1;
    }
    n
}

fn type_text(toks: &[Tok], at: usize) -> String {
    toks[at.min(toks.len())..at.min(toks.len()) + type_len(toks, at)]
        .iter()
        .map(|t| t.text.as_str())
        .collect::<Vec<_>>()
        .join(" ")
}

/// A handler or action line without its trailing comment, like `when Self touches Coin as Pickup:`.
fn header_text(line: &Line) -> String {
    let chars: Vec<char> = line.raw.chars().collect();
    match (line.toks.first(), line.toks.last()) {
        (Some(first), Some(last)) => {
            let end = last.span.col - 1 + last.span.len;
            chars[first.span.col - 1..end.min(chars.len())].iter().collect()
        }
        _ => String::new(),
    }
}

/// The comments directly above a line, and the one at its end, as one block of text.
fn doc_for(lines: &[Line], i: usize) -> Option<String> {
    let mut above: Vec<String> = Vec::new();
    let mut j = i;
    while j > 0 && lines[j - 1].kind == LineKind::Comment {
        let text = lines[j - 1].raw.trim_start().trim_start_matches("//").trim().to_string();
        // `// node 6: Move Object` is for the node editor, not for people reading the code.
        if !is_node_marker(&text) {
            above.push(text);
        }
        j -= 1;
    }
    above.reverse();
    if let Some(t) = &lines[i].trailing {
        above.push(t.clone());
    }
    let doc = above.join("\n");
    (!doc.trim().is_empty()).then_some(doc)
}

fn is_node_marker(text: &str) -> bool {
    text.strip_prefix("node ").is_some_and(|rest| rest.starts_with(|c: char| c.is_ascii_digit()))
}

fn scan_line(raw: &str, line_no: usize) -> Line {
    let chars: Vec<char> = raw.chars().collect();
    let (mut tabs, mut spaces) = (0, 0);
    let mut pos = 0;
    while pos < chars.len() && (chars[pos] == '\t' || chars[pos] == ' ') {
        if chars[pos] == '\t' {
            tabs += 1;
        } else {
            spaces += 1;
        }
        pos += 1;
    }
    let indent = tabs + spaces / 4;

    if pos >= chars.len() {
        return Line { raw: raw.to_string(), indent, kind: LineKind::Blank, toks: Vec::new(), trailing: None };
    }
    if chars[pos] == '/' && chars.get(pos + 1) == Some(&'/') {
        return Line { raw: raw.to_string(), indent, kind: LineKind::Comment, toks: Vec::new(), trailing: None };
    }

    let n = chars.len();
    let mut toks = Vec::new();
    let mut trailing = None;
    let span = |col: usize, len: usize| Span { line: line_no, col, len };
    while pos < n {
        let c = chars[pos];
        let col = pos + 1;
        if c == ' ' || c == '\t' {
            pos += 1;
        } else if c == '/' && chars.get(pos + 1) == Some(&'/') {
            trailing = Some(chars[pos + 2..].iter().collect::<String>().trim().to_string());
            break;
        } else if c == '"' {
            let start = pos + 1;
            let end = chars[start..].iter().position(|&q| q == '"').map_or(n, |p| start + p);
            toks.push(Tok { kind: TokKind::Text, text: chars[start..end].iter().collect(), span: span(start + 1, end - start) });
            pos = (end + 1).min(n);
        } else if c.is_ascii_digit() || (c == '-' && chars.get(pos + 1).is_some_and(|d| d.is_ascii_digit())) {
            let start = pos;
            pos += 1;
            while pos < n && chars[pos].is_ascii_digit() {
                pos += 1;
            }
            if pos + 1 < n && chars[pos] == '.' && chars[pos + 1].is_ascii_digit() {
                pos += 1;
                while pos < n && chars[pos].is_ascii_digit() {
                    pos += 1;
                }
            }
            toks.push(Tok { kind: TokKind::Number, text: chars[start..pos].iter().collect(), span: span(col, pos - start) });
        } else if c.is_alphabetic() || c == '_' {
            let start = pos;
            while pos < n && (chars[pos].is_alphanumeric() || chars[pos] == '_') {
                pos += 1;
            }
            toks.push(Tok { kind: TokKind::Word, text: chars[start..pos].iter().collect(), span: span(col, pos - start) });
        } else {
            let kind = match c {
                ':' => TokKind::Colon,
                ',' => TokKind::Comma,
                '.' => TokKind::Dot,
                _ => TokKind::Other,
            };
            toks.push(Tok { kind, text: c.to_string(), span: span(col, 1) });
            pos += 1;
        }
    }
    Line { raw: raw.to_string(), indent, kind: LineKind::Code, toks, trailing }
}

/// Whether the words from `toks[k]` read as one of the phrases in `phrases.rs`.
fn table_phrase_head(toks: &[Tok], k: usize) -> bool {
    crate::phrases::starting_with(&toks[k].text).any(|def| {
        let mut at = k;
        for piece in def.pieces {
            match piece {
                crate::phrases::Piece::Word(w) => {
                    if !toks.get(at).is_some_and(|t| t.kind == TokKind::Word && t.text == *w) {
                        return false;
                    }
                    at += 1;
                }
                crate::phrases::Piece::Key(key) => {
                    if !(toks.get(at).is_some_and(|t| t.kind == TokKind::Word && t.text == *key) && toks.get(at + 1).is_some_and(|t| t.kind == TokKind::Colon)) {
                        return false;
                    }
                    at += 2;
                }
                crate::phrases::Piece::Arg(_) => {
                    if !toks.get(at).is_some_and(|t| matches!(t.kind, TokKind::Word | TokKind::Number | TokKind::Text)) {
                        return false;
                    }
                    at += 1;
                    while toks.get(at).is_some_and(|t| t.kind == TokKind::Dot) && toks.get(at + 1).is_some_and(|t| t.kind == TokKind::Word) {
                        at += 2;
                    }
                }
            }
        }
        true
    })
}

/// The names written between braces in the text of a `format` (`"Score: {Score}"`), each with where it is.
/// Only the first part of a field (`Transform` in `{Transform.Loc.X}`) is a name; the span of a text token is
/// the part inside the quotes.
fn format_names(tok: &Tok) -> Vec<(String, Span)> {
    crate::format::placeholder_names(&tok.text)
        .into_iter()
        .map(|(at, name)| {
            let len = name.chars().count();
            (name, Span { line: tok.span.line, col: tok.span.col + at, len })
        })
        .collect()
}
