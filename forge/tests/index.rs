//! The symbol index (`forge::index`) is built from text, line by line, so it can work on broken
//! files. These tests keep it honest against the real parser, and check scopes and references.

use forge::ast::*;
use forge::index::{ExternalKind, Index, Section, SymbolKind};
use std::path::{Path, PathBuf};

fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("..")
}

fn forge_files() -> Vec<PathBuf> {
    fn walk(dir: &Path, out: &mut Vec<PathBuf>) {
        for entry in std::fs::read_dir(dir).into_iter().flatten().flatten() {
            let path = entry.path();
            if path.is_dir() {
                if !matches!(path.file_name().and_then(|n| n.to_str()), Some("target" | "node_modules" | ".git")) {
                    walk(&path, out);
                }
            } else if path.extension().is_some_and(|e| e == "forge") {
                out.push(path);
            }
        }
    }
    let mut out = Vec::new();
    walk(&root(), &mut out);
    out.sort();
    out
}

fn from_index(src: &str) -> Vec<(String, String, usize)> {
    let index = Index::build(src);
    let mut v: Vec<_> = index.symbols.iter().map(|s| (s.kind.label().to_string(), s.name.clone(), s.decl.line)).collect();
    v.sort();
    v
}

fn from_ast(file: &File) -> Vec<(String, String, usize)> {
    let mut v: Vec<(String, String, usize)> = Vec::new();
    let mut push = |kind: SymbolKind, name: &str, line: usize| v.push((kind.label().to_string(), name.to_string(), line));

    for d in &file.header {
        match &d.kind {
            DeclKind::Variable { name, .. } => push(SymbolKind::Variable, name, d.line),
            DeclKind::Constant { name, .. } => push(SymbolKind::Constant, name, d.line),
            DeclKind::NeedsComponent(n) => push(SymbolKind::Component, n, d.line),
            DeclKind::NeedsSystem(n) => push(SymbolKind::System, n, d.line),
            DeclKind::NeedsEntity { name, .. } => push(SymbolKind::Entity, name, d.line),
            DeclKind::NeedsEntities { name, .. } => push(SymbolKind::Entities, name, d.line),
            DeclKind::NeedsFile { alias: Some(a), .. } => push(SymbolKind::File, a, d.line),
            DeclKind::Event(n) => push(SymbolKind::Event, n, d.line),
            _ => {}
        }
    }
    fn walk(body: &[Stmt], push: &mut dyn FnMut(SymbolKind, &str, usize)) {
        for s in body {
            match &s.kind {
                StmtKind::Remember { name, .. } | StmtKind::Spawn { name, .. } => push(SymbolKind::Local, name, s.line),
                StmtKind::ForRange { var, body, .. } | StmtKind::ForEach { var, body, .. } => {
                    push(SymbolKind::Loop, var, s.line);
                    walk(body, push);
                }
                StmtKind::If { branches, otherwise, .. } => {
                    for (_, b) in branches {
                        walk(b, push);
                    }
                    if let Some(b) = otherwise {
                        walk(b, push);
                    }
                }
                StmtKind::Repeat { body, .. } | StmtKind::While { body, .. } | StmtKind::After { body, .. } | StmtKind::Through { body, .. } => walk(body, push),
                StmtKind::Animate { var, body, .. } => {
                    push(SymbolKind::Loop, var, s.line);
                    walk(body, push);
                }
                StmtKind::OnlyOnce { name, body } | StmtKind::FirstTimes { name, body, .. } => {
                    if let Some(n) = name {
                        push(SymbolKind::Latch, n, s.line);
                    }
                    walk(body, push);
                }
                StmtKind::Alternate { name, first, second } => {
                    if let Some(n) = name {
                        push(SymbolKind::Latch, n, s.line);
                    }
                    walk(first, push);
                    walk(second, push);
                }
                StmtKind::Choose { cases, otherwise, .. } => {
                    for c in cases {
                        walk(&c.body, push);
                    }
                    if let Some(b) = otherwise {
                        walk(b, push);
                    }
                }
                _ => {}
            }
        }
    }
    for item in &file.implement {
        match &item.kind {
            ItemKind::Action { name, params, body, .. } => {
                push(SymbolKind::Action, name, item.line);
                for p in params {
                    push(SymbolKind::Parameter, &p.name, item.line);
                }
                walk(body, &mut push);
            }
            ItemKind::Handler { trigger, body } => {
                match trigger {
                    Trigger::Touches { name, .. } | Trigger::StopsTouching { name, .. } | Trigger::Sends { name, .. } | Trigger::Hits { name, .. } => {
                        push(SymbolKind::Local, name, item.line)
                    }
                    Trigger::Damaged { amount, source } => {
                        push(SymbolKind::Local, amount, item.line);
                        if let Some(s) = source {
                            push(SymbolKind::Local, s, item.line);
                        }
                    }
                    _ => {}
                }
                walk(body, &mut push);
            }
        }
    }
    v.sort();
    v
}

