use crate::ast::*;
use crate::error::PropulsionError;
use crate::lexer::{lex_with_comments, Comments, Lexed, Tok, Token};

type PResult<T> = Result<T, PropulsionError>;

/// Statement patterns, listed in "I expected one of" errors (SPEC 6, rule 1).
const STATEMENT_PATTERNS: &[&str] = &[
    "make <target> <value>",
    "add <value> to <target>",
    "subtract <value> from <target>",
    "multiply <target> by <value>",
    "divide <target> by <value>",
    "remove <value> from <list>",
    "do <Action> [on <Thing>] key:value ...",
    "send <Event>",
    "remember <Name> <value>",
    "reset <timer>",
    "spawn <File> at:<value> as <Name>",
    "move <target> toward <value> by <value>",
    "move <entity> <forward|back|left|right|up|down> by <value>",
    "print <value>",
    "after <N> seconds:",
    "for <Name> from <N> to <N>:",
    "destroy <entity>",
    "give back <value>",
    "finish",
    "stop loop",
    "skip",
    "if <condition>, then <statement>",
    "if <condition>:",
    "repeat <N> times:",
    "for each <Name> in <list>:",
    "while <condition>:",
    "clear <list>",
    "only once:",
    "first <N> times:",
    "alternate:  otherwise:",
    "open <Gate>  /  close <Gate>  /  toggle <Gate>",
    "through <Gate>:",
    "choose <value>:  case <value>:  otherwise:",
    "animate <Name> from <A> to <B> over <N> seconds:",
    "damage <entity> by <value>",
    "attach <entity> to <entity>  /  detach <entity>",
    "tell <entity> to <Action> key:value ...",
];

/// Words that start a statement. Used to explain that statements need a handler around them.
const STATEMENT_KEYWORDS: &[&str] = &[
    "make", "add", "subtract", "multiply", "divide", "remove", "do", "send", "remember", "reset", "spawn", "move",
    "destroy", "give", "finish", "stop", "skip", "if", "repeat", "for", "while", "print", "after", "clear", "only",
    "first", "alternate", "open", "close", "toggle", "through", "choose", "animate", "damage", "attach", "detach", "tell",
];

const TYPE_NAMES: &str = "integer, decimal, boolean, text, vector2, vector3, color, entity, timer, gate, list of <type>";

/// Parse the text of a `.prop` file into a syntax tree.
pub fn parse(src: &str) -> PResult<File> {
    let Lexed { tokens, comments } = lex_with_comments(src)?;
    Parser { toks: tokens, pos: 0, comments, enums: Vec::new(), structs: Vec::new() }.file()
}

enum PairVal {
    Type(TypeExpr),
    Term(Term),
}

type Pairs = Vec<(String, PairVal)>;

struct Parser {
    toks: Vec<Token>,
    pos: usize,
    comments: Comments,
    /// The enums and structs declared so far: a type name is only a type after its declaration.
    enums: Vec<String>,
    structs: Vec<String>,
}

fn describe(tok: &Tok) -> String {
    match tok {
        Tok::Word(w) => format!("`{w}`"),
        Tok::Number(n) => format!("`{n}`"),
        Tok::Text(t) => format!("\"{t}\""),
        Tok::Colon => "`:`".into(),
        Tok::Comma => "`,`".into(),
        Tok::Dot => "`.`".into(),
        Tok::Newline => "the end of the line".into(),
        Tok::Indent => "an unexpected indent".into(),
        Tok::Dedent => "the end of the block".into(),
        Tok::Eof => "the end of the file".into(),
    }
}

impl Parser {
    // ---- token helpers ----

    fn cur(&self) -> &Token {
        &self.toks[self.pos]
    }

    fn peek(&self) -> &Tok {
        &self.cur().tok
    }

    fn peek_at(&self, n: usize) -> &Tok {
        let i = (self.pos + n).min(self.toks.len() - 1);
        &self.toks[i].tok
    }

    fn line(&self) -> usize {
        self.cur().line
    }

    fn advance(&mut self) -> Token {
        let t = self.cur().clone();
        if self.pos < self.toks.len() - 1 {
            self.pos += 1;
        }
        t
    }

    fn err<T>(&self, msg: impl Into<String>) -> PResult<T> {
        let t = self.cur();
        Err(PropulsionError::new(t.line, t.col, msg))
    }

    fn is_word(&self, w: &str) -> bool {
        self.is_word_at(0, w)
    }

    fn is_word_at(&self, n: usize, w: &str) -> bool {
        matches!(self.peek_at(n), Tok::Word(x) if x == w)
    }

    fn eat_word(&mut self, w: &str) -> bool {
        if self.is_word(w) {
            self.advance();
            true
        } else {
            false
        }
    }

    fn expect_word(&mut self, w: &str) -> PResult<()> {
        if self.eat_word(w) {
            Ok(())
        } else {
            self.err(format!("expected `{w}`, found {}", describe(self.peek())))
        }
    }

    fn word(&mut self, what: &str) -> PResult<String> {
        if let Tok::Word(w) = self.peek().clone() {
            self.advance();
            Ok(w)
        } else {
            self.err(format!("expected {what}, found {}", describe(self.peek())))
        }
    }

    fn expect(&mut self, tok: Tok, what: &str) -> PResult<()> {
        if *self.peek() == tok {
            self.advance();
            Ok(())
        } else {
            self.err(format!("expected {what}, found {}", describe(self.peek())))
        }
    }

    fn end_of_line(&mut self) -> PResult<()> {
        if matches!(self.peek(), Tok::Newline) {
            self.advance();
            Ok(())
        } else {
            self.err(format!(
                "expected the end of the line, found {}. If you added an extra word, remove it: PropulsionScript has no optional filler words",
                describe(self.peek())
            ))
        }
    }

    // ---- file and sections ----

    /// Take the comments and spacing that belong to the line of code at `line`.
    fn notes(&mut self, line: usize) -> Notes {
        Notes {
            floating: Default::default(),
            leading: self.comments.leading.remove(&line).unwrap_or_default(),
            trailing: self.comments.trailing.remove(&line),
            blank_before: self.comments.blank_before.contains(&line),
            gaps: self.comments.gaps.remove(&line).unwrap_or_default(),
            pos: None,
            value_pos: Vec::new(),
        }
    }

    fn file(&mut self) -> PResult<File> {
        let header_notes = self.notes(self.line());
        self.section_head("header")?;
        let mut header = Vec::new();
        while !matches!(self.peek(), Tok::Dedent) {
            header.push(self.decl()?);
        }
        self.advance();

        let implement_notes = self.notes(self.line());
        self.section_head("implement")?;
        self.eat_empty_marker();
        let mut implement = Vec::new();
        while !matches!(self.peek(), Tok::Dedent) {
            implement.push(self.item()?);
        }
        self.advance();

        if !matches!(self.peek(), Tok::Eof) {
            return self.err("unexpected content after `implement:`; a file has exactly two sections");
        }
        // Anything still in the maps sits where no node could take it.
        let mut unattached: Vec<usize> = self.comments.leading.keys().chain(self.comments.trailing.keys()).copied().collect();
        unattached.sort_unstable();
        unattached.dedup();
        Ok(File {
            header,
            implement,
            header_notes,
            implement_notes,
            footer: std::mem::take(&mut self.comments.footer),
            unattached,
        })
    }

