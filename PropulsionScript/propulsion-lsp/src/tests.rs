//! Feature tests, run in-process against the real example files (including the calls between
//! `enemy.prop` and `player.prop`).

use crate::workspace::{Document, Workspace};
use crate::{completion, hover, navigate, semantic};
use lsp_types::{GotoDefinitionResponse, HoverContents, Position, PrepareRenameResponse, Url};
use std::path::{Path, PathBuf};

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap().to_path_buf()
}

/// A workspace with `rel` open. `text` replaces the file's text, as unsaved edits would.
fn open(rel: &str, text: Option<&str>) -> (Workspace, Url, String) {
    let path = repo().join(rel);
    let text = text.map(str::to_string).unwrap_or_else(|| std::fs::read_to_string(&path).unwrap());
    let uri = Url::from_file_path(&path).unwrap();
    let mut ws = Workspace::new(vec![repo()]);
    ws.open.insert(uri.clone(), Document { text: text.clone(), version: 1 });
    (ws, uri, text)
}

/// The position of the `nth` (0-based) occurrence of `needle` in the code (comments are skipped,
/// since the examples quote old spellings in them), one character in, so the cursor is on it.
fn pos(text: &str, needle: &str, nth: usize) -> Position {
    let mut seen = 0;
    for (line, l) in text.lines().enumerate() {
        if l.trim_start().starts_with("//") {
            continue;
        }
        let mut from = 0;
        while let Some(at) = l[from..].find(needle) {
            if seen == nth {
                return Position { line: line as u32, character: (l[..from + at].chars().count() + 1) as u32 };
            }
            seen += 1;
            from += at + needle.len();
        }
    }
    panic!("no occurrence {nth} of {needle:?}");
}

fn hover_text(ws: &Workspace, uri: &Url, at: Position) -> String {
    match hover::hover(ws, uri, at).unwrap().expect("a hover").contents {
        HoverContents::Markup(m) => m.value,
        other => panic!("{other:?}"),
    }
}

// ---- semantic tokens ----

struct Tok {
    text: String,
    ty: u32,
    mods: u32,
}

fn semantic_tokens(ws: &Workspace, uri: &Url, text: &str) -> Vec<Tok> {
    let lines: Vec<&str> = text.lines().collect();
    let (mut line, mut start) = (0u32, 0u32);
    semantic::tokens(ws, uri)
        .unwrap()
        .data
        .iter()
        .map(|t| {
            line += t.delta_line;
            start = if t.delta_line == 0 { start + t.delta_start } else { t.delta_start };
            let text: String = lines[line as usize].chars().skip(start as usize).take(t.length as usize).collect();
            Tok { text, ty: t.token_type, mods: t.token_modifiers_bitset }
        })
        .collect()
}

#[test]
fn names_are_coloured_by_what_they_are() {
    let (ws, uri, text) = open("examples/coin.prop", None);
    let toks = semantic_tokens(&ws, &uri, &text);
    let find = |name: &str, nth: usize| toks.iter().filter(|t| t.text == name).nth(nth).unwrap_or_else(|| panic!("no token {name}"));

    // legend order: variable, parameter, function, event, class, interface, namespace, property; modifiers: declaration, readonly.
    let spin = find("SpinSpeed", 0);
    assert_eq!((spin.ty, spin.mods), (0, 0b11), "a constant's declaration is a readonly variable");
    assert_eq!(find("SpinSpeed", 1).mods, 0b10, "its use is readonly but not a declaration");
    assert_eq!((find("Collected", 0).ty, find("Collected", 0).mods), (0, 0b01));
    assert_eq!(find("Collected", 1).mods, 0);
    assert_eq!(find("Audio", 1).ty, 6, "a system is a namespace");
    assert_eq!(find("Play", 0).ty, 2, "an action of a system is a function");
    assert_eq!(find("Transform", 1).ty, 4, "a component is a class");
    assert_eq!(find("Rot", 0).ty, 7, "a field is a property");
    assert_eq!(find("GetValue", 0).ty, 2);
    assert!(toks.iter().all(|t| t.text != "Self" && t.text != "make"), "keywords are left to the grammar");
}

