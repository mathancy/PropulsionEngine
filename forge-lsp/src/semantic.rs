//! Semantic tokens: colour every name by what it is (variable, constant, parameter, action, ...),
//! which a grammar alone can't know.

use crate::workspace::Workspace;
use forge::index::{ExternalKind, Span, SymbolKind};
use lsp_types::{
    SemanticToken, SemanticTokenModifier, SemanticTokenType, SemanticTokens, SemanticTokensLegend, Url,
};

const VARIABLE: u32 = 0;
const PARAMETER: u32 = 1;
const FUNCTION: u32 = 2;
const EVENT: u32 = 3;
const CLASS: u32 = 4;
const INTERFACE: u32 = 5;
const NAMESPACE: u32 = 6;
const PROPERTY: u32 = 7;

const DECLARATION: u32 = 1 << 0;
const READONLY: u32 = 1 << 1;

pub fn legend() -> SemanticTokensLegend {
    SemanticTokensLegend {
        token_types: vec![
            SemanticTokenType::VARIABLE,
            SemanticTokenType::PARAMETER,
            SemanticTokenType::FUNCTION,
            SemanticTokenType::EVENT,
            SemanticTokenType::CLASS,
            SemanticTokenType::INTERFACE,
            SemanticTokenType::NAMESPACE,
            SemanticTokenType::PROPERTY,
        ],
        token_modifiers: vec![SemanticTokenModifier::DECLARATION, SemanticTokenModifier::READONLY],
    }
}

fn token_type(kind: SymbolKind) -> (u32, u32) {
    match kind {
        SymbolKind::Variable | SymbolKind::Local | SymbolKind::Loop => (VARIABLE, 0),
        SymbolKind::Constant => (VARIABLE, READONLY),
        SymbolKind::Parameter => (PARAMETER, 0),
        SymbolKind::Entity | SymbolKind::Entities => (INTERFACE, 0),
        SymbolKind::File | SymbolKind::System => (NAMESPACE, 0),
        SymbolKind::Component => (CLASS, 0),
        SymbolKind::Event | SymbolKind::Latch => (EVENT, 0),
        SymbolKind::Action => (FUNCTION, 0),
    }
}

pub fn tokens(ws: &Workspace, uri: &Url) -> Result<SemanticTokens, String> {
    let (_, index) = ws.index_of(uri)?;

    // (span, type, modifiers)
    let mut found: Vec<(Span, u32, u32)> = Vec::new();
    for o in &index.occurrences {
        let (ty, mods) = token_type(index.symbol(o.symbol).kind);
        found.push((o.span, ty, mods | if o.declaration { DECLARATION } else { 0 }));
    }
    for e in &index.externals {
        found.push((e.span, if e.kind == ExternalKind::Action { FUNCTION } else { EVENT }, 0));
    }
    for p in &index.paths {
        for (_, span) in p.segments.iter().skip(1) {
            found.push((*span, PROPERTY, 0));
        }
    }
    found.sort_by_key(|(s, _, _)| (s.line, s.col));

    let mut data = Vec::with_capacity(found.len());
    let (mut prev_line, mut prev_start) = (0u32, 0u32);
    for (span, ty, mods) in found {
        let text = index.line_text(span.line);
        let range = crate::text::span_range(text, span);
        let (line, start) = (range.start.line, range.start.character);
        let delta_line = line - prev_line;
        let delta_start = if delta_line == 0 { start - prev_start } else { start };
        data.push(SemanticToken {
            delta_line,
            delta_start,
            length: range.end.character - range.start.character,
            token_type: ty,
            token_modifiers_bitset: mods,
        });
        prev_line = line;
        prev_start = start;
    }
    Ok(SemanticTokens { result_id: None, data })
}
