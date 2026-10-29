//! Turning the compiler's errors into editor diagnostics.

use propulsion::{PropulsionError, Manifest, Project};
use lsp_types::{Diagnostic, DiagnosticSeverity, Position, Range};
use std::collections::HashMap;
use std::path::Path;

/// Check `path` (and everything it needs) and return the problems found in `path` itself.
/// `overrides` holds the text of files that are open in the editor, by `Project::key_for`,
/// so unsaved edits are what gets checked.
pub fn diagnose(path: &Path, overrides: &HashMap<String, String>) -> Result<Vec<Diagnostic>, String> {
    propulsion::use_inputs_near(path); // the game's own Input Actions, if it has an inputs file
    let project = Project::load_with(path, overrides)?;
    let reports = project.check(&Manifest::builtin());
    let Some(report) = reports.iter().find(|r| r.path == project.entry) else {
        return Ok(Vec::new());
    };
    Ok(report.errors.iter().map(|e| to_diagnostic(&report.source, e)).collect())
}

fn to_diagnostic(source: &str, error: &PropulsionError) -> Diagnostic {
    Diagnostic {
        range: range_for(source, error),
        severity: Some(DiagnosticSeverity::ERROR),
        source: Some("propc".to_string()),
        message: error.message.clone(),
        ..Diagnostic::default()
    }
}

/// The part of the line to underline. An error that only knows its line (column 1) covers the
/// whole statement; otherwise it covers the word, or the text, that starts at the column.
fn range_for(source: &str, error: &PropulsionError) -> Range {
    let line_index = error.line.saturating_sub(1);
    let chars: Vec<char> = source.lines().nth(line_index).unwrap_or("").chars().collect();
    let len = chars.len();

    let (start, end) = if error.col <= 1 {
        let start = chars.iter().take_while(|c| c.is_whitespace()).count();
        // Stop before a trailing comment, so only the code is underlined.
        let code_end = comment_start(&chars).unwrap_or(len);
        let end = chars[..code_end].iter().rposition(|c| !c.is_whitespace()).map_or(start, |last| last + 1);
        (start, end.max(start))
    } else {
        // A column past the end of the line (an error at the end of a statement) points at the last character.
        let start = (error.col - 1).min(len.saturating_sub(1));
        let end = match chars.get(start) {
            Some(c) if c.is_alphanumeric() || *c == '_' => {
                start + chars[start..].iter().take_while(|c| c.is_alphanumeric() || **c == '_').count()
            }
            Some('"') => chars[start + 1..].iter().position(|c| *c == '"').map_or(len, |close| start + close + 2),
            Some(_) => start + 1,
            None => start,
        };
        (start, end)
    };

    Range {
        start: Position { line: line_index as u32, character: utf16_offset(&chars, start) },
        end: Position { line: line_index as u32, character: utf16_offset(&chars, end) },
    }
}

/// Where a `//` comment starts on the line, ignoring any `//` inside a quoted text.
fn comment_start(chars: &[char]) -> Option<usize> {
    let mut in_text = false;
    for (i, c) in chars.iter().enumerate() {
        match c {
            '"' => in_text = !in_text,
            '/' if !in_text && chars.get(i + 1) == Some(&'/') => return Some(i),
            _ => {}
        }
    }
    None
}

/// Editors count columns in UTF-16 units; the compiler counts characters.
fn utf16_offset(chars: &[char], index: usize) -> u32 {
    chars.iter().take(index).map(|c| c.len_utf16() as u32).sum()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    fn repo(rel: &str) -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..").join(rel)
    }

    fn with_text(path: &Path, text: &str) -> HashMap<String, String> {
        HashMap::from([(Project::key_for(path), text.to_string())])
    }

    #[test]
    fn a_good_file_has_no_diagnostics() {
        let path = repo("examples/coin.prop");
        assert_eq!(diagnose(&path, &HashMap::new()).unwrap(), vec![]);
    }

    #[test]
    fn every_example_is_clean() {
        for rel in ["examples/enemy.prop", "examples/player.prop", "examples/spawner.prop", "examples/ui/hud.prop"] {
            let found = diagnose(&repo(rel), &HashMap::new()).unwrap();
            assert!(found.is_empty(), "{rel}: {found:?}");
        }
    }

    #[test]
    fn the_unsaved_text_is_what_gets_checked() {
        let path = repo("examples/coin.prop");
        let text = std::fs::read_to_string(&path).unwrap().replace("destroy Self", "destroy Nothing");
        let found = diagnose(&path, &with_text(&path, &text)).unwrap();
        assert_eq!(found.len(), 1, "{found:?}");
        let line = text.lines().position(|l| l.contains("destroy Nothing")).unwrap() as u32;
        assert_eq!(found[0].range.start.line, line);
        assert_eq!(found[0].severity, Some(DiagnosticSeverity::ERROR));
    }

    #[test]
    fn an_unsaved_dependency_is_seen_by_the_file_that_needs_it() {
        // enemy.prop calls `do TakeDamage on Player`; rename that action in an unsaved player.prop.
        let enemy = repo("examples/enemy.prop");
        let player = repo("examples/player.prop");
        let text = std::fs::read_to_string(&player).unwrap().replace("action TakeDamage", "action Hurt");
        let found = diagnose(&enemy, &with_text(&player, &text)).unwrap();
        assert!(found.iter().any(|d| d.message.contains("TakeDamage")), "{found:?}");
    }

    #[test]
    fn a_syntax_error_underlines_its_word() {
        let path = repo("examples/coin.prop");
        let found = diagnose(&path, &with_text(&path, "header:\n\tneeds component:Transform\n\nimplement:\n\tevery frame:\n\t\tmake Speed (1)\n")).unwrap();
        assert_eq!(found.len(), 1, "{found:?}");
        assert_eq!(found[0].range.start.line, 5);
        assert!(found[0].message.contains("parentheses"));
        assert_eq!(found[0].range.end.character - found[0].range.start.character, 1);
    }

    #[test]
    fn columns_are_counted_in_utf16_units() {
        // The emoji is one character but two UTF-16 units, so the word after it starts one unit later.
        let source = "\t\"😀\" oops\n";
        let range = range_for(source, &PropulsionError::new(1, 6, "x"));
        assert_eq!(range.start.character, 6);
        assert_eq!(range.end.character, 10);
    }

    #[test]
    fn a_line_only_error_covers_the_statement_but_not_its_comment() {
        let source = "\t\tmake Speed 1 // note\n";
        let range = range_for(source, &PropulsionError::new(1, 1, "x"));
        assert_eq!(range.start.character, 2);
        assert_eq!(range.end.character, 14);
    }

    #[test]
    fn slashes_inside_text_are_not_a_comment() {
        let source = "\tprint \"a // b\"\n";
        let range = range_for(source, &PropulsionError::new(1, 1, "x"));
        assert_eq!(range.end.character, 15);
    }
}
