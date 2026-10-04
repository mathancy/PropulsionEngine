//! ForgeScript front end: lexer, parser, and file-level checks.
//!
//! ```
//! let file = forge::parse("header:\n\tvariable name:Switch type:boolean\n\nimplement:\n\tevery frame:\n\t\tif Switch is true, then make Switch false\n").unwrap();
//! assert_eq!(file.header.len(), 1);
//! ```

pub mod ast;
pub mod check;
pub mod codegen;
pub mod error;
pub mod format;
pub mod graph;
pub mod index;
pub mod lexer;
pub mod manifest;
pub mod nodes;
pub mod parser;
pub mod phrases;
pub mod project;
pub mod runtime;
pub mod ty;
pub mod typecheck;

pub use check::check;
pub use error::ForgeError;
pub use format::{format_file, format_source};
pub use manifest::Manifest;
pub use parser::parse;
pub use project::{check_path, FileReport, Project};

/// Parse a file and run the file-level checks, returning every error found.
pub fn parse_and_check(src: &str) -> Result<ast::File, Vec<ForgeError>> {
    let file = parse(src).map_err(|e| vec![e])?;
    let errors = check(&file);
    if errors.is_empty() {
        Ok(file)
    } else {
        Err(errors)
    }
}
