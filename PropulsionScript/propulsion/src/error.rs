use std::fmt;

/// A problem found in a `.prop` file, with the position it was found at (1-based).
#[derive(Debug, Clone, PartialEq)]
pub struct PropulsionError {
    pub line: usize,
    pub col: usize,
    pub message: String,
}

impl PropulsionError {
    pub fn new(line: usize, col: usize, message: impl Into<String>) -> Self {
        PropulsionError { line, col, message: message.into() }
    }

    /// Format the error with the offending source line and a caret under the column.
    pub fn render(&self, source: &str, path: &str) -> String {
        let text = source.lines().nth(self.line.saturating_sub(1)).unwrap_or("");
        let gutter = self.line.to_string();
        let pad = " ".repeat(gutter.len());
        // Errors that only know their line (col 1) point at the first character of the statement.
        let col = if self.col <= 1 {
            text.chars().take_while(|c| c.is_whitespace()).count() + 1
        } else {
            self.col
        };
        // Copy tabs so the caret lines up with the source line in any terminal.
        let caret_prefix: String = text
            .chars()
            .take(col.saturating_sub(1))
            .map(|c| if c == '\t' { '\t' } else { ' ' })
            .collect();
        format!(
            "error: {msg}\n{pad} --> {path}:{line}:{col}\n{pad} |\n{gutter} | {text}\n{pad} | {caret_prefix}^\n",
            msg = self.message,
            line = self.line,
            col = self.col,
        )
    }
}

impl fmt::Display for PropulsionError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "line {}:{}: {}", self.line, self.col, self.message)
    }
}

impl std::error::Error for PropulsionError {}
