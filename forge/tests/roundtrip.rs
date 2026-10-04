//! The formatter must never change what a file means or lose a comment.

use std::fs;
use std::path::PathBuf;

fn root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..")
}

fn forge_files(dir: PathBuf) -> Vec<PathBuf> {
    let mut files = Vec::new();
    let mut stack = vec![dir];
    while let Some(d) = stack.pop() {
        for entry in fs::read_dir(&d).unwrap() {
            let p = entry.unwrap().path();
            if p.is_dir() {
                stack.push(p);
            } else if p.extension().is_some_and(|x| x == "forge") {
                files.push(p);
            }
        }
    }
    files.sort();
    files
}

/// The tree as text, without line numbers (formatting moves lines around).
fn shape(src: &str) -> String {
    let file = forge::parse(src).unwrap_or_else(|e| panic!("should parse: {e}\n{src}"));
    format!("{file:#?}")
        .lines()
        .filter(|l| !l.trim_start().starts_with("line: ") && !l.trim_start().starts_with("unattached"))
        .collect::<Vec<_>>()
        .join("\n")
}

fn all_valid_files() -> Vec<PathBuf> {
    let mut files = forge_files(root().join("examples"));
    files.extend(forge_files(root().join("behaviors")));
    files
}

#[test]
fn formatting_keeps_the_meaning_and_every_comment_of_every_example_and_behavior() {
    let files = all_valid_files();
    assert!(files.len() >= 15, "found only {} files", files.len());
    for path in files {
        let src = fs::read_to_string(&path).unwrap();
        let formatted = forge::format_source(&src).unwrap_or_else(|e| panic!("{}: {e}", path.display()));
        assert_eq!(
            shape(&src),
            shape(&formatted),
            "{}: formatting changed the parsed tree (or dropped a comment)",
            path.display()
        );
    }
}

#[test]
fn formatting_twice_changes_nothing() {
    for path in all_valid_files() {
        let src = fs::read_to_string(&path).unwrap();
        let once = forge::format_source(&src).unwrap();
        let twice = forge::format_source(&once).unwrap();
        assert_eq!(once, twice, "{} is not stable under formatting", path.display());
    }
}

#[test]
fn every_example_and_behavior_is_already_formatted() {
    for path in all_valid_files() {
        let src = fs::read_to_string(&path).unwrap().replace("\r\n", "\n");
        let formatted = forge::format_source(&src).unwrap();
        assert_eq!(src, formatted, "{} needs `forgec fmt --write`", path.display());
    }
}

#[test]
fn messy_spacing_and_comments_are_tidied_but_kept() {
    let messy = "\
//top comment

header:
\tvariable name:Health   type:integer value:100 //trailing note
\tconstant name:Max type:integer    value:100



\tevent name:Died
implement:
\t//describes the handler
\tevery frame:
\t\t//first
\t\t// second
\t\tif Health is at most 0,   then send Died
\t\tmake Health 5



\t\tfinish
\t// after everything
";
    let expected = "\
// top comment

header:
\tvariable name:Health type:integer value:100 // trailing note
\tconstant name:Max type:integer value:100

\tevent name:Died

implement:
\t// describes the handler
\tevery frame:
\t\t// first
\t\t// second
\t\tif Health is at most 0, then send Died
\t\tmake Health 5

\t\tfinish

// after everything
";
    assert_eq!(forge::format_source(messy).unwrap(), expected);
}

#[test]
fn block_and_inline_if_keep_their_style() {
    let src = "header:\n\tvariable name:A type:boolean\n\nimplement:\n\tevery frame:\n\t\tif A is true, then finish\n\t\tif A is true:\n\t\t\tfinish\n";
    let out = forge::format_source(src).unwrap();
    assert!(out.contains("if A is true, then finish"));
    assert!(out.contains("if A is true:\n\t\t\tfinish"));
}

#[test]
fn a_comment_in_a_place_it_cannot_be_kept_is_refused_not_dropped() {
    let src = "header:\n\tvariable name:A type:boolean\n\nimplement:\n\tevery frame:\n\t\tif A is true:\n\t\t\tfinish\n\t\t// about the else\n\t\totherwise:\n\t\t\tfinish\n";
    let err = forge::format_source(src).unwrap_err();
    assert!(err.message.contains("can't keep it"), "{err}");
    assert_eq!(err.line, 9, "points at the line the comment is above (`otherwise:`)");
}

#[test]
fn comments_survive_on_every_kind_of_line() {
    let src = "\
header:
\t// a
\tneeds component:Transform
\t// b
\tvariable name:Spawns type:list of vector3
\t\tentry x:0 y:0 z:0

implement:
\t// c
\twhen created: // d
\t\t// e
\t\tmake Spawns Spawns
\t// f
\taction Run amount:integer gives integer: // g
\t\t// h
\t\tgive back amount
";
    // The comment-free parts of this file are intentionally not valid to run; only formatting matters.
    let out = forge::format_source(src);
    // `make Spawns Spawns` and the `entry` line have no comment above, so the file is formattable.
    let out = out.unwrap_or_else(|e| panic!("{e}"));
    for note in ["a", "b", "c", "d", "e", "f", "g", "h"] {
        assert!(out.contains(&format!("// {note}")), "lost comment {note} in:\n{out}");
    }
}
