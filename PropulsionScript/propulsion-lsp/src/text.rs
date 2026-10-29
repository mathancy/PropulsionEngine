//! Converting between the compiler's positions (1-based, counted in characters) and the
//! editor's (0-based, counted in UTF-16 units).

use propulsion::index::Span;
use lsp_types::{Position, Range};

fn utf16_offset(line_text: &str, chars_before: usize) -> u32 {
    line_text.chars().take(chars_before).map(|c| c.len_utf16() as u32).sum()
}

/// The editor range for a span on `line_text`.
pub fn span_range(line_text: &str, span: Span) -> Range {
    let line = (span.line - 1) as u32;
    Range {
        start: Position { line, character: utf16_offset(line_text, span.col - 1) },
        end: Position { line, character: utf16_offset(line_text, span.col - 1 + span.len) },
    }
}

/// The range of a whole line, without the line break.
pub fn line_range(line_text: &str, line: usize) -> Range {
    let line = (line - 1) as u32;
    Range {
        start: Position { line, character: 0 },
        end: Position { line, character: line_text.chars().map(|c| c.len_utf16() as u32).sum() },
    }
}

/// The compiler's `(line, col)` for an editor position.
pub fn index_position(line_text: &str, position: Position) -> (usize, usize) {
    let mut units = 0;
    let mut col = 1;
    for c in line_text.chars() {
        if units >= position.character {
            break;
        }
        units += c.len_utf16() as u32;
        col += 1;
    }
    (position.line as usize + 1, col)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn positions_round_trip_through_utf16() {
        let text = "\t\"😀\" oops";
        let span = Span { line: 3, col: 6, len: 4 };
        let range = span_range(text, span);
        assert_eq!((range.start.character, range.end.character), (6, 10));
        assert_eq!(index_position(text, range.start), (3, 6));
        assert_eq!(index_position(text, range.end), (3, 10));
    }
}
