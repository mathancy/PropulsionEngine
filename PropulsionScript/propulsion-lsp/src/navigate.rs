//! Go to definition, find references, rename, and the outline.

use crate::resolve::{resolve_member, thing_at, Resolved, Thing};
use crate::text::{index_position, line_range, span_range};
use crate::workspace::{key_of, same_key, url_for_key, Workspace};
use propulsion::index::{ExternalKind, Index, Section, SymbolKind};
use lsp_types::{
    DocumentSymbol, GotoDefinitionResponse, Location, Position, PrepareRenameResponse, Range, TextEdit, Url,
    WorkspaceEdit,
};
use std::collections::HashMap;
use std::path::Path;

fn cursor(index: &Index, position: Position) -> (usize, usize) {
    index_position(index.line_text(position.line as usize + 1), position)
}

fn location(uri: Url, index: &Index, span: propulsion::index::Span) -> Location {
    Location { uri, range: span_range(index.line_text(span.line), span) }
}

fn start_of_file() -> Range {
    Range { start: Position { line: 0, character: 0 }, end: Position { line: 0, character: 0 } }
}

pub fn definition(ws: &Workspace, uri: &Url, position: Position) -> Result<Option<GotoDefinitionResponse>, String> {
    let (path, index) = ws.index_of(uri)?;
    let (line, col) = cursor(&index, position);

    let found = match thing_at(&index, line, col) {
        Some(Thing::Symbol { id }) => {
            let s = index.symbol(id);
            // A file name goes to the file itself; everything else to where it is declared.
            if s.kind == SymbolKind::File && s.decl.contains(line, col) {
                s.file.as_ref().and_then(|f| open_file(&path, f))
            } else {
                Some(location(uri.clone(), &index, s.decl))
            }
        }
        Some(Thing::Member(i)) => match resolve_member(ws, &path, &index, &index.externals[i]) {
            Some(Resolved::Remote(r)) => {
                url_for_key(&r.key).map(|u| location(u, &r.index, r.index.symbol(r.symbol).decl))
            }
            _ => None,
        },
        Some(Thing::FilePath(i)) => open_file(&path, &index.file_refs[i].path),
        _ => None,
    };
    Ok(found.map(GotoDefinitionResponse::Scalar))
}

/// The start of the file that `rel` (written in a `needs`) names, if it exists.
fn open_file(this_file: &Path, rel: &str) -> Option<Location> {
    let target = this_file.parent()?.join(rel);
    if !target.is_file() {
        return None;
    }
    Some(Location { uri: Url::from_file_path(&target).ok()?, range: start_of_file() })
}

/// Every place in other files that calls the action or sends the event `name` declared in `this_file`.
fn uses_in_other_files(ws: &Workspace, this_file: &Path, kind: ExternalKind, name: &str) -> Vec<Location> {
    let this_key = key_of(this_file);
    let mut found = Vec::new();
    for path in ws.all_propulsion_files() {
        let key = key_of(&path);
        if same_key(&key, &this_key) {
            continue;
        }
        let Some(text) = ws.source_for_key(&key) else { continue };
        let other = Index::build(&text);
        for ext in other.externals.iter().filter(|e| e.kind == kind && e.member == name) {
            let Some(rel) = other.symbol(ext.owner).file.as_ref() else { continue };
            let Some(dir) = path.parent() else { continue };
            if same_key(&key_of(&dir.join(rel)), &this_key) {
                if let Some(uri) = url_for_key(&key) {
                    found.push(location(uri, &other, ext.span));
                }
            }
        }
    }
    found
}

fn external_kind_of(kind: SymbolKind) -> Option<ExternalKind> {
    match kind {
        SymbolKind::Action => Some(ExternalKind::Action),
        SymbolKind::Event => Some(ExternalKind::Event),
        _ => None,
    }
}