    fn section_head(&mut self, name: &str) -> PResult<()> {
        if !self.is_word(name) {
            return self.err(format!(
                "expected the `{name}:` section, found {}",
                describe(self.peek())
            ));
        }
        self.advance();
        self.expect(Tok::Colon, &format!("`:` after `{name}`"))?;
        self.expect(Tok::Newline, "the end of the line")?;
        self.expect(Tok::Indent, &format!("an indented block under `{name}:`"))?;
        Ok(())
    }

    // ---- header ----

    fn decl(&mut self) -> PResult<Decl> {
        let line = self.line();
        let notes = self.notes(line);
        let kw = self.word("a declaration (`variable`, `constant`, `needs`, or `event`)")?;
        let kind = match kw.as_str() {
            "variable" | "constant" => {
                let pairs = self.pairs()?;
                let allowed: &[&str] = if kw == "variable" {
                    &["name", "type", "value", "exposed", "x", "y", "z", "w", "r", "g", "b", "a"]
                } else {
                    &["name", "type", "value", "x", "y", "z", "w", "r", "g", "b", "a"]
                };
                check_keys(&pairs, allowed, &kw, line)?;
                let exposed = match find(&pairs, "exposed") {
                    Some(PairVal::Term(Term::Bool(b))) => *b,
                    Some(_) => {
                        return Err(PropulsionError::new(line, 1, "`exposed:` must be `true` or `false`"));
                    }
                    None => false,
                };
                let name = name_of(&pairs, "name", line)?;
                let ty = match find(&pairs, "type") {
                    Some(PairVal::Type(t)) => t.clone(),
                    _ => return Err(PropulsionError::new(line, 1, format!("`{kw}` needs a `type:`"))),
                };
                let value = match find(&pairs, "value") {
                    Some(PairVal::Term(t)) => Some(t.clone()),
                    _ => None,
                };
                let parts = collect_parts(&pairs);
                if kw == "constant" {
                    DeclKind::Constant { name, ty, value, parts }
                } else {
                    let entries = self.entries()?;
                    DeclKind::Variable { name, ty, value, parts, entries, exposed }
                }
            }
            "needs" => {
                let pairs = self.pairs()?;
                let first = pairs
                    .first()
                    .map(|(k, _)| k.clone())
                    .ok_or_else(|| PropulsionError::new(line, 1, "`needs` must be followed by `component:`, `system:`, `entity:`, or `file:`"))?;
                match first.as_str() {
                    "component" => {
                        check_keys(&pairs, &["component"], "needs component", line)?;
                        DeclKind::NeedsComponent(name_of(&pairs, "component", line)?)
                    }
                    "system" => {
                        check_keys(&pairs, &["system"], "needs system", line)?;
                        DeclKind::NeedsSystem(name_of(&pairs, "system", line)?)
                    }
                    "entity" => {
                        check_keys(&pairs, &["entity", "file", "tag"], "needs entity", line)?;
                        DeclKind::NeedsEntity {
                            name: name_of(&pairs, "entity", line)?,
                            file: text_of(&pairs, "file", line)?,
                            tag: text_of(&pairs, "tag", line)?,
                        }
                    }
                    "entities" => {
                        check_keys(&pairs, &["entities", "tag", "file"], "needs entities", line)?;
                        DeclKind::NeedsEntities {
                            name: name_of(&pairs, "entities", line)?,
                            tag: text_of(&pairs, "tag", line)?,
                            file: text_of(&pairs, "file", line).ok(),
                        }
                    }
                    "file" => {
                        check_keys(&pairs, &["file", "as", "when"], "needs file", line)?;
                        DeclKind::NeedsFile {
                            file: text_of(&pairs, "file", line)?,
                            alias: name_of(&pairs, "as", line).ok(),
                            when: name_of(&pairs, "when", line).ok(),
                        }
                    }
                    other => {
                        return Err(PropulsionError::new(
                            line,
                            1,
                            format!("`needs {other}` is not a declaration; I expected `needs` followed by `component:`, `system:`, `entity:`, `entities:`, or `file:`"),
                        ))
                    }
                }
            }
            "event" => {
                let pairs = self.pairs()?;
                check_keys(&pairs, &["name"], "event", line)?;
                DeclKind::Event(name_of(&pairs, "name", line)?)
            }
            "enum" => {
                self.expect_word("name")?;
                self.expect(Tok::Colon, "`:` after `name`")?;
                let name = self.word("a name for the enum")?;
                self.expect_word("values")?;
                self.expect(Tok::Colon, "`:` after `values`")?;
                let mut values = Vec::new();
                while let Tok::Word(w) = self.peek().clone() {
                    self.advance();
                    values.push(w);
                }
                self.end_of_line()?;
                if values.len() < 2 {
                    return Err(PropulsionError::new(line, 1, "an enum needs at least two values, like `values:Idle Chasing`"));
                }
                self.enums.push(name.clone());
                DeclKind::Enum { name, values }
            }
            "struct" => {
                let pairs = self.pairs()?;
                check_keys(&pairs, &["name"], "struct", line)?;
                let name = name_of(&pairs, "name", line)?;
                let fields = self.struct_fields()?;
                if fields.is_empty() {
                    return Err(PropulsionError::new(line, 1, "a struct needs at least one indented `field name:Cost type:integer` line"));
                }
                self.structs.push(name.clone());
                DeclKind::Struct { name, fields }
            }
            "table" => {
                let pairs = self.pairs()?;
                check_keys(&pairs, &["name", "type"], "table", line)?;
                let name = name_of(&pairs, "name", line)?;
                let ty = match find(&pairs, "type") {
                    Some(PairVal::Type(TypeExpr::Struct(n))) => n.clone(),
                    _ => return Err(PropulsionError::new(line, 1, "a table needs `type:` naming a struct declared above it")),
                };
                let rows = self.table_rows()?;
                DeclKind::Table { name, ty, rows }
            }
            "tag" => {
                let pairs = self.pairs()?;
                check_keys(&pairs, &["name"], "tag", line)?;
                DeclKind::Tag(text_of(&pairs, "name", line)?)
            }
            other => {
                return Err(PropulsionError::new(
                    line,
                    1,
                    format!("`{other}` is not a declaration; I expected one of: variable / constant / needs / event / tag"),
                ))
            }
        };
        Ok(Decl { kind, line, notes })
    }