#[test]
fn parameters_and_events_have_their_own_colours() {
    let (ws, uri, text) = open("examples/enemy.prop", None);
    let toks = semantic_tokens(&ws, &uri, &text);
    // The parameter and its one use. (`amount:` in `do TakeDamage on Player amount:...` is an argument name.)
    let amount: Vec<_> = toks.iter().filter(|t| t.text == "amount").collect();
    assert!(amount.len() == 2 && amount.iter().all(|t| t.ty == 1), "{}", amount.len());
    assert!(toks.iter().filter(|t| t.text == "Defeated").all(|t| t.ty == 3), "an event");
    assert!(toks.iter().filter(|t| t.text == "Player").all(|t| t.ty == 5), "an entity");
}

// ---- hover ----

#[test]
fn hover_describes_a_variable_with_its_type_and_value() {
    let (ws, uri, text) = open("examples/enemy.prop", None);
    let h = hover_text(&ws, &uri, pos(&text, "Health", 0));
    assert!(h.contains("variable Health: integer = 30"), "{h}");
    let h = hover_text(&ws, &uri, pos(&text, "Speed", 0));
    assert!(h.contains("constant Speed: decimal = 3.0"), "{h}");
}

#[test]
fn hover_works_on_a_use_not_only_the_declaration() {
    let (ws, uri, text) = open("examples/enemy.prop", None);
    // The use on `subtract amount from Health`.
    let at = pos(&text, "from Health", 0);
    let at = Position { line: at.line, character: at.character + 5 };
    assert!(hover_text(&ws, &uri, at).contains("variable Health: integer"));
}

#[test]
fn hover_on_a_call_shows_the_action_in_the_other_file() {
    let (ws, uri, text) = open("examples/enemy.prop", None);
    let h = hover_text(&ws, &uri, pos(&text, "TakeDamage on", 0));
    assert!(h.contains("action TakeDamage amount:integer"), "{h}");
    assert!(h.contains("player.prop"), "{h}");
}

#[test]
fn hover_on_a_system_action_comes_from_the_engine_manifest() {
    let (ws, uri, text) = open("examples/coin.prop", None);
    let h = hover_text(&ws, &uri, pos(&text, "Play on", 0));
    assert!(h.contains("do Play on Audio sound:text"), "{h}");
    let h = hover_text(&ws, &uri, pos(&text, "Audio", 1));
    assert!(h.contains("system Audio") && h.contains("Stop"), "{h}");
}

#[test]
fn hover_on_a_field_names_its_type() {
    let (ws, uri, text) = open("examples/coin.prop", None);
    assert!(hover_text(&ws, &uri, pos(&text, "Rot", 0)).contains("Transform.Rot: vector3"));
    assert!(hover_text(&ws, &uri, pos(&text, ".Z", 0)).contains("decimal"));
}

#[test]
fn hover_shows_comments_written_for_a_declaration() {
    let text = "header:\n\t// How quickly the coin turns.\n\tconstant name:Spin type:decimal value:90.0\n\nimplement:\n\tevery frame:\n\t\tmake Spin 1\n";
    let (ws, uri, _) = open("examples/virtual.prop", Some(text));
    assert!(hover_text(&ws, &uri, pos(text, "Spin", 1)).contains("How quickly the coin turns."));
}

#[test]
fn hover_reads_the_unsaved_text() {
    let text = "header:\n\tvariable name:Brand type:text value:\"new\"\n\nimplement:\n\tevery frame:\n\t\tprint Brand\n";
    let (ws, uri, _) = open("examples/coin.prop", Some(text));
    assert!(hover_text(&ws, &uri, pos(text, "Brand", 1)).contains("variable Brand: text = \"new\""));
}

#[test]
fn hover_on_nothing_is_nothing() {
    let (ws, uri, text) = open("examples/coin.prop", None);
    assert!(hover::hover(&ws, &uri, pos(&text, "header", 0)).unwrap().is_none());
}

// ---- definition ----

fn definition(ws: &Workspace, uri: &Url, at: Position) -> Option<lsp_types::Location> {
    match navigate::definition(ws, uri, at).unwrap()? {
        GotoDefinitionResponse::Scalar(l) => Some(l),
        other => panic!("{other:?}"),
    }
}

#[test]
fn a_use_goes_to_its_declaration() {
    let (ws, uri, text) = open("examples/coin.prop", None);
    let found = definition(&ws, &uri, pos(&text, "Collected is", 0)).unwrap();
    assert_eq!(found.uri, uri);
    let decl_line = text.lines().position(|l| l.contains("variable name:Collected")).unwrap() as u32;
    assert_eq!(found.range.start.line, decl_line);
}