#[test]
fn the_index_finds_exactly_the_names_the_parser_does() {
    let files = forge_files();
    assert!(files.len() > 40, "expected to find the examples, behaviors and fail tests, found {}", files.len());
    let mut compared = 0;
    for path in files {
        let src = std::fs::read_to_string(&path).unwrap();
        // Files that don't parse (some of the must-fail tests) are covered by the no-panic test below.
        let Ok(ast) = forge::parse(&src) else { continue };
        assert_eq!(from_index(&src), from_ast(&ast), "{}", path.display());
        compared += 1;
    }
    assert!(compared > 40, "only {compared} files parsed");
}

#[test]
fn every_declaration_span_covers_its_name() {
    for path in forge_files() {
        let src = std::fs::read_to_string(&path).unwrap();
        let index = Index::build(&src);
        let lines: Vec<&str> = src.lines().collect();
        for s in &index.symbols {
            let text: String = lines[s.decl.line - 1].chars().skip(s.decl.col - 1).take(s.decl.len).collect();
            assert_eq!(text, s.name, "{}:{}", path.display(), s.decl.line);
        }
    }
}

#[test]
fn broken_files_do_not_stop_the_index() {
    // Parentheses and spaces are errors for the parser, but the index should still work.
    let src = "header:\n\tvariable name:Health type:integer\n\nimplement:\n    every frame:\n\t\tmake Health (1)\n\t\tmake Health 2\n";
    let index = Index::build(src);
    assert_eq!(index.symbols.len(), 1);
    assert!(index.references(0).count() >= 2);
    for path in forge_files() {
        Index::build(&std::fs::read_to_string(path).unwrap());
    }
}

fn example(name: &str) -> (String, Index) {
    let src = std::fs::read_to_string(root().join("examples").join(name)).unwrap();
    let index = Index::build(&src);
    (src, index)
}

fn find(index: &Index, name: &str) -> usize {
    index.symbols.iter().position(|s| s.name == name).unwrap_or_else(|| panic!("no symbol {name}"))
}

#[test]
fn a_variable_has_one_declaration_and_every_use() {
    let (src, index) = example("coin.forge");
    let id = find(&index, "Collected");
    let uses: Vec<usize> = index.references(id).filter(|o| !o.declaration).map(|o| o.span.line).collect();
    let expected: Vec<usize> =
        src.lines().enumerate().filter(|(_, l)| !l.contains("variable") && l.contains("Collected")).map(|(i, _)| i + 1).collect();
    assert_eq!(uses, expected);
    assert_eq!(index.references(id).filter(|o| o.declaration).count(), 1);
}

#[test]
fn a_declaration_knows_its_type_value_and_kind() {
    let (_, index) = example("enemy.forge");
    let speed = index.symbol(find(&index, "Speed"));
    assert_eq!((speed.kind, speed.ty.as_deref(), speed.value.as_deref()), (SymbolKind::Constant, Some("decimal"), Some("3.0")));
    let waypoints = index.symbol(find(&index, "Waypoints"));
    assert_eq!(waypoints.ty.as_deref(), Some("list of vector3"));
    let player = index.symbol(find(&index, "Player"));
    assert_eq!((player.kind, player.file.as_deref(), player.tag.as_deref()), (SymbolKind::Entity, Some("player.forge"), Some("player")));
    let take = index.symbol(find(&index, "TakeDamage"));
    assert_eq!(take.kind, SymbolKind::Action);
    assert_eq!(take.params.len(), 1);
    assert_eq!((take.params[0].name.as_str(), take.params[0].ty.as_str()), ("amount", "integer"));
}

#[test]
fn an_action_remembers_what_it_gives() {
    let (_, index) = example("coin.forge");
    let get = index.symbol(find(&index, "GetValue"));
    assert_eq!(get.gives.as_deref(), Some("integer"));
}

#[test]
fn a_parameter_is_only_visible_inside_its_action() {
    let (src, index) = example("enemy.forge");
    let amount = find(&index, "amount");
    let action_line = src.lines().position(|l| l.starts_with("\taction TakeDamage")).unwrap() + 1;
    let (from, to) = index.symbol(amount).scope;
    assert_eq!(from, action_line);
    assert_eq!(to, src.lines().count());
    // ... and not in the action above it.
    let patrol_line = src.lines().position(|l| l.starts_with("\taction Patrol")).unwrap() + 1;
    assert!(index.visible_at(patrol_line + 1).iter().all(|(_, s)| s.name != "amount"));
}