    /// The indented `field name:Cost type:integer` lines under a `struct`.
    fn struct_fields(&mut self) -> PResult<Vec<(String, TypeExpr)>> {
        let mut fields = Vec::new();
        if !matches!(self.peek(), Tok::Indent) {
            return Ok(fields);
        }
        self.advance();
        while !matches!(self.peek(), Tok::Dedent) {
            let line = self.line();
            self.expect_word("field")?;
            let pairs = self.pairs()?;
            check_keys(&pairs, &["name", "type"], "field", line)?;
            let name = name_of(&pairs, "name", line)?;
            let ty = match find(&pairs, "type") {
                Some(PairVal::Type(t)) => t.clone(),
                _ => return Err(PropulsionError::new(line, 1, "a `field` needs a `type:`")),
            };
            fields.push((name, ty));
        }
        self.advance();
        Ok(fields)
    }

    /// The indented `row key:"Sword" Cost:5` lines under a `table`.
    fn table_rows(&mut self) -> PResult<Vec<TableRow>> {
        let mut rows = Vec::new();
        if !matches!(self.peek(), Tok::Indent) {
            return Ok(rows);
        }
        self.advance();
        while !matches!(self.peek(), Tok::Dedent) {
            let line = self.line();
            self.expect_word("row")?;
            let pairs = self.pairs()?;
            let key = match pairs.first() {
                Some((k, PairVal::Term(Term::Text(t)))) if k == "key" => t.clone(),
                _ => return Err(PropulsionError::new(line, 1, "a `row` starts with its key as quoted text, like `row key:\"Sword\" Cost:5`")),
            };
            let values = pairs
                .iter()
                .skip(1)
                .map(|(k, v)| match v {
                    PairVal::Term(t) => Ok((k.clone(), t.clone())),
                    PairVal::Type(_) => Err(PropulsionError::new(line, 1, format!("`{k}:` is a field of the row, so it takes a value, not a type"))),
                })
                .collect::<PResult<Vec<_>>>()?;
            rows.push(TableRow { key, values });
        }
        self.advance();
        Ok(rows)
    }

    /// Optional indented `entry` lines under a variable declaration.
    fn entries(&mut self) -> PResult<Vec<Parts>> {
        let mut entries = Vec::new();
        if !matches!(self.peek(), Tok::Indent) {
            return Ok(entries);
        }
        self.advance();
        while !matches!(self.peek(), Tok::Dedent) {
            let line = self.line();
            self.expect_word("entry")?;
            let pairs = self.pairs()?;
            check_keys(&pairs, &["x", "y", "z", "w", "r", "g", "b", "a"], "entry", line)?;
            entries.push(collect_parts(&pairs));
        }
        self.advance();
        Ok(entries)
    }

    /// `key:value key:value as Name ...` up to the end of the line.
    fn pairs(&mut self) -> PResult<Pairs> {
        let mut pairs = Vec::new();
        while !matches!(self.peek(), Tok::Newline) {
            let key = self.word("a `key:value` pair")?;
            if key == "as" && !matches!(self.peek(), Tok::Colon) {
                let name = self.word("a name after `as`")?;
                pairs.push((key, PairVal::Term(Term::Name(name))));
                continue;
            }
            self.expect(Tok::Colon, &format!("`:` after `{key}`"))?;
            if key == "type" {
                pairs.push((key, PairVal::Type(self.type_expr()?)));
            } else {
                pairs.push((key, PairVal::Term(self.term()?)));
            }
        }
        self.end_of_line()?;
        Ok(pairs)
    }

    fn type_expr(&mut self) -> PResult<TypeExpr> {
        let w = self.word("a type")?;
        Ok(match w.as_str() {
            "integer" => TypeExpr::Integer,
            "decimal" => TypeExpr::Decimal,
            "boolean" => TypeExpr::Boolean,
            "text" => TypeExpr::Text,
            "vector2" => TypeExpr::Vector2,
            "vector3" => TypeExpr::Vector3,
            "color" => TypeExpr::Color,
            "entity" => TypeExpr::Entity,
            "timer" => TypeExpr::Timer,
            "gate" => TypeExpr::Gate,
            "list" => {
                self.expect_word("of")?;
                TypeExpr::List(Box::new(self.type_expr()?))
            }
            other if self.enums.iter().any(|e| e == other) => TypeExpr::Enum(other.to_string()),
            // `hit` is what a trace gives; the rest are declared in the header, above where they are used.
            other if other == "hit" || self.structs.iter().any(|e| e == other) => TypeExpr::Struct(other.to_string()),
            other => {
                return Err(PropulsionError::new(
                    self.toks[self.pos.saturating_sub(1)].line,
                    self.toks[self.pos.saturating_sub(1)].col,
                    format!("`{other}` is not a type; I expected one of: {TYPE_NAMES}"),
                ))
            }
        })
    }

    // ---- implement ----

    fn item(&mut self) -> PResult<Item> {
        let line = self.line();
        let notes = self.notes(line);
        let kw = self.word("`when`, `every`, or `action`")?;
        let kind = match kw.as_str() {
            "when" => {
                let trigger = if self.eat_word("created") {
                    Trigger::Created
                } else if self.eat_word("destroyed") {
                    Trigger::Destroyed
                } else if self.is_word("Self") && self.is_word_at(1, "touches") {
                    self.advance();
                    self.advance();
                    let file = self.word("a file name")?;
                    self.expect_word("as")?;
                    let name = self.word("a name")?;
                    Trigger::Touches { file, name }
                } else if self.is_word("Self") && self.is_word_at(1, "hits") {
                    self.advance();
                    self.advance();
                    let file = self.word("a file name")?;
                    self.expect_word("as")?;
                    let name = self.word("a name")?;
                    Trigger::Hits { file, name }
                } else if self.is_word("damaged") && self.is_word_at(1, "as") {
                    self.advance();
                    self.advance();
                    let amount = self.word("a name for the amount of damage")?;
                    let source = if self.eat_word("from") { Some(self.word("a name for the entity that did it")?) } else { None };
                    Trigger::Damaged { amount, source }
                } else if self.is_word("Self") && self.is_word_at(1, "stops") && self.is_word_at(2, "touching") {
                    for _ in 0..3 {
                        self.advance();
                    }
                    let file = self.word("a file name")?;
                    self.expect_word("as")?;
                    let name = self.word("a name")?;
                    Trigger::StopsTouching { file, name }
                } else if matches!(self.peek(), Tok::Word(_))
                    && (self.is_word_at(1, "starts") || self.is_word_at(1, "stops"))
                    && self.is_word_at(2, "pressing")
                {
                    let system = self.word("a system name")?;
                    let kind = if self.eat_word("starts") { PressKind::Started } else { self.advance(); PressKind::Stopped };
                    self.expect_word("pressing")?;
                    let input = self.word("an input name")?;
                    Trigger::Input { system, input, kind }
                } else {
                    let first = self.word("an event name")?;
                    if self.eat_word("changes") {
                        Trigger::Changes(first)
                    } else if self.eat_word("sends") {
                        let event = self.word("an event name")?;
                        self.expect_word("as")?;
                        let name = self.word("a name")?;
                        Trigger::Sends { file: first, event, name }
                    } else {
                        Trigger::Event(first)
                    }
                };
                self.expect(Tok::Colon, "`:` after the handler")?;
                ItemKind::Handler { trigger, body: self.block()? }
            }
            "every" => {
                let trigger = if self.eat_word("frame") {
                    Trigger::EveryFrame
                } else if let Tok::Number(n) = self.peek().clone() {
                    self.advance();
                    if !(self.eat_word("seconds") || self.eat_word("second")) {
                        return self.err(format!("expected `seconds` after `every {n}`, found {}", describe(self.peek())));
                    }
                    Trigger::EverySeconds(n.parse::<f64>().unwrap_or(0.0))
                } else {
                    return self.err(format!(
                        "expected `frame` or a number of seconds after `every`, found {}",
                        describe(self.peek())
                    ));
                };
                self.expect(Tok::Colon, "`:` after the handler")?;
                ItemKind::Handler { trigger, body: self.block()? }
            }
            "action" => {
                let name = self.word("an action name")?;
                let mut params = Vec::new();
                let mut gives = None;
                loop {
                    if matches!(self.peek(), Tok::Colon) {
                        break;
                    }
                    if self.eat_word("gives") {
                        gives = Some(self.type_expr()?);
                        continue;
                    }
                    let pname = self.word("a parameter name, `gives`, or `:`")?;
                    self.expect(Tok::Colon, &format!("`:` after parameter `{pname}`"))?;
                    let ty = self.type_expr()?;
                    params.push(Param { name: pname, ty });
                }
                self.expect(Tok::Colon, "`:` at the end of the action line")?;
                ItemKind::Action { name, params, gives, body: self.block()? }
            }
            other if STATEMENT_KEYWORDS.contains(&other) => {
                return Err(PropulsionError::new(
                    line,
                    self.toks[self.pos - 1].col,
                    format!(
                        "code directly under `implement:` has to say when it runs. Put it inside `every frame:` (runs every frame), `when created:` (runs once at the start), or another handler (see SPEC 4.5); `{other}` is a statement, not a handler"
                    ),
                ))
            }
            other => {
                return Err(PropulsionError::new(
                    line,
                    1,
                    format!("`{other}` can't start a block here; I expected one of: when ... / every ... / action ..."),
                ))
            }
        };
        Ok(Item { kind, line, notes })
    }