pub fn references(ws: &Workspace, uri: &Url, position: Position, include_declaration: bool) -> Result<Vec<Location>, String> {
    let (path, index) = ws.index_of(uri)?;
    let (line, col) = cursor(&index, position);

    match thing_at(&index, line, col) {
        Some(Thing::Symbol { id }) => {
            let mut found: Vec<Location> = index
                .references(id)
                .filter(|o| include_declaration || !o.declaration)
                .map(|o| location(uri.clone(), &index, o.span))
                .collect();
            let s = index.symbol(id);
            if let Some(kind) = external_kind_of(s.kind) {
                found.extend(uses_in_other_files(ws, &path, kind, &s.name));
            }
            Ok(found)
        }
        Some(Thing::Member(i)) => {
            // Uses of the same action: the calls in this file, then the ones elsewhere, plus its declaration.
            let ext = &index.externals[i];
            let Some(Resolved::Remote(remote)) = resolve_member(ws, &path, &index, ext) else { return Ok(Vec::new()) };
            let Some(remote_uri) = url_for_key(&remote.key) else { return Ok(Vec::new()) };
            let remote_path = remote_uri.to_file_path().map_err(|_| "bad path".to_string())?;
            let s = remote.index.symbol(remote.symbol);
            let mut found = Vec::new();
            if include_declaration {
                found.push(location(remote_uri.clone(), &remote.index, s.decl));
            }
            found.extend(
                remote.index.references(remote.symbol).filter(|o| !o.declaration).map(|o| location(remote_uri.clone(), &remote.index, o.span)),
            );
            found.extend(uses_in_other_files(ws, &remote_path, ext.kind, &ext.member));
            Ok(found)
        }
        _ => Ok(Vec::new()),
    }
}

pub fn prepare_rename(ws: &Workspace, uri: &Url, position: Position) -> Result<Option<PrepareRenameResponse>, String> {
    let (_, index) = ws.index_of(uri)?;
    let (line, col) = cursor(&index, position);
    let Some(Thing::Symbol { id }) = thing_at(&index, line, col) else {
        return Err("only names declared in this file can be renamed".to_string());
    };
    let s = index.symbol(id);
    if matches!(s.kind, SymbolKind::System | SymbolKind::Component) {
        return Err(format!("`{}` is part of the engine, so it can't be renamed", s.name));
    }
    let span = index.occurrence_at(line, col).map(|o| o.span).unwrap();
    Ok(Some(PrepareRenameResponse::RangeWithPlaceholder {
        range: span_range(index.line_text(span.line), span),
        placeholder: s.name.clone(),
    }))
}

/// Why `name` can't be used as a new name for symbol `id`, if it can't.
fn check_new_name(index: &Index, id: usize, name: &str) -> Result<(), String> {
    let mut chars = name.chars();
    let valid = chars.next().is_some_and(|c| c.is_alphabetic() || c == '_') && chars.all(|c| c.is_alphanumeric() || c == '_');
    if !valid {
        return Err(format!("`{name}` isn't a valid name; use letters, digits and `_` with no spaces, like `MaxHealth`"));
    }
    if propulsion::index::KEYWORDS.contains(&name) {
        return Err(format!("`{name}` is a word the language uses, so it can't be a name"));
    }
    let scope = index.symbol(id).scope;
    // Names are unique within a file (SPEC 6a), so a clash with anything usable in the same lines is an error.
    if let Some((_, other)) = index
        .symbols
        .iter()
        .enumerate()
        .find(|(other_id, s)| *other_id != id && s.name == name && s.scope.0 <= scope.1 && scope.0 <= s.scope.1)
    {
        return Err(format!("`{name}` is already used in this file, on line {}", other.decl.line));
    }
    Ok(())
}