#[test]
fn a_remembered_name_ends_with_its_block() {
    let src = "header:\n\tvariable name:A type:integer\n\nimplement:\n\tevery frame:\n\t\tif A is 1:\n\t\t\tremember Inner A\n\t\t\tmake A Inner\n\t\tmake A Inner\n\t\tremember Outer A\n\t\tmake A Outer\n";
    let index = Index::build(src);
    let inner = find(&index, "Inner");
    let lines: Vec<usize> = index.references(inner).map(|o| o.span.line).collect();
    // The declaration and the use inside the `if`; the use after the block is not a reference.
    assert_eq!(lines, vec![7, 8]);
    let outer = find(&index, "Outer");
    assert_eq!(index.references(outer).count(), 2);
}

#[test]
fn a_loop_variable_covers_only_the_loop() {
    let src = "header:\n\tvariable name:A type:integer\n\nimplement:\n\tevery frame:\n\t\tfor Index from 0 to 4:\n\t\t\tadd Index to A\n\t\tadd Index to A\n";
    let index = Index::build(src);
    let id = find(&index, "Index");
    assert_eq!(index.symbol(id).kind, SymbolKind::Loop);
    assert_eq!(index.symbol(id).ty.as_deref(), Some("integer"));
    assert_eq!(index.references(id).map(|o| o.span.line).collect::<Vec<_>>(), vec![6, 7]);
}

#[test]
fn a_name_made_by_as_covers_its_handler() {
    let src = "header:\n\tneeds file:\"coin.forge\" as Coin\n\nimplement:\n\twhen Self touches Coin as Pickup:\n\t\tdestroy Pickup\n\n\tevery frame:\n\t\tdestroy Pickup\n";
    let index = Index::build(src);
    let pickup = index.symbol(find(&index, "Pickup"));
    assert_eq!(pickup.file.as_deref(), Some("coin.forge"));
    assert_eq!(pickup.scope, (5, 6));
    let coin = find(&index, "Coin");
    assert_eq!(index.references(coin).count(), 2, "declaration and the `touches` line");
}

#[test]
fn calls_on_other_files_are_external_members() {
    let (_, index) = example("enemy.forge");
    let take = index.externals.iter().find(|e| e.member == "TakeDamage").expect("do TakeDamage on Player");
    assert_eq!(take.kind, ExternalKind::Action);
    assert_eq!(index.symbol(take.owner).name, "Player");
    // `do Patrol` is this file's own action, so it is an ordinary reference.
    assert!(index.externals.iter().all(|e| e.member != "Patrol"));
    let patrol = find(&index, "Patrol");
    assert_eq!(index.references(patrol).filter(|o| !o.declaration).count(), 1);
}

#[test]
fn system_calls_resolve_their_owner() {
    let (_, index) = example("coin.forge");
    let play = index.externals.iter().find(|e| e.member == "Play").unwrap();
    let owner = index.symbol(play.owner);
    assert_eq!((owner.kind, owner.name.as_str()), (SymbolKind::System, "Audio"));
}

#[test]
fn a_phrase_word_is_not_a_use_of_a_name_with_the_same_spelling() {
    // hud.forge has a parameter called `count`, and the phrase `count of` exists too.
    let src = "header:\n\tvariable name:Items type:list of integer\n\nimplement:\n\taction Show count:integer:\n\t\tmake Items count\n\t\tremember Total count of Items\n";
    let index = Index::build(src);
    let count = find(&index, "count");
    let lines: Vec<usize> = index.references(count).map(|o| o.span.line).collect();
    assert_eq!(lines, vec![5, 6], "the `count of` phrase on line 7 is not a use");
}

#[test]
fn field_paths_keep_each_part() {
    let (src, index) = example("coin.forge");
    let (path, _) = index
        .paths
        .iter()
        .map(|p| (p, ()))
        .find(|(p, _)| p.segments.len() == 3)
        .expect("Transform.Rot.Y");
    let names: Vec<&str> = path.segments.iter().map(|(n, _)| n.as_str()).collect();
    assert_eq!(names, vec!["Transform", "Rot", "Y"]);
    let line = path.segments[0].1.line;
    let col = path.segments[2].1.col;
    assert!(index.path_at(line, col).is_some());
    assert!(src.lines().nth(line - 1).unwrap().contains("Transform.Rot.Y"));
    // The first part is a use of the `needs component` declaration; the fields are not names.
    let transform = find(&index, "Transform");
    assert!(index.references(transform).any(|o| !o.declaration));
}

#[test]
fn file_paths_are_recorded_for_navigation() {
    let (_, index) = example("enemy.forge");
    let r = index.file_refs.iter().find(|f| f.path == "player.forge").expect("file:\"player.forge\"");
    assert!(index.file_ref_at(r.span.line, r.span.col + 1).is_some());
}

