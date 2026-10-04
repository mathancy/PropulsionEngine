use crate::error::ForgeError;
use std::collections::{HashMap, HashSet};

/// The comments in a file, keyed by the line of code they belong to. The language ignores
/// comments, but tools (the formatter, the node editor) must not lose them.
#[derive(Debug, Clone, Default)]
pub struct Comments {
    /// Comment lines directly above a line of code, without the `//`.
    pub leading: HashMap<usize, Vec<String>>,
    /// A comment at the end of a line of code.
    pub trailing: HashMap<usize, String>,
    /// Lines of code that had a blank line above them (above their comments, if any).
    pub blank_before: HashSet<usize>,
    /// Blank lines inside a group of comments: position `i` means a blank line before the `i`th
    /// comment, and a position equal to the number of comments means one between the last
    /// comment and the code.
    pub gaps: HashMap<usize, Vec<usize>>,
    /// Comments after the last line of code.
    pub footer: Vec<String>,
}

#[derive(Debug, Clone)]
pub struct Lexed {
    pub tokens: Vec<Token>,
    pub comments: Comments,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Tok {
    Word(String),
    Number(String),
    Text(String),
    Colon,
    Comma,
    Dot,
    Newline,
    Indent,
    Dedent,
    Eof,
}

#[derive(Debug, Clone)]
pub struct Token {
    pub tok: Tok,
    pub line: usize,
    pub col: usize,
}

/// Turn source text into tokens. Indentation (tabs only) becomes `Indent`/`Dedent`
/// tokens, and every non-blank line ends with a `Newline`.
pub fn lex(src: &str) -> Result<Vec<Token>, ForgeError> {
    lex_with_comments(src).map(|l| l.tokens)
}

/// Like `lex`, but also keeps the comments and blank lines (see `Comments`).
pub fn lex_with_comments(src: &str) -> Result<Lexed, ForgeError> {
    // Some editors start a UTF-8 file with an invisible byte-order mark; it is not part of the script.
    let src = src.strip_prefix('\u{feff}').unwrap_or(src);
    let mut out: Vec<Token> = Vec::new();
    let mut comments = Comments::default();
    let mut level = 0usize;
    let mut last_line = 1;

    // Comments waiting for the next line of code, and whether a blank line came before them.
    let mut pending: Vec<String> = Vec::new();
    let mut pending_gaps: Vec<usize> = Vec::new();
    let mut blank_before_group = false;
    let mut saw_blank = false;

    for (i, raw) in src.lines().enumerate() {
        let line_no = i + 1;
        last_line = line_no;
        let trimmed = raw.trim();
        if trimmed.is_empty() {
            if pending.is_empty() {
                saw_blank = true;
            } else if !pending_gaps.contains(&pending.len()) {
                pending_gaps.push(pending.len());
            }
            continue;
        }
        if let Some(rest) = trimmed.strip_prefix("//") {
            if pending.is_empty() {
                blank_before_group = saw_blank;
            }
            pending.push(rest.trim().to_string());
            saw_blank = false;
            continue;
        }
        let blank = if pending.is_empty() { saw_blank } else { blank_before_group };
        if !pending.is_empty() {
            comments.leading.insert(line_no, std::mem::take(&mut pending));
            if !pending_gaps.is_empty() {
                comments.gaps.insert(line_no, std::mem::take(&mut pending_gaps));
            }
        }
        if blank {
            comments.blank_before.insert(line_no);
        }
        saw_blank = false;

        let chars: Vec<char> = raw.chars().collect();
        let mut pos = 0;
        while pos < chars.len() && (chars[pos] == '\t' || chars[pos] == ' ') {
            if chars[pos] == ' ' {
                return Err(ForgeError::new(
                    line_no,
                    pos + 1,
                    "indent with tabs only; spaces in indentation are not allowed",
                ));
            }
            pos += 1;
        }
        let tabs = pos;

        while level > tabs {
            out.push(Token { tok: Tok::Dedent, line: line_no, col: 1 });
            level -= 1;
        }
        if tabs > level {
            if tabs != level + 1 {
                return Err(ForgeError::new(line_no, 1, "indentation jumps by more than one tab"));
            }
            out.push(Token { tok: Tok::Indent, line: line_no, col: 1 });
            level = tabs;
        }

        let n = chars.len();
        while pos < n {
            let c = chars[pos];
            let col = pos + 1;
            match c {
                ' ' | '\t' => pos += 1,
                '/' if pos + 1 < n && chars[pos + 1] == '/' => {
                    let text: String = chars[pos + 2..].iter().collect();
                    comments.trailing.insert(line_no, text.trim().to_string());
                    break;
                }
                '"' => {
                    let start = pos + 1;
                    let mut end = start;
                    while end < n && chars[end] != '"' {
                        end += 1;
                    }
                    if end >= n {
                        return Err(ForgeError::new(line_no, col, "unterminated text; expected a closing `\"`"));
                    }
                    let text: String = chars[start..end].iter().collect();
                    out.push(Token { tok: Tok::Text(text), line: line_no, col });
                    pos = end + 1;
                }
                ':' => {
                    out.push(Token { tok: Tok::Colon, line: line_no, col });
                    pos += 1;
                }
                ',' => {
                    out.push(Token { tok: Tok::Comma, line: line_no, col });
                    pos += 1;
                }
                '.' => {
                    out.push(Token { tok: Tok::Dot, line: line_no, col });
                    pos += 1;
                }
                '-' | '0'..='9' => {
                    let start = pos;
                    if c == '-' {
                        if !(pos + 1 < n && chars[pos + 1].is_ascii_digit()) {
                            return Err(ForgeError::new(
                                line_no,
                                col,
                                "unexpected `-`; write `minus` for subtraction (negative numbers are written like `-8`)",
                            ));
                        }
                        pos += 1;
                    }
                    while pos < n && chars[pos].is_ascii_digit() {
                        pos += 1;
                    }
                    if pos + 1 < n && chars[pos] == '.' && chars[pos + 1].is_ascii_digit() {
                        pos += 1;
                        while pos < n && chars[pos].is_ascii_digit() {
                            pos += 1;
                        }
                    }
                    let text: String = chars[start..pos].iter().collect();
                    out.push(Token { tok: Tok::Number(text), line: line_no, col });
                }
                c if c.is_alphabetic() || c == '_' => {
                    let start = pos;
                    while pos < n && (chars[pos].is_alphanumeric() || chars[pos] == '_') {
                        pos += 1;
                    }
                    let text: String = chars[start..pos].iter().collect();
                    out.push(Token { tok: Tok::Word(text), line: line_no, col });
                }
                '(' | ')' => {
                    return Err(ForgeError::new(
                        line_no,
                        col,
                        "parentheses are not part of ForgeScript; use a fixed phrase, one operator, or `remember` to split the line (see SPEC 4.1)",
                    ));
                }
                '[' | ']' => {
                    return Err(ForgeError::new(
                        line_no,
                        col,
                        "square brackets are not part of ForgeScript; list items go on indented `entry` lines (see SPEC 3.1)",
                    ));
                }
                other => {
                    return Err(ForgeError::new(line_no, col, format!("unexpected character `{other}`")));
                }
            }
        }
        out.push(Token { tok: Tok::Newline, line: line_no, col: n + 1 });
    }

    while level > 0 {
        out.push(Token { tok: Tok::Dedent, line: last_line, col: 1 });
        level -= 1;
    }
    out.push(Token { tok: Tok::Eof, line: last_line, col: 1 });
    comments.footer = pending;
    Ok(Lexed { tokens: out, comments })
}