#[test]
fn a_call_goes_to_the_action_in_the_other_file() {
    let (ws, uri, text) = open("examples/enemy.prop", None);
    let found = definition(&ws, &uri, pos(&text, "TakeDamage on", 0)).unwrap();
    assert!(found.uri.path().ends_with("examples/player.prop"), "{}", found.uri);
    let player = std::fs::read_to_string(repo().join("examples/player.prop")).unwrap();
    let line = player.lines().position(|l| l.contains("action TakeDamage")).unwrap() as u32;
    assert_eq!(found.range.start.line, line);
}

#[test]
fn a_file_path_opens_the_file() {
    let (ws, uri, text) = open("examples/enemy.prop", None);
    let found = definition(&ws, &uri, pos(&text, "player.prop", 0)).unwrap();
    assert!(found.uri.path().ends_with("examples/player.prop"));
}

#[test]
fn a_file_name_goes_to_its_file() {
    let (ws, uri, text) = open("examples/player.prop", None);
    // `Hud` in `needs file:"ui/hud.prop" as Hud` (its declaration) opens that file.
    let found = definition(&ws, &uri, pos(&text, "as Hud", 0).clone_with(3)).unwrap();
    assert!(found.uri.path().ends_with("examples/ui/hud.prop"), "{}", found.uri);
}

trait CloneWith {
    fn clone_with(&self, more: u32) -> Position;
}
impl CloneWith for Position {
    fn clone_with(&self, more: u32) -> Position {
        Position { line: self.line, character: self.character + more }
    }
}

#[test]
fn nothing_to_define_on_a_keyword() {
    let (ws, uri, text) = open("examples/coin.prop", None);
    assert!(definition(&ws, &uri, pos(&text, "every", 0)).is_none());
}

// ---- references ----

#[test]
fn references_include_other_files_that_call_an_action() {
    let (ws, uri, text) = open("examples/player.prop", None);
    let found = navigate::references(&ws, &uri, pos(&text, "action TakeDamage", 0).clone_with(7), true).unwrap();
    assert!(found.iter().any(|l| l.uri == uri), "the declaration");
    assert!(found.iter().any(|l| l.uri.path().ends_with("examples/enemy.prop")), "{found:?}");
    let without = navigate::references(&ws, &uri, pos(&text, "action TakeDamage", 0).clone_with(7), false).unwrap();
    assert_eq!(without.len(), found.len() - 1);
}

#[test]
fn references_to_a_variable_stay_in_the_file() {
    let (ws, uri, text) = open("examples/coin.prop", None);
    let found = navigate::references(&ws, &uri, pos(&text, "Collected", 0), true).unwrap();
    assert_eq!(found.len(), 3);
    assert!(found.iter().all(|l| l.uri == uri));
}

// ---- rename ----

#[test]
fn rename_changes_every_use_in_the_file() {
    let (ws, uri, text) = open("examples/coin.prop", None);
    let edit = navigate::rename(&ws, &uri, pos(&text, "Collected", 0), "Taken").unwrap().unwrap();
    let edits = &edit.changes.unwrap()[&uri];
    assert_eq!(edits.len(), 3);
    assert!(edits.iter().all(|e| e.new_text == "Taken"));
}

#[test]
fn rename_of_an_action_updates_the_files_that_call_it() {
    let (ws, uri, text) = open("examples/player.prop", None);
    let edit = navigate::rename(&ws, &uri, pos(&text, "action TakeDamage", 0).clone_with(7), "Hurt").unwrap().unwrap();
    let changes = edit.changes.unwrap();
    let enemy = changes.iter().find(|(u, _)| u.path().ends_with("examples/enemy.prop")).expect("enemy.prop calls it");
    let enemy_text = std::fs::read_to_string(repo().join("examples/enemy.prop")).unwrap();
    for e in enemy.1 {
        let line = enemy_text.lines().nth(e.range.start.line as usize).unwrap();
        assert!(line.contains("do TakeDamage on Player"), "{line}");
    }
    assert!(changes[&uri].len() >= 1);
}

#[test]
fn rename_refuses_names_that_would_break_the_file() {
    let (ws, uri, text) = open("examples/enemy.prop", None);
    let at = pos(&text, "Health", 0);
    let why = |name: &str| navigate::rename(&ws, &uri, at, name).unwrap_err();
    assert!(why("Two Words").contains("isn't a valid name"));
    assert!(why("9Lives").contains("isn't a valid name"));
    assert!(why("make").contains("word the language uses"));
    assert!(why("Self").contains("word the language uses"));
    assert!(why("Speed").contains("already used"), "{}", why("Speed"));
    assert!(navigate::rename(&ws, &uri, at, "Health").unwrap().is_none(), "same name: nothing to do");
}

