//! Hover: what a name is, in a short PropulsionScript-looking summary.

use crate::resolve::{axes_of, resolve_member, thing_at, EngineAction, Remote, Resolved, Thing};
use crate::text::span_range;
use crate::workspace::Workspace;
use propulsion::index::{Index, Param, Symbol, SymbolKind};
use propulsion::Manifest;
use lsp_types::{Hover, HoverContents, MarkupContent, MarkupKind, Position, Url};

pub fn hover(ws: &Workspace, uri: &Url, position: Position) -> Result<Option<Hover>, String> {
    let (path, index) = ws.index_of(uri)?;
    let (line, col) = crate::text::index_position(index.line_text(position.line as usize + 1), position);

    let (markdown, span) = match thing_at(&index, line, col) {
        Some(Thing::Symbol { id }) => {
            let span = index.occurrence_at(line, col).map(|o| o.span).unwrap();
            (symbol_markdown(index.symbol(id), &ws.manifest), span)
        }
        Some(Thing::Member(i)) => {
            let ext = &index.externals[i];
            let text = match resolve_member(ws, &path, &index, ext) {
                Some(Resolved::Remote(r)) => remote_markdown(&r),
                Some(Resolved::Engine(a)) => engine_action_markdown(&a),
                None => {
                    let owner = &index.symbol(ext.owner).name;
                    format!("`{}` on `{owner}`: not found. The checker will say more.", ext.member)
                }
            };
            (text, ext.span)
        }
        Some(Thing::Field { path, part }) => {
            let p = &index.paths[path];
            match field_markdown(&index, p.segments.iter().map(|(n, _)| n.as_str()).collect::<Vec<_>>().as_slice(), part, &ws.manifest) {
                Some(text) => (text, p.segments[part].1),
                None => return Ok(None),
            }
        }
        Some(Thing::FilePath(i)) => {
            let r = &index.file_refs[i];
            (format!("Script file `{}`.\n\nCtrl+click to open it.", r.path), r.span)
        }
        None => return Ok(None),
    };

    let range = span_range(index.line_text(span.line), span);
    Ok(Some(Hover {
        contents: HoverContents::Markup(MarkupContent { kind: MarkupKind::Markdown, value: markdown }),
        range: Some(range),
    }))
}

pub fn code_block(text: &str) -> String {
    format!("```propulsionscript\n{text}\n```")
}

pub fn signature(name: &str, params: &[Param], gives: Option<&str>) -> String {
    let mut s = format!("action {name}");
    for p in params {
        s.push_str(&format!(" {}:{}", p.name, p.ty));
    }
    if let Some(g) = gives {
        s.push_str(&format!(" gives {g}"));
    }
    s
}

fn with_doc(code: String, doc: Option<&str>) -> String {
    match doc {
        Some(d) => format!("{}\n\n{d}", code_block(&code)),
        None => code_block(&code),
    }
}