pub fn rename(ws: &Workspace, uri: &Url, position: Position, new_name: &str) -> Result<Option<WorkspaceEdit>, String> {
    let (path, index) = ws.index_of(uri)?;
    let (line, col) = cursor(&index, position);
    let Some(Thing::Symbol { id }) = thing_at(&index, line, col) else {
        return Err("only names declared in this file can be renamed".to_string());
    };
    let s = index.symbol(id);
    if matches!(s.kind, SymbolKind::System | SymbolKind::Component) {
        return Err(format!("`{}` is part of the engine, so it can't be renamed", s.name));
    }
    if new_name == s.name {
        return Ok(None);
    }
    check_new_name(&index, id, new_name)?;

    let mut changes: HashMap<Url, Vec<TextEdit>> = HashMap::new();
    let edit = |range: Range| TextEdit { range, new_text: new_name.to_string() };
    for o in index.references(id) {
        changes.entry(uri.clone()).or_default().push(edit(span_range(index.line_text(o.span.line), o.span)));
    }
    // An action or event can be used from other files; update those too, so nothing breaks.
    if let Some(kind) = external_kind_of(s.kind) {
        for loc in uses_in_other_files(ws, &path, kind, &s.name) {
            changes.entry(loc.uri).or_default().push(edit(loc.range));
        }
    }
    Ok(Some(WorkspaceEdit { changes: Some(changes), ..WorkspaceEdit::default() }))
}

#[allow(deprecated)] // `DocumentSymbol::deprecated` has to be filled in even though it is deprecated.
pub fn document_symbols(ws: &Workspace, uri: &Url) -> Result<Vec<DocumentSymbol>, String> {
    use lsp_types::SymbolKind as K;
    let (_, index) = ws.index_of(uri)?;
    let symbol = |name: String, detail: Option<String>, kind: K, range: Range, selection: Range, children| DocumentSymbol {
        name,
        detail,
        kind,
        tags: None,
        deprecated: None,
        range,
        selection_range: selection,
        children,
    };
    let mut out = Vec::new();

    // The header: what the file declares.
    for s in &index.symbols {
        let header_decl = s.scope.0 == 1
            && s.kind != SymbolKind::Action
            && index.section_at(s.decl.line) == Section::Header;
        if !header_decl {
            continue;
        }
        let kind = match s.kind {
            SymbolKind::Constant => K::CONSTANT,
            SymbolKind::Event => K::EVENT,
            SymbolKind::File => K::FILE,
            SymbolKind::System | SymbolKind::Component => K::MODULE,
            SymbolKind::Entity | SymbolKind::Entities => K::OBJECT,
            _ => K::VARIABLE,
        };
        let text = index.line_text(s.decl.line);
        let detail = s.ty.clone().or_else(|| Some(s.kind.label().to_string()));
        out.push(symbol(s.name.clone(), detail, kind, line_range(text, s.decl.line), span_range(text, s.decl), None));
    }

    // The implement section: handlers and actions, with the names each one makes.
    for h in &index.handlers {
        let first = index.line_tokens(h.line).first().map(|t| t.span).unwrap();
        let header_text = index.line_text(h.line);
        let last_text = index.line_text(h.end);
        let range = Range { start: line_range(header_text, h.line).start, end: line_range(last_text, h.end).end };
        let is_action = index.line_tokens(h.line).first().is_some_and(|t| t.text == "action");
        let (name, kind, selection) = match index.symbols.iter().find(|s| s.kind == SymbolKind::Action && s.decl.line == h.line) {
            Some(s) if is_action => (s.name.clone(), K::FUNCTION, span_range(header_text, s.decl)),
            _ => (h.text.trim_end_matches(':').to_string(), K::EVENT, span_range(header_text, first)),
        };
        let children: Vec<DocumentSymbol> = index
            .symbols
            .iter()
            .filter(|s| !matches!(s.kind, SymbolKind::Action) && s.scope.0 >= h.line && s.decl.line <= h.end && s.decl.line >= h.line)
            .filter(|s| matches!(s.kind, SymbolKind::Parameter | SymbolKind::Local | SymbolKind::Loop))
            .map(|s| {
                let text = index.line_text(s.decl.line);
                symbol(s.name.clone(), s.ty.clone(), K::VARIABLE, span_range(text, s.decl), span_range(text, s.decl), None)
            })
            .collect();
        out.push(symbol(name, None, kind, range, selection, (!children.is_empty()).then_some(children)));
    }
    Ok(out)
}
