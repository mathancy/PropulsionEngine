//! Checks that need the whole file rather than a single line.

use crate::ast::*;
use crate::error::PropulsionError;
use std::collections::HashMap;

/// Check names (SPEC 6, rules 6 and 6a). Returns every problem found.
pub fn check(file: &File) -> Vec<PropulsionError> {
    let mut errors = Vec::new();
    let mut declared: HashMap<String, usize> = HashMap::new();

    let mut declare = |name: &str, line: usize, errors: &mut Vec<PropulsionError>| {
        if name == "Self" {
            errors.push(PropulsionError::new(
                line,
                1,
                "`Self` is reserved: it always means the file the code is running in, so it can't be declared",
            ));
        } else if let Some(first) = declared.get(name) {
            errors.push(PropulsionError::new(
                line,
                1,
                format!("`{name}` is already declared on line {first}; names are unique within a file"),
            ));
        } else {
            declared.insert(name.to_string(), line);
        }
    };

    for d in &file.header {
        match &d.kind {
            DeclKind::Variable { name, .. } | DeclKind::Constant { name, .. } => declare(name, d.line, &mut errors),
            DeclKind::NeedsEntity { name, .. } | DeclKind::NeedsEntities { name, .. } => declare(name, d.line, &mut errors),
            DeclKind::NeedsFile { alias: Some(alias), .. } => declare(alias, d.line, &mut errors),
            DeclKind::Event(name) => declare(name, d.line, &mut errors),
            DeclKind::Enum { name, values } => {
                declare(name, d.line, &mut errors);
                for v in values {
                    declare(v, d.line, &mut errors);
                }
            }
            DeclKind::Struct { name, .. } | DeclKind::Table { name, .. } => declare(name, d.line, &mut errors),
            _ => {}
        }
    }
    for item in &file.implement {
        if let ItemKind::Action { name, .. } = &item.kind {
            declare(name, item.line, &mut errors);
        }
    }
    // `only once as Greeting:` names the statement so `reset Greeting` can start it over.
    for item in &file.implement {
        let (ItemKind::Handler { body, .. } | ItemKind::Action { body, .. }) = &item.kind;
        declare_latches(body, &mut declare, &mut errors);
    }

    // Remembered names must not collide with declared names or each other (within one handler/action).
    for item in &file.implement {
        let body = match &item.kind {
            ItemKind::Handler { body, .. } | ItemKind::Action { body, .. } => body,
        };
        let mut remembered: HashMap<String, usize> = HashMap::new();
        check_remembered(body, &declared, &mut remembered, &mut errors);
    }

    errors
}

fn check_remembered(
    body: &[Stmt],
    declared: &HashMap<String, usize>,
    remembered: &mut HashMap<String, usize>,
    errors: &mut Vec<PropulsionError>,
) {
    for stmt in body {
        match &stmt.kind {
            StmtKind::Remember { name, .. } => {
                if name == "Self" {
                    errors.push(PropulsionError::new(stmt.line, 1, "`Self` is reserved and can't be remembered"));
                } else if let Some(first) = declared.get(name).or_else(|| remembered.get(name)) {
                    errors.push(PropulsionError::new(
                        stmt.line,
                        1,
                        format!("`{name}` is already declared on line {first}; names are unique within a file"),
                    ));
                } else {
                    remembered.insert(name.clone(), stmt.line);
                }
            }
            StmtKind::If { branches, otherwise, .. } => {
                for (_, b) in branches {
                    check_remembered(b, declared, remembered, errors);
                }
                if let Some(b) = otherwise {
                    check_remembered(b, declared, remembered, errors);
                }
            }
            StmtKind::Repeat { body, .. }
            | StmtKind::ForEach { body, .. }
            | StmtKind::While { body, .. }
            | StmtKind::ForRange { body, .. }
            | StmtKind::After { body, .. }
            | StmtKind::OnlyOnce { body, .. }
            | StmtKind::FirstTimes { body, .. }
            | StmtKind::Through { body, .. }
            | StmtKind::Animate { body, .. } => {
                check_remembered(body, declared, remembered, errors);
            }
            StmtKind::Alternate { first, second, .. } => {
                check_remembered(first, declared, remembered, errors);
                check_remembered(second, declared, remembered, errors);
            }
            StmtKind::Choose { cases, otherwise, .. } => {
                for c in cases {
                    check_remembered(&c.body, declared, remembered, errors);
                }
                if let Some(b) = otherwise {
                    check_remembered(b, declared, remembered, errors);
                }
            }
            _ => {}
        }
    }
}

fn declare_latches(body: &[Stmt], declare: &mut impl FnMut(&str, usize, &mut Vec<PropulsionError>), errors: &mut Vec<PropulsionError>) {
    for stmt in body {
        match &stmt.kind {
            StmtKind::OnlyOnce { name, body } | StmtKind::FirstTimes { name, body, .. } => {
                if let Some(n) = name {
                    declare(n, stmt.line, errors);
                }
                declare_latches(body, declare, errors);
            }
            StmtKind::Alternate { name, first, second } => {
                if let Some(n) = name {
                    declare(n, stmt.line, errors);
                }
                declare_latches(first, declare, errors);
                declare_latches(second, declare, errors);
            }
            StmtKind::If { branches, otherwise, .. } => {
                for (_, b) in branches {
                    declare_latches(b, declare, errors);
                }
                if let Some(b) = otherwise {
                    declare_latches(b, declare, errors);
                }
            }
            StmtKind::Choose { cases, otherwise, .. } => {
                for c in cases {
                    declare_latches(&c.body, declare, errors);
                }
                if let Some(b) = otherwise {
                    declare_latches(b, declare, errors);
                }
            }
            StmtKind::Repeat { body, .. }
            | StmtKind::ForEach { body, .. }
            | StmtKind::ForRange { body, .. }
            | StmtKind::While { body, .. }
            | StmtKind::After { body, .. }
            | StmtKind::Through { body, .. }
            | StmtKind::Animate { body, .. } => declare_latches(body, declare, errors),
            _ => {}
        }
    }
}