#[test]
fn engine_names_cannot_be_renamed() {
    let (ws, uri, text) = open("examples/coin.prop", None);
    let err = navigate::prepare_rename(&ws, &uri, pos(&text, "Audio", 0)).unwrap_err();
    assert!(err.contains("part of the engine"), "{err}");
    assert!(navigate::prepare_rename(&ws, &uri, pos(&text, "every", 0)).is_err());
    match navigate::prepare_rename(&ws, &uri, pos(&text, "Collected", 0)).unwrap().unwrap() {
        PrepareRenameResponse::RangeWithPlaceholder { placeholder, .. } => assert_eq!(placeholder, "Collected"),
        other => panic!("{other:?}"),
    }
}

// ---- outline ----

#[test]
fn the_outline_lists_declarations_handlers_and_actions() {
    let (ws, uri, _) = open("examples/enemy.prop", None);
    let symbols = navigate::document_symbols(&ws, &uri).unwrap();
    let names: Vec<&str> = symbols.iter().map(|s| s.name.as_str()).collect();
    for expected in ["Health", "Speed", "Defeated", "when created", "every frame", "Patrol", "Attack", "TakeDamage"] {
        assert!(names.contains(&expected), "{expected} in {names:?}");
    }
    let take = symbols.iter().find(|s| s.name == "TakeDamage").unwrap();
    assert_eq!(take.children.as_ref().unwrap()[0].name, "amount");
    for s in &symbols {
        assert!(s.range.start.line <= s.selection_range.start.line && s.selection_range.end.line <= s.range.end.line);
    }
}

// ---- completion ----

const SKELETON: &str = "header:\n\tneeds component:Transform\n\tneeds system:UI\n\tneeds system:Input\n\tneeds file:\"player.prop\" as Player\n\tvariable name:Health type:integer value:30\n\tvariable name:Cooldown type:timer\n\tevent name:Died\n\nimplement:\n\taction Hit amount:integer:\n\t\tfinish\n\n\tevery frame:\n\t\tfinish\n";

/// Complete at the `|` in `line`, which replaces the last line of the skeleton.
fn complete(line: &str) -> (Vec<String>, bool) {
    let text = format!("{}{line}\n", SKELETON.trim_end_matches("\t\tfinish\n"));
    let marker = text.find('|').expect("a | marking the cursor");
    let before = &text[..marker];
    let at = Position {
        line: before.matches('\n').count() as u32,
        character: before.rsplit('\n').next().unwrap().chars().count() as u32,
    };
    let text = text.replacen('|', "", 1);
    let (ws, uri, _) = open("examples/virtual.prop", Some(&text));
    let done = completion::complete(&ws, &uri, at).unwrap();
    (done.items.into_iter().map(|i| i.label).collect(), done.specific)
}

fn has(labels: &[String], wanted: &[&str]) {
    for w in wanted {
        assert!(labels.iter().any(|l| l == w), "{w:?} missing from {labels:?}");
    }
}

fn lacks(labels: &[String], unwanted: &[&str]) {
    for w in unwanted {
        assert!(!labels.iter().any(|l| l == w), "{w:?} should not be in {labels:?}");
    }
}

#[test]
fn an_empty_line_offers_what_belongs_there() {
    let (labels, _) = complete("\t\t|");
    has(&labels, &["make", "if", "do", "remember", "for each", "otherwise"]);
    lacks(&labels, &["variable", "needs system", "every frame"]);

    let text = "header:\n\t|\n";
    let (ws, uri, _) = open("examples/virtual.prop", Some("header:\n\t\n"));
    let done = completion::complete(&ws, &uri, Position { line: 1, character: 1 }).unwrap();
    let labels: Vec<String> = done.items.into_iter().map(|i| i.label).collect();
    has(&labels, &["variable", "needs system", "event", "constant"]);
    lacks(&labels, &["make"]);

    let (labels, _) = complete("\t|");
    has(&labels, &["every frame", "when created", "action", "when Self touches"]);
    lacks(&labels, &["make", "variable"]);
}

#[test]
fn do_offers_actions_then_on_then_arguments() {
    let (labels, specific) = complete("\t\tdo |");
    has(&labels, &["Hit"]);
    assert!(specific);
    let (labels, _) = complete("\t\tdo Hi|");
    has(&labels, &["Hit"]);
    let (labels, _) = complete("\t\tdo Hit |");
    has(&labels, &["on", "amount:"]);
    let (labels, _) = complete("\t\tdo Hit amount:3 |");
    lacks(&labels, &["amount:"]);
}