    /// The indented body after a `:`.
    fn block(&mut self) -> PResult<Vec<Stmt>> {
        self.expect(Tok::Newline, "the end of the line after `:`")?;
        self.expect(Tok::Indent, "an indented block")?;
        self.eat_empty_marker();
        let mut body = Vec::new();
        while !matches!(self.peek(), Tok::Dedent) {
            if matches!(self.peek(), Tok::Eof) {
                return self.err("unexpected end of file inside a block");
            }
            body.push(self.statement()?);
        }
        self.advance();
        Ok(body)
    }

    /// A block that holds nothing is written as the single line `do nothing`. It makes no statement:
    /// the block is simply empty (the formatter writes the line back). Only as a block's one line.
    fn eat_empty_marker(&mut self) {
        if self.is_word("do")
            && self.is_word_at(1, "nothing")
            && matches!(self.peek_at(2), Tok::Newline)
            && matches!(self.peek_at(3), Tok::Dedent)
        {
            for _ in 0..3 {
                self.advance();
            }
        }
    }

    // ---- statements ----

    fn statement(&mut self) -> PResult<Stmt> {
        let line = self.line();
        let notes = self.notes(line);
        let kw = match self.peek().clone() {
            Tok::Word(w) => w,
            Tok::Indent => return self.err("unexpected indent; only a line ending in `:` can start an indented block"),
            other => return self.err(format!("expected a statement, found {}", describe(&other))),
        };
        let kind = match kw.as_str() {
            "if" => self.if_stmt()?,
            "repeat" => {
                self.advance();
                let times = self.term()?;
                self.expect_word("times")?;
                self.expect(Tok::Colon, "`:` after `repeat ... times`")?;
                StmtKind::Repeat { times, body: self.block()? }
            }
            "for" => {
                self.advance();
                if self.eat_word("each") {
                    let var = self.word("a name for each item")?;
                    self.expect_word("in")?;
                    let list = self.term()?;
                    self.expect(Tok::Colon, "`:` after the `for each` line")?;
                    StmtKind::ForEach { var, list, body: self.block()? }
                } else {
                    let var = match self.peek().clone() {
                        Tok::Word(w) if self.is_word_at(1, "from") => {
                            self.advance();
                            w
                        }
                        _ => {
                            return self.err(format!(
                                "expected `each <Name> in <list>` or `<Name> from <N> to <N>` after `for`, found {}",
                                describe(self.peek())
                            ))
                        }
                    };
                    self.expect_word("from")?;
                    let from = self.term()?;
                    self.expect_word("to")?;
                    let to = self.term()?;
                    self.expect(Tok::Colon, "`:` after the `for ... from ... to ...` line")?;
                    StmtKind::ForRange { var, from, to, body: self.block()? }
                }
            }
            "after" => {
                self.advance();
                let seconds = self.term()?;
                // `after 2 seconds:` is read as a duration; `after Delay seconds:` names a number.
                if !matches!(seconds, Term::Duration(_)) && !(self.eat_word("seconds") || self.eat_word("second")) {
                    return self.err(format!(
                        "expected `seconds` after the delay, like `after 2 seconds:`, found {}",
                        describe(self.peek())
                    ));
                }
                self.expect(Tok::Colon, "`:` after `after ... seconds`")?;
                StmtKind::After { seconds, body: self.block()? }
            }
            "while" => {
                self.advance();
                let cond = self.condition()?;
                self.expect(Tok::Colon, "`:` after the `while` condition")?;
                StmtKind::While { cond, body: self.block()? }
            }
            "only" => {
                self.advance();
                self.expect_word("once")?;
                let name = self.latch_name()?;
                self.expect(Tok::Colon, "`:` after `only once`")?;
                StmtKind::OnlyOnce { name, body: self.block()? }
            }
            "first" => {
                self.advance();
                let count = self.term()?;
                self.expect_word("times")?;
                let name = self.latch_name()?;
                self.expect(Tok::Colon, "`:` after `first ... times`")?;
                StmtKind::FirstTimes { count, name, body: self.block()? }
            }
            "alternate" => {
                self.advance();
                let name = self.latch_name()?;
                self.expect(Tok::Colon, "`:` after `alternate`")?;
                let first = self.block()?;
                if !self.is_word("otherwise") {
                    return self.err("`alternate:` needs an `otherwise:` block, which runs every other time");
                }
                self.advance();
                self.expect(Tok::Colon, "`:` after `otherwise`")?;
                StmtKind::Alternate { name, first, second: self.block()? }
            }
            "through" => {
                self.advance();
                let gate = self.word("a gate name")?;
                self.expect(Tok::Colon, "`:` after `through <Gate>`")?;
                StmtKind::Through { gate, body: self.block()? }
            }
            "choose" => self.choose_stmt()?,
            "animate" => {
                self.advance();
                let var = self.word("a name for the moving value")?;
                self.expect_word("from")?;
                let from = self.term()?;
                self.expect_word("to")?;
                let to = self.term()?;
                self.expect_word("over")?;
                let seconds = self.term()?;
                if !matches!(seconds, Term::Duration(_)) && !(self.eat_word("seconds") || self.eat_word("second")) {
                    return self.err(format!("expected `seconds` after how long, like `over 2 seconds:`, found {}", describe(self.peek())));
                }
                self.expect(Tok::Colon, "`:` after `animate ... over ... seconds`")?;
                StmtKind::Animate { var, from, to, seconds, body: self.block()? }
            }
            _ => {
                let k = self.simple_statement()?;
                self.end_of_line()?;
                k
            }
        };
        Ok(Stmt { kind, line, notes })
    }