#[test]
fn comments_above_and_after_a_declaration_become_its_doc() {
    let src = "header:\n\t// How fast it turns.\n\tconstant name:Spin type:decimal value:1.0 // degrees\n\t// node 3: Set Variable\n\tvariable name:Other type:integer\n\nimplement:\n\tevery frame:\n\t\tmake Other 1\n";
    let index = Index::build(src);
    assert_eq!(index.symbol(find(&index, "Spin")).doc.as_deref(), Some("How fast it turns.\ndegrees"));
    assert_eq!(index.symbol(find(&index, "Other")).doc, None, "node markers are not documentation");
}

#[test]
fn handlers_and_sections_are_known() {
    let (src, index) = example("enemy.forge");
    let texts: Vec<&str> = index.handlers.iter().map(|h| h.text.as_str()).collect();
    assert!(texts.contains(&"when created:") && texts.contains(&"every frame:") && texts.contains(&"action Patrol:"), "{texts:?}");
    let header_line = src.lines().position(|l| l.contains("variable name:Health")).unwrap() + 1;
    let body_line = src.lines().position(|l| l.contains("send Defeated")).unwrap() + 1;
    assert_eq!(index.section_at(header_line), Section::Header);
    assert_eq!(index.section_at(body_line), Section::Implement);
}

#[test]
fn names_in_format_text_and_the_names_the_new_statements_make_are_found() {
    let src = "header:\n\tvariable name:Score type:integer value:0\n\nimplement:\n\twhen damaged as Hit from Attacker:\n\t\tonly once as Intro:\n\t\t\tprint format \"Score {Score} after {Hit} from {Attacker}\"\n\t\tanimate Progress from 0.0 to 1.0 over 1 second:\n\t\t\tprint text of Progress\n\t\treset Intro\n";
    let index = Index::build(src);
    let find = |name: &str| index.symbols.iter().position(|s| s.name == name).unwrap_or_else(|| panic!("no symbol {name}"));

    // `{Score}` inside the text is a use of Score, found where it sits in the line.
    let score = find("Score");
    let uses: Vec<_> = index.references(score).filter(|o| !o.declaration).collect();
    assert_eq!(uses.len(), 1);
    let line = src.lines().nth(uses[0].span.line - 1).unwrap();
    let at: String = line.chars().skip(uses[0].span.col - 1).take(uses[0].span.len).collect();
    assert_eq!(at, "Score");
    assert!(line.contains("{Score}") && !line[..uses[0].span.col - 1].ends_with("print format \""), "it is the one in the braces: {line}");

    // The names a `when damaged` handler gives, and where they can be used.
    assert_eq!(index.symbol(find("Hit")).ty.as_deref(), Some("decimal"));
    assert_eq!(index.symbol(find("Attacker")).ty.as_deref(), Some("entity"));
    assert_eq!(index.references(find("Hit")).count(), 2, "its declaration and the use in the text");

    // A named `only once` can be reset from anywhere; an `animate` name only inside it.
    let intro = find("Intro");
    assert_eq!(index.symbol(intro).kind, SymbolKind::Latch);
    assert_eq!(index.references(intro).filter(|o| !o.declaration).count(), 1, "`reset Intro`");
    let progress = index.symbol(find("Progress"));
    assert_eq!(progress.kind, SymbolKind::Loop);
    assert!(progress.scope.1 < src.lines().count(), "{:?}", progress.scope);
}

#[test]
fn a_name_that_is_also_the_start_of_a_phrase_is_still_a_name() {
    // `length` and `clamp` are variables here; `length of Dir` and `clamp Speed between 0 and 5` are phrases.
    let src = "header:\n\tvariable name:length type:integer value:1\n\tvariable name:Speed type:integer value:1\n\tvariable name:Dir type:vector3\n\nimplement:\n\twhen created:\n\t\tmake length 2\n\t\tprint length of Dir\n\t\tmake Speed clamp Speed between 0 and 5\n";
    let file = forge::parse(src).unwrap();
    let forge::ast::ItemKind::Handler { body, .. } = &file.implement[0].kind else { panic!() };
    assert!(matches!(&body[1].kind, forge::ast::StmtKind::Print(forge::ast::Value::Phrase { node: "Vector Length", .. })));
    assert!(matches!(&body[2].kind, forge::ast::StmtKind::Make { value: forge::ast::Value::Phrase { node: "Clamp", .. }, .. }));
    let index = Index::build(src);
    let length = index.symbols.iter().position(|s| s.name == "length").unwrap();
    assert_eq!(index.references(length).filter(|o| !o.declaration).count(), 1, "only `make length 2` uses the variable");
}