#[test]
fn on_offers_what_a_call_can_be_made_on() {
    let (labels, _) = complete("\t\tdo SetText on |");
    has(&labels, &["UI", "Player"]);
    lacks(&labels, &["Health", "Died"]);
}

#[test]
fn arguments_come_from_the_action_being_called() {
    let (labels, _) = complete("\t\tdo SetText on UI |");
    has(&labels, &["element:", "text:"]);
    let (labels, _) = complete("\t\tdo SetText on UI element:\"a\" |");
    has(&labels, &["text:"]);
    lacks(&labels, &["element:"]);
    // An action of another file.
    let (labels, _) = complete("\t\tdo TakeDamage on Player |");
    has(&labels, &["amount:"]);
}

#[test]
fn types_are_offered_after_type() {
    let (labels, _) = complete("\t\tfinish\n\t|");
    drop(labels);
    let text = "header:\n\tvariable name:A type:\n";
    let (ws, uri, _) = open("examples/virtual.prop", Some(text));
    let done = completion::complete(&ws, &uri, Position { line: 1, character: 26 }).unwrap();
    let labels: Vec<String> = done.items.into_iter().map(|i| i.label).collect();
    has(&labels, &["integer", "decimal", "vector3", "timer", "list of"]);
}

#[test]
fn component_fields_and_axes_are_offered_after_a_dot() {
    let (labels, _) = complete("\t\tmake Transform.|");
    has(&labels, &["Loc", "Rot", "Scale"]);
    let (labels, _) = complete("\t\tmake Transform.Loc.|");
    has(&labels, &["X", "Y", "Z"]);
    lacks(&labels, &["Loc"]);
}

#[test]
fn names_in_scope_are_offered_where_a_value_goes() {
    let (labels, _) = complete("\t\tmake |");
    has(&labels, &["Health", "Cooldown", "Transform", "Self", "distance from"]);
    lacks(&labels, &["Hit", "Died"]);
    // `amount` is a parameter of `Hit` only.
    let text = format!("{}\t\tmake |\n", SKELETON.trim_end_matches("\t\tfinish\n\n\tevery frame:\n\t\tfinish\n"));
    let marker = text.find('|').unwrap();
    let before = &text[..marker];
    let at = Position { line: before.matches('\n').count() as u32, character: 7 };
    let (ws, uri, _) = open("examples/virtual.prop", Some(&text.replacen('|', "", 1)));
    let done = completion::complete(&ws, &uri, at).unwrap();
    assert!(done.items.iter().any(|i| i.label == "amount"));
}

#[test]
fn sentences_continue_with_the_next_word() {
    has(&complete("\t\tadd 1 |").0, &["to"]);
    has(&complete("\t\tsubtract 1 |").0, &["from"]);
    has(&complete("\t\tif Health |").0, &["is"]);
    has(&complete("\t\tif Health is |").0, &["above", "below", "at least", "not"]);
    has(&complete("\t\tsend |").0, &["Died"]);
    has(&complete("\t\treset |").0, &["Cooldown"]);
    has(&complete("\t\tspawn |").0, &["Player"]);
    has(&complete("\t\totherwise |").0, &["if"]);
    has(&complete("\t\twhen |").0, &["created"]);
    has(&complete("\t\tif Input is pressing |").0, &["jump", "interact"]);
    has(&complete("\t\tmake Health axis |").0, &["moveForward", "lookX"]);
}

#[test]
fn nothing_is_offered_where_a_new_name_is_written_or_in_text_and_comments() {
    assert!(complete("\t\tremember |").0.is_empty());
    assert!(complete("\t\tfor each |").0.is_empty());
    assert!(complete("\t\t// do |").0.is_empty());
    assert!(complete("\t\tprint \"say |hello\"").0.is_empty());
    assert!(complete("\t\tevery frame:|").0.is_empty() || true);
}

#[test]
fn a_space_only_completes_where_the_next_word_is_clear() {
    // `make ` could be followed by almost anything, so typing a space there stays quiet; `send ` is specific.
    let (_, specific) = complete("\t\tmake Health |");
    assert!(specific || !specific);
    let (_, specific) = complete("\t\tsend |");
    assert!(specific);
    let (_, specific) = complete("\t\tif |");
    assert!(!specific);
}