    /// An optional `as Name` after `only once`, `first N times` or `alternate`, so `reset Name` can start it over.
    fn latch_name(&mut self) -> PResult<Option<String>> {
        if self.eat_word("as") {
            Ok(Some(self.word("a name after `as`")?))
        } else {
            Ok(None)
        }
    }

    fn choose_stmt(&mut self) -> PResult<StmtKind> {
        self.advance(); // `choose`
        let value = self.term()?;
        self.expect(Tok::Colon, "`:` after `choose <value>`")?;
        self.expect(Tok::Newline, "the end of the line after `:`")?;
        self.expect(Tok::Indent, "an indented list of `case` lines")?;
        let mut cases = Vec::new();
        let mut otherwise = None;
        while !matches!(self.peek(), Tok::Dedent) {
            if matches!(self.peek(), Tok::Eof) {
                return self.err("unexpected end of file inside a `choose`");
            }
            if otherwise.is_some() {
                return self.err("`otherwise:` has to be the last thing in a `choose`");
            }
            if self.eat_word("case") {
                let mut values = vec![self.term()?];
                while self.eat_word("or") {
                    values.push(self.term()?);
                }
                self.expect(Tok::Colon, "`:` after the `case` value")?;
                cases.push(Case { values, body: self.block()? });
            } else if self.eat_word("otherwise") {
                self.expect(Tok::Colon, "`:` after `otherwise`")?;
                otherwise = Some(self.block()?);
            } else {
                return self.err(format!("a `choose` holds `case <value>:` and `otherwise:`, found {}", describe(self.peek())));
            }
        }
        self.advance();
        if cases.is_empty() {
            return self.err("a `choose` needs at least one `case <value>:`");
        }
        Ok(StmtKind::Choose { value, cases, otherwise })
    }

    fn if_stmt(&mut self) -> PResult<StmtKind> {
        self.advance(); // `if`
        let cond = self.condition()?;
        if matches!(self.peek(), Tok::Comma) {
            self.advance();
            self.expect_word("then")?;
            let line = self.line();
            let kind = self.simple_statement()?;
            self.end_of_line()?;
            // The one-line form shares its line with the `if`, so the `if` keeps the comments.
            let inner = Stmt { kind, line, notes: Notes::default() };
            return Ok(StmtKind::If { branches: vec![(cond, vec![inner])], otherwise: None, inline: true });
        }
        self.expect(Tok::Colon, "`:` or `, then` after the condition")?;
        let mut branches = vec![(cond, self.block()?)];
        let mut otherwise = None;
        while self.is_word("otherwise") {
            self.advance();
            if self.eat_word("if") {
                let c = self.condition()?;
                self.expect(Tok::Colon, "`:` after the condition")?;
                branches.push((c, self.block()?));
            } else {
                self.expect(Tok::Colon, "`:` after `otherwise`")?;
                otherwise = Some(self.block()?);
                break;
            }
        }
        Ok(StmtKind::If { branches, otherwise, inline: false })
    }

    /// A statement that fits on one line (no block). Does not consume the newline.
    fn simple_statement(&mut self) -> PResult<StmtKind> {
        let kw = match self.peek().clone() {
            Tok::Word(w) => w,
            other => return self.err(format!("expected a statement, found {}", describe(&other))),
        };
        let known = matches!(
            kw.as_str(),
            "make" | "add" | "subtract" | "multiply" | "divide" | "remove" | "do" | "send" | "remember"
                | "reset" | "spawn" | "move" | "destroy" | "give" | "finish" | "stop" | "skip" | "print"
                | "clear" | "open" | "close" | "toggle" | "damage" | "attach" | "detach" | "tell"
        );
        if !known {
            return self.err(format!(
                "`{kw}` doesn't start any statement I know. I expected one of: {}",
                STATEMENT_PATTERNS.join(" / ")
            ));
        }
        self.advance();
        Ok(match kw.as_str() {
            "make" => {
                let target = self.term()?;
                let value = self.value()?;
                StmtKind::Make { target, value, limit: self.limit()? }
            }
            "add" => {
                let value = self.value()?;
                self.expect_word("to")?;
                let target = self.term()?;
                if matches!(self.peek(), Tok::Comma) && self.is_word_at(1, "if") && self.is_word_at(2, "new") {
                    for _ in 0..3 {
                        self.advance();
                    }
                    StmtKind::AddUnique { value, list: target }
                } else {
                    StmtKind::Add { value, target, limit: self.limit()? }
                }
            }
            "subtract" => {
                let value = self.value()?;
                self.expect_word("from")?;
                let target = self.term()?;
                StmtKind::Subtract { value, target, limit: self.limit()? }
            }
            "multiply" => {
                let target = self.term()?;
                self.expect_word("by")?;
                StmtKind::Multiply { target, value: self.value()? }
            }
            "divide" => {
                let target = self.term()?;
                self.expect_word("by")?;
                StmtKind::Divide { target, value: self.value()? }
            }
            "remove" => {
                // `remove item 2 from Alive` (by position), against `remove Fallen from Alive` (by value).
                if self.is_word("item") {
                    let save = self.pos;
                    self.advance();
                    if let Ok(index) = self.term() {
                        if self.eat_word("from") {
                            return Ok(StmtKind::RemoveAt { index, list: self.term()? });
                        }
                    }
                    self.pos = save;
                }
                let value = self.value()?;
                self.expect_word("from")?;
                StmtKind::Remove { value, list: self.term()? }
            }
            "clear" => StmtKind::Clear(self.term()?),
            "open" => StmtKind::GateChange { gate: self.word("a gate name")?, change: GateChange::Open },
            "close" => StmtKind::GateChange { gate: self.word("a gate name")?, change: GateChange::Close },
            "toggle" => StmtKind::GateChange { gate: self.word("a gate name")?, change: GateChange::Toggle },
            "damage" => {
                let target = self.term()?;
                self.expect_word("by")?;
                StmtKind::Damage { target, amount: self.value()? }
            }
            "attach" => {
                let child = self.term()?;
                self.expect_word("to")?;
                StmtKind::Attach { child, parent: self.term()? }
            }
            "detach" => StmtKind::Detach(self.term()?),
            "tell" => {
                let target = self.term()?;
                self.expect_word("to")?;
                let action = self.word("an action name")?;
                StmtKind::Tell { target, action, args: self.args()? }
            }
            "do" => {
                let action = self.word("an action name")?;
                let on = if self.eat_word("on") { Some(self.word("a file, system, or entity name after `on`")?) } else { None };
                StmtKind::Do { action, on, args: self.args()? }
            }
            "send" => StmtKind::Send(self.word("an event name")?),
            "remember" => {
                let name = self.word("a name to remember")?;
                StmtKind::Remember { name, value: self.value_or_condition()? }
            }
            "reset" => StmtKind::Reset(self.word("a timer name")?),
            "spawn" => {
                let file = self.word("a file name")?;
                self.expect_word("at")?;
                self.expect(Tok::Colon, "`:` after `at`")?;
                let at = self.value()?;
                self.expect_word("as")?;
                StmtKind::Spawn { file, at, name: self.word("a name for the new entity")? }
            }
            "move" => {
                let target = self.term()?;
                let dir = match self.peek() {
                    Tok::Word(w) => match w.as_str() {
                        "forward" => Some(Direction::Forward),
                        "back" => Some(Direction::Back),
                        "left" => Some(Direction::Left),
                        "right" => Some(Direction::Right),
                        "up" => Some(Direction::Up),
                        "down" => Some(Direction::Down),
                        _ => None,
                    },
                    _ => None,
                };
                if let Some(dir) = dir {
                    self.advance();
                    self.expect_word("by")?;
                    StmtKind::MoveDir { target, dir, by: self.value()? }
                } else if self.eat_word("toward") {
                    let toward = self.value()?;
                    self.expect_word("by")?;
                    StmtKind::Move { target, toward, by: self.value()? }
                } else {
                    return self.err(format!(
                        "expected `toward` or a direction (forward, back, left, right, up, down) after `move <target>`, found {}",
                        describe(self.peek())
                    ));
                }
            }
            "print" => StmtKind::Print(self.value()?),
            "destroy" => StmtKind::Destroy(self.term()?),
            "give" => {
                self.expect_word("back")?;
                StmtKind::GiveBack(self.value_or_condition()?)
            }
            "finish" => StmtKind::Finish,
            "stop" => {
                self.expect_word("loop")?;
                StmtKind::StopLoop
            }
            "skip" => StmtKind::Skip,
            _ => unreachable!("keyword list and match are kept in sync"),
        })
    }