pub fn symbol_markdown(s: &Symbol, manifest: &Manifest) -> String {
    let doc = s.doc.as_deref();
    let ty = s.ty.as_deref().unwrap_or("?");
    match s.kind {
        SymbolKind::Variable => {
            let mut code = format!("variable {}: {ty}", s.name);
            if let Some(v) = &s.value {
                code.push_str(&format!(" = {v}"));
            }
            if s.exposed {
                code.push_str("  (exposed)");
            }
            with_doc(code, doc)
        }
        SymbolKind::Constant => {
            let value = s.value.as_ref().map(|v| format!(" = {v}")).unwrap_or_default();
            with_doc(format!("constant {}: {ty}{value}", s.name), doc)
        }
        SymbolKind::Parameter => with_doc(format!("parameter {}: {ty}", s.name), doc),
        SymbolKind::Loop => with_doc(format!("loop variable {}: {}", s.name, s.ty.as_deref().unwrap_or("item of the list")), doc),
        SymbolKind::Local => match (&s.file, s.ty.as_deref()) {
            (Some(file), _) => with_doc(format!("{}: entity running \"{file}\"", s.name), doc),
            (None, Some(t)) => with_doc(format!("{}: {t}", s.name), doc),
            (None, None) => with_doc(format!("remembered value {}", s.name), doc),
        },
        SymbolKind::Entity => {
            let mut code = format!("entity {}", s.name);
            if let Some(tag) = &s.tag {
                code.push_str(&format!(" tagged \"{tag}\""));
            }
            if let Some(file) = &s.file {
                code.push_str(&format!(" running \"{file}\""));
            }
            with_doc(code, doc)
        }
        SymbolKind::Entities => {
            let tag = s.tag.as_ref().map(|t| format!(" tagged \"{t}\"")).unwrap_or_default();
            with_doc(format!("entities {}: list of entity{tag}", s.name), doc)
        }
        SymbolKind::File => {
            let file = s.file.as_ref().map(|f| format!(" = \"{f}\"")).unwrap_or_default();
            with_doc(format!("file {}{file}", s.name), doc)
        }
        SymbolKind::Event => with_doc(format!("event {}", s.name), doc),
        SymbolKind::Latch => with_doc(format!("{}: a named `only once`, `first N times` or `alternate`; `reset {}` starts it over", s.name, s.name), doc),
        SymbolKind::Action => with_doc(signature(&s.name, &s.params, s.gives.as_deref()), doc),
        SymbolKind::Component => {
            let mut text = with_doc(format!("component {}", s.name), doc);
            if let Some(fields) = manifest.components.get(&s.name) {
                text.push_str("\n\nFields:\n");
                for (name, ty) in fields {
                    text.push_str(&format!("- `{}.{name}`: {ty}\n", s.name));
                }
            }
            text
        }
        SymbolKind::System => {
            let mut text = with_doc(format!("system {}", s.name), doc);
            if let Some(def) = manifest.systems.get(&s.name) {
                let mut actions: Vec<_> = def.actions.iter().collect();
                actions.sort_by_key(|(n, _)| n.as_str());
                if !actions.is_empty() {
                    text.push_str("\n\nActions:\n");
                    for (name, params) in actions {
                        let args: String = params.iter().map(|(n, t)| format!(" {n}:{t}")).collect();
                        text.push_str(&format!("- `do {name} on {}{args}`\n", s.name));
                    }
                }
                if !def.inputs.is_empty() {
                    text.push_str(&format!("\nInputs: {}\n", def.inputs.join(", ")));
                }
                if !def.axes.is_empty() {
                    text.push_str(&format!("\nAxes: {}\n", def.axes.join(", ")));
                }
            }
            text
        }
    }
}

fn remote_markdown(r: &Remote) -> String {
    let s = r.index.symbol(r.symbol);
    let from = r.key.rsplit('/').next().unwrap_or(&r.key);
    format!("{}\n\nfrom `{from}`", symbol_markdown(s, &Manifest::default()))
}

fn engine_action_markdown(a: &EngineAction) -> String {
    let args: String = a.params.iter().map(|(n, t)| format!(" {n}:{t}")).collect();
    format!("{}\n\nan action of the `{}` system", code_block(&format!("do {} on {}{args}", a.name, a.system)), a.system)
}

fn field_markdown(index: &Index, segments: &[&str], part: usize, manifest: &Manifest) -> Option<String> {
    let component = segments[0];
    // Only components declared in the header have fields.
    index.symbols.iter().find(|s| s.name == component && s.kind == SymbolKind::Component)?;
    let fields = manifest.components.get(component)?;
    let (name, ty) = fields.iter().find(|(n, _)| n == segments[1])?;
    if part == 1 {
        return Some(format!("{}\n\na field of `{component}`", code_block(&format!("{component}.{name}: {ty}"))));
    }
    let axes = axes_of(ty);
    axes.contains(&segments[2]).then(|| format!("{}\n\none axis of a `{ty}`", code_block(&format!("{}: decimal", segments[2]))))
}
