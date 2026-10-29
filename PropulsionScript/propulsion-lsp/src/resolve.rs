//! Working out what the cursor is on, and what it refers to.

use crate::workspace::{key_of, Workspace};
use propulsion::index::{External, ExternalKind, Index, SymbolKind};
use propulsion::ty::Type;
use std::path::Path;

/// What is under the cursor.
pub enum Thing {
    /// A name from this file: an index into `Index::symbols`.
    Symbol { id: usize },
    /// The `TakeDamage` in `do TakeDamage on Player`: index into `Index::externals`.
    Member(usize),
    /// A field in a path like `Transform.Loc.X`: index into `Index::paths`, and which part.
    Field { path: usize, part: usize },
    /// The text in `file:"player.prop"`: index into `Index::file_refs`.
    FilePath(usize),
}

pub fn thing_at(index: &Index, line: usize, col: usize) -> Option<Thing> {
    if let Some(o) = index.occurrence_at(line, col) {
        return Some(Thing::Symbol { id: o.symbol });
    }
    if let Some(i) = index.externals.iter().position(|e| e.span.contains(line, col)) {
        return Some(Thing::Member(i));
    }
    for (path, p) in index.paths.iter().enumerate() {
        if let Some(part) = p.segments.iter().position(|(_, s)| s.contains(line, col)) {
            if part > 0 {
                return Some(Thing::Field { path, part });
            }
        }
    }
    index.file_refs.iter().position(|f| f.span.contains(line, col)).map(Thing::FilePath)
}

/// An action or event in another file.
pub struct Remote {
    /// The compiler's key for that file.
    pub key: String,
    pub index: Index,
    pub symbol: usize,
}

/// An action of an engine system, from the manifest.
pub struct EngineAction {
    pub system: String,
    pub name: String,
    pub params: Vec<(String, Type)>,
}

pub enum Resolved {
    Remote(Remote),
    Engine(EngineAction),
}

/// Find what `ext` (the member of a call on another file or system) refers to.
pub fn resolve_member(ws: &Workspace, this_file: &Path, index: &Index, ext: &External) -> Option<Resolved> {
    let owner = index.symbol(ext.owner);
    if owner.kind == SymbolKind::System {
        let params = ws.manifest.systems.get(&owner.name)?.actions.get(&ext.member)?.clone();
        return Some(Resolved::Engine(EngineAction { system: owner.name.clone(), name: ext.member.clone(), params }));
    }
    let rel = owner.file.as_ref()?;
    let key = key_of(&this_file.parent()?.join(rel));
    let remote_index = Index::build(&ws.source_for_key(&key)?);
    let wanted = match ext.kind {
        ExternalKind::Action => SymbolKind::Action,
        ExternalKind::Event => SymbolKind::Event,
    };
    let symbol = remote_index.symbols.iter().position(|s| s.kind == wanted && s.name == ext.member)?;
    Some(Resolved::Remote(Remote { key, index: remote_index, symbol }))
}

/// The axes a vector-like type has, for completing `Transform.Loc.`.
pub fn axes_of(ty: &Type) -> &'static [&'static str] {
    match ty {
        Type::Vector2 => &["X", "Y"],
        Type::Vector3 => &["X", "Y", "Z"],
        Type::Color => &["R", "G", "B", "A"],
        _ => &[],
    }
}