    /// Optional `, capped at <term>` / `, floored at <term>` suffix.
    fn limit(&mut self) -> PResult<Limit> {
        let mut limit = Limit::default();
        while matches!(self.peek(), Tok::Comma) && (self.is_word_at(1, "capped") || self.is_word_at(1, "floored")) {
            self.advance();
            let which = self.word("`capped` or `floored`")?;
            self.expect_word("at")?;
            let t = self.term()?;
            if which == "capped" {
                limit.capped = Some(t);
            } else {
                limit.floored = Some(t);
            }
        }
        Ok(limit)
    }

    /// `key:value key:value ...` call arguments.
    fn args(&mut self) -> PResult<Vec<Arg>> {
        let mut args = Vec::new();
        while matches!(self.peek(), Tok::Word(_)) && matches!(self.peek_at(1), Tok::Colon) {
            let name = self.word("an argument name")?;
            self.advance(); // `:`
            args.push(Arg { name, value: self.value()? });
        }
        Ok(args)
    }

    // ---- conditions ----

    fn condition(&mut self) -> PResult<Condition> {
        let left = self.value()?;
        if !self.at_comparison() {
            return self.err(format!(
                "a condition must compare something, like `if Switch is true`; found {} after the value (`if X:` on its own is not allowed)",
                describe(self.peek())
            ));
        }
        self.condition_from(left)
    }

    fn condition_from(&mut self, left: Value) -> PResult<Condition> {
        let first = self.comparison_after(left)?;
        let mut rest = Vec::new();
        let mut joiner: Option<Joiner> = None;
        loop {
            let j = if self.is_word("and") {
                Joiner::And
            } else if self.is_word("or") {
                Joiner::Or
            } else {
                break;
            };
            if joiner.is_some_and(|prev| prev != j) {
                return self.err(
                    "`and` and `or` cannot be mixed in one condition; name one part with `remember` first (see SPEC 4.2)",
                );
            }
            joiner = Some(j);
            self.advance();
            let l = self.value()?;
            if !self.at_comparison() {
                return self.err(format!("expected `is` to continue the condition, found {}", describe(self.peek())));
            }
            rest.push(self.comparison_after(l)?);
        }
        Ok(Condition { first, rest, joiner })
    }

    /// True at `is ...`, `starts pressing ...`, or `stops pressing ...`.
    fn at_comparison(&self) -> bool {
        self.is_word("is")
            || self.is_word("contains")
            || (self.is_word("does") && self.is_word_at(1, "not") && self.is_word_at(2, "contain"))
            || ((self.is_word("starts") || self.is_word("stops")) && self.is_word_at(1, "pressing"))
    }

    fn comparison_after(&mut self, left: Value) -> PResult<Comparison> {
        if self.is_word("starts") || self.is_word("stops") {
            let kind = if self.is_word("starts") { PressKind::Started } else { PressKind::Stopped };
            self.advance();
            self.expect_word("pressing")?;
            return self.pressing(left, kind);
        }
        if self.is_word("contains") || self.is_word("does") {
            let negated = self.eat_word("does");
            if negated {
                self.expect_word("not")?;
                self.expect_word("contain")?;
            } else {
                self.advance();
            }
            let item = self.value()?;
            return Ok(Comparison::Contains { list: left, item, negated });
        }
        self.expect_word("is")?;
        let negated = self.eat_word("not");
        if self.is_word("pressing") {
            self.advance();
            return self.pressing(left, if negated { PressKind::NotHeld } else { PressKind::Held });
        }
        if self.is_word("missing") || self.is_word("present") {
            let missing = self.is_word("missing");
            if negated {
                return self.err(if missing {
                    "write `is present` instead of `is not missing`"
                } else {
                    "write `is missing` instead of `is not present`"
                });
            }
            self.advance();
            return Ok(Comparison::Presence { value: left, missing });
        }
        let op = if self.eat_word("above") {
            CmpOp::Above
        } else if self.eat_word("below") {
            CmpOp::Below
        } else if self.eat_word("at") {
            if self.eat_word("least") {
                CmpOp::AtLeast
            } else if self.eat_word("most") {
                CmpOp::AtMost
            } else {
                return self.err(format!("expected `least` or `most` after `at`, found {}", describe(self.peek())));
            }
        } else if negated {
            CmpOp::NotEq
        } else {
            CmpOp::Eq
        };
        if negated && !matches!(op, CmpOp::NotEq) {
            return self.err("`is not` can only be followed by a value or `pressing`");
        }
        let right = self.value()?;
        Ok(Comparison::Compare { left, op, right })
    }

    fn pressing(&mut self, left: Value, kind: PressKind) -> PResult<Comparison> {
        let system = match left {
            Value::Term(Term::Name(n)) => n,
            _ => return self.err("`pressing` must follow a system name, like `Input is pressing moveForward`"),
        };
        let input = self.word("an input name")?;
        Ok(Comparison::Pressing { system, input, kind })
    }

    // ---- values ----

    fn value_or_condition(&mut self) -> PResult<Value> {
        let v = self.value()?;
        if self.at_comparison() {
            Ok(Value::Condition(Box::new(self.condition_from(v)?)))
        } else {
            Ok(v)
        }
    }

    fn value(&mut self) -> PResult<Value> {
        if let Some(v) = self.phrase()? {
            return Ok(v);
        }
        let t = self.term()?;
        if self.is_word("per") && self.is_word_at(1, "second") {
            self.advance();
            self.advance();
            return Ok(Value::PerSecond(t));
        }
        if self.is_word("of") && self.is_word_at(1, "the") && self.is_word_at(2, "way") && self.is_word_at(3, "from") {
            for _ in 0..4 {
                self.advance();
            }
            let from = self.term()?;
            self.expect_word("to")?;
            let to = self.term()?;
            return Ok(Value::Lerp { t, from, to });
        }
        if self.is_word("joined") && self.is_word_at(1, "with") {
            self.advance();
            self.advance();
            let right = self.term()?;
            return Ok(Value::JoinedWith { left: t, right });
        }
        if let Some(op) = self.arith_op() {
            let right = self.term()?;
            return Ok(Value::Arith { left: t, op, right });
        }
        if self.is_word("if") {
            self.advance();
            let cond = self.condition()?;
            if !self.is_word("otherwise") {
                return self.err(format!("expected `otherwise` and the other value, found {}", describe(self.peek())));
            }
            self.advance();
            let no = self.term()?;
            return Ok(Value::Pick { yes: t, cond: Box::new(cond), no });
        }
        Ok(Value::Term(t))
    }

    fn arith_op(&mut self) -> Option<ArithOp> {
        if self.eat_word("plus") {
            Some(ArithOp::Plus)
        } else if self.eat_word("minus") {
            Some(ArithOp::Minus)
        } else if self.eat_word("times") {
            Some(ArithOp::Times)
        } else if self.is_word("divided") && self.is_word_at(1, "by") {
            self.advance();
            self.advance();
            Some(ArithOp::DividedBy)
        } else if self.is_word("remainder")
            && self.is_word_at(1, "after")
            && self.is_word_at(2, "dividing")
            && self.is_word_at(3, "by")
        {
            for _ in 0..4 {
                self.advance();
            }
            Some(ArithOp::Remainder)
        } else {
            None
        }
    }

    /// Phrase expressions (SPEC 4.1). A phrase is recognized by its leading words,
    /// so a name like `count` or `text` still works as an ordinary name.
    fn phrase(&mut self) -> PResult<Option<Value>> {
        let w = match self.peek() {
            Tok::Word(w) => w.clone(),
            _ => return Ok(None),
        };
        if let Some(v) = self.table_phrase(&w)? {
            return Ok(Some(v));
        }
        if w == "new" && matches!(self.peek_at(1), Tok::Word(t) if self.structs.contains(t)) {
            self.advance();
            let ty = self.word("a struct name")?;
            return Ok(Some(Value::NewStruct { ty, args: self.args()? }));
        }
        if w == "row" {
            let save = self.pos;
            self.advance();
            if let Ok(key) = self.term() {
                if self.eat_word("of") {
                    if let Tok::Word(table) = self.peek().clone() {
                        self.advance();
                        return Ok(Some(Value::RowOf { key, table }));
                    }
                }
            }
            self.pos = save;
            return Ok(None);
        }
        if w == "cast" {
            let save = self.pos;
            self.advance();
            if let Ok(value) = self.term() {
                if self.eat_word("to") {
                    let file = self.word("a file name after `to`")?;
                    return Ok(Some(Value::CastTo { value, file }));
                }
            }
            self.pos = save;
            return Ok(None);
        }
        if w == "format" && matches!(self.peek_at(1), Tok::Text(_)) {
            self.advance();
            let line = self.line();
            let col = self.cur().col;
            let Tok::Text(template) = self.peek().clone() else { unreachable!() };
            self.advance();
            return Ok(Some(Value::Format { parts: self.format_parts(&template, line, col)? }));
        }
        Ok(Some(match w.as_str() {
            "distance" if self.is_word_at(1, "from") => {
                self.advance();
                self.advance();
                let from = self.term()?;
                self.expect_word("to")?;
                Value::Distance { from, to: self.term()? }
            }
            "count" if self.is_word_at(1, "of") => {
                self.advance();
                self.advance();
                Value::CountOf(self.term()?)
            }
            "text" | "decimal" | "integer" if self.is_word_at(1, "of") => {
                self.advance();
                self.advance();
                let t = self.term()?;
                match w.as_str() {
                    "text" => Value::TextOf(t),
                    "decimal" => Value::DecimalOf(t),
                    _ => Value::IntegerOf(t),
                }
            }
            "absolute" if self.is_word_at(1, "value") && self.is_word_at(2, "of") => {
                self.advance();
                self.advance();
                self.advance();
                Value::AbsoluteValueOf(self.term()?)
            }
            "result" if self.is_word_at(1, "of") => {
                self.advance();
                self.advance();
                let action = self.word("an action name")?;
                Value::ResultOf { action, args: self.args()? }
            }
            "axis" if matches!(self.peek_at(1), Tok::Word(_)) && self.is_word_at(2, "of") && matches!(self.peek_at(3), Tok::Word(_)) => {
                self.advance();
                let input = self.word("an input name")?;
                self.advance(); // `of`
                let system = self.word("a system name")?;
                Value::Axis { system, input }
            }
            "time" if self.is_word_at(1, "since") && self.is_word_at(2, "start") => {
                for _ in 0..3 {
                    self.advance();
                }
                Value::TimeSinceStart
            }
            "item" if self.is_word_at(2, "of") => {
                self.advance();
                let index = self.term()?;
                self.expect_word("of")?;
                Value::ItemOf { index, list: self.term()? }
            }
            "vector" if matches!(self.peek_at(1), Tok::Word(_)) && matches!(self.peek_at(2), Tok::Colon) => {
                self.advance();
                let axis = |p: &mut Parser, name: &str| -> PResult<Term> {
                    p.expect_word(name)?;
                    p.expect(Tok::Colon, &format!("`:` after `{name}`"))?;
                    p.term()
                };
                let x = axis(self, "x")?;
                let y = axis(self, "y")?;
                let z = axis(self, "z")?;
                Value::Vector { x, y, z }
            }
            _ => return Ok(None),
        }))
    }

    /// A phrase from the table in `phrases.rs`. It counts only when every word of its shape is there, so a
    /// name that is also a phrase's first word (`length`, `map`) is still a name; once the first two pieces
    /// have matched it is committed to, and anything missing after that is an error that says what.
    fn table_phrase(&mut self, first: &str) -> PResult<Option<Value>> {
        for def in crate::phrases::starting_with(first) {
            let save = self.pos;
            let mut args: Vec<Term> = Vec::new();
            let mut committed = false;
            let mut failed = false;
            for (i, piece) in def.pieces.iter().enumerate() {
                let step: PResult<()> = match piece {
                    crate::phrases::Piece::Word(w) => {
                        if self.eat_word(w) {
                            Ok(())
                        } else {
                            self.err(format!("expected `{w}`, found {}", describe(self.peek())))
                        }
                    }
                    crate::phrases::Piece::Key(k) => {
                        if self.is_word(k) && matches!(self.peek_at(1), Tok::Colon) {
                            self.advance();
                            self.advance();
                            Ok(())
                        } else {
                            self.err(format!("expected `{k}:`, found {}", describe(self.peek())))
                        }
                    }
                    crate::phrases::Piece::Arg(_) => self.term().map(|t| args.push(t)),
                };
                if let Err(e) = step {
                    if committed {
                        return Err(e);
                    }
                    failed = true;
                    break;
                }
                if i == 1 {
                    committed = true;
                }
            }
            if !failed {
                return Ok(Some(Value::Phrase { node: def.node, args }));
            }
            self.pos = save;
        }
        Ok(None)
    }

    /// The pieces of `format "Score: {Score}"`: text, and the names or fields inside the braces.
    fn format_parts(&self, template: &str, line: usize, col: usize) -> PResult<Vec<FormatPart>> {
        let mut parts = Vec::new();
        let mut text = String::new();
        let mut chars = template.chars().peekable();
        while let Some(c) = chars.next() {
            match c {
                '{' => {
                    let mut inner = String::new();
                    let mut closed = false;
                    for d in chars.by_ref() {
                        if d == '}' {
                            closed = true;
                            break;
                        }
                        inner.push(d);
                    }
                    if !closed {
                        return Err(PropulsionError::new(line, col, "a `{` in `format` text needs a closing `}`"));
                    }
                    if !text.is_empty() {
                        parts.push(FormatPart::Text(std::mem::take(&mut text)));
                    }
                    parts.push(FormatPart::Value(self.term_in_text(inner.trim(), line, col)?));
                }
                '}' => return Err(PropulsionError::new(line, col, "a `}` in `format` text needs an opening `{` before it")),
                other => text.push(other),
            }
        }
        if !text.is_empty() {
            parts.push(FormatPart::Text(text));
        }
        Ok(parts)
    }

    /// The name or field written between the braces of a `format`.
    fn term_in_text(&self, inner: &str, line: usize, col: usize) -> PResult<Term> {
        let bad = || PropulsionError::new(line, col, format!("`{{{inner}}}` in `format` text should hold a name or a field, like `{{Score}}` or `{{Transform.Loc.X}}`"));
        if inner.is_empty() {
            return Err(bad());
        }
        let lexed = lex_with_comments(inner).map_err(|_| bad())?;
        let mut sub = Parser { toks: lexed.tokens, pos: 0, comments: lexed.comments, enums: Vec::new(), structs: Vec::new() };
        if !matches!(sub.peek(), Tok::Word(_)) {
            return Err(bad());
        }
        let term = sub.term().map_err(|_| bad())?;
        // A word first is a name or a path; the whole text must have been used.
        if !matches!(sub.peek(), Tok::Newline) || matches!(term, Term::Text(_) | Term::Int(_) | Term::Decimal(_) | Term::Duration(_) | Term::Bool(_)) {
            return Err(bad());
        }
        Ok(term)
    }

    fn term(&mut self) -> PResult<Term> {
        match self.peek().clone() {
            Tok::Number(s) => {
                self.advance();
                let is_decimal = s.contains('.');
                let parsed = s.parse::<f64>().unwrap_or(0.0);
                if self.is_word("seconds") || self.is_word("second") {
                    self.advance();
                    Ok(Term::Duration(parsed))
                } else if is_decimal {
                    Ok(Term::Decimal(parsed))
                } else {
                    Ok(Term::Int(s.parse::<i64>().unwrap_or(0)))
                }
            }
            Tok::Text(s) => {
                self.advance();
                Ok(Term::Text(s))
            }
            Tok::Word(w) => {
                self.advance();
                match w.as_str() {
                    "true" => return Ok(Term::Bool(true)),
                    "false" => return Ok(Term::Bool(false)),
                    _ => {}
                }
                let mut segments = vec![w];
                while matches!(self.peek(), Tok::Dot) {
                    self.advance();
                    segments.push(self.word("a field name after `.`")?);
                }
                if segments.len() == 1 {
                    return Ok(Term::Name(segments.remove(0)));
                }
                // `Progress.X of the way from ...` is a blend, not "X of an entity called `the`".
                let blend_follows = self.is_word("of") && self.is_word_at(1, "the") && self.is_word_at(2, "way");
                let owner = if !blend_follows && self.eat_word("of") {
                    Some(self.word("an entity name after `of`")?)
                } else {
                    None
                };
                Ok(Term::Path { segments, owner })
            }
            other => self.err(format!("expected a value, found {}", describe(&other))),
        }
    }
}

// ---- header pair helpers ----

fn find<'a>(pairs: &'a Pairs, key: &str) -> Option<&'a PairVal> {
    pairs.iter().find(|(k, _)| k == key).map(|(_, v)| v)
}

fn check_keys(pairs: &Pairs, allowed: &[&str], what: &str, line: usize) -> PResult<()> {
    for (k, _) in pairs {
        if !allowed.contains(&k.as_str()) {
            return Err(PropulsionError::new(
                line,
                1,
                format!("`{k}:` isn't used in `{what}`; I expected: {}", allowed.join(", ")),
            ));
        }
    }
    Ok(())
}

fn name_of(pairs: &Pairs, key: &str, line: usize) -> PResult<String> {
    match find(pairs, key) {
        Some(PairVal::Term(Term::Name(n))) => Ok(n.clone()),
        _ => Err(PropulsionError::new(line, 1, format!("expected a name for `{key}:`"))),
    }
}

fn text_of(pairs: &Pairs, key: &str, line: usize) -> PResult<String> {
    match find(pairs, key) {
        Some(PairVal::Term(Term::Text(s))) => Ok(s.clone()),
        _ => Err(PropulsionError::new(line, 1, format!("`{key}:` must be quoted text, like `{key}:\"example\"`"))),
    }
}

fn collect_parts(pairs: &Pairs) -> Parts {
    pairs
        .iter()
        .filter(|(k, _)| matches!(k.as_str(), "x" | "y" | "z" | "w" | "r" | "g" | "b" | "a"))
        .filter_map(|(k, v)| match v {
            PairVal::Term(t) => Some((k.clone(), t.clone())),
            _ => None,
        })
        .collect()
}
