//! `propulsion-lsp`: the PropulsionScript language server. It speaks LSP over stdio.
//!
//! It checks every open `.prop` file with the real compiler front end and publishes the errors,
//! and answers editor requests (colours, hover, go to definition, completion, rename, ...) from
//! a symbol index of the file (`propulsion::index`).

mod completion;
mod diagnostics;
mod hover;
mod navigate;
mod resolve;
mod semantic;
mod text;
mod workspace;

#[cfg(test)]
mod tests;

use lsp_server::{Connection, ErrorCode, Message, Notification, Request, Response};
use lsp_types::notification::{
    DidChangeTextDocument, DidCloseTextDocument, DidOpenTextDocument, DidSaveTextDocument, Notification as _,
    PublishDiagnostics,
};
use lsp_types::request::{
    Completion, DocumentSymbolRequest, GotoDefinition, HoverRequest, PrepareRenameRequest, References, Rename,
    SemanticTokensFullRequest,
};
use lsp_types::{
    CompletionOptions, CompletionResponse, DocumentSymbolResponse, InitializeParams, OneOf, PublishDiagnosticsParams,
    RenameOptions, SemanticTokensFullOptions, SemanticTokensOptions, SemanticTokensResult,
    SemanticTokensServerCapabilities, ServerCapabilities, TextDocumentSyncCapability, TextDocumentSyncKind, Url,
    WorkDoneProgressOptions,
};
use serde::de::DeserializeOwned;
use serde::Serialize;
use std::collections::HashMap;
use std::error::Error;
use std::path::PathBuf;
use workspace::{Document, Workspace};

type Result<T> = std::result::Result<T, Box<dyn Error + Sync + Send>>;

fn capabilities() -> ServerCapabilities {
    ServerCapabilities {
        // The whole file is sent on every change. Files are small and this keeps the server simple.
        text_document_sync: Some(TextDocumentSyncCapability::Kind(TextDocumentSyncKind::FULL)),
        hover_provider: Some(lsp_types::HoverProviderCapability::Simple(true)),
        definition_provider: Some(OneOf::Left(true)),
        references_provider: Some(OneOf::Left(true)),
        document_symbol_provider: Some(OneOf::Left(true)),
        rename_provider: Some(OneOf::Right(RenameOptions {
            prepare_provider: Some(true),
            work_done_progress_options: WorkDoneProgressOptions::default(),
        })),
        completion_provider: Some(CompletionOptions {
            trigger_characters: Some(vec![".".into(), ":".into(), " ".into()]),
            ..CompletionOptions::default()
        }),
        semantic_tokens_provider: Some(SemanticTokensServerCapabilities::SemanticTokensOptions(SemanticTokensOptions {
            work_done_progress_options: WorkDoneProgressOptions::default(),
            legend: semantic::legend(),
            range: None,
            full: Some(SemanticTokensFullOptions::Bool(true)),
        })),
        ..ServerCapabilities::default()
    }
}

/// The folders the editor has open.
fn roots_of(params: &InitializeParams) -> Vec<PathBuf> {
    let mut roots: Vec<PathBuf> = params
        .workspace_folders
        .iter()
        .flatten()
        .filter_map(|f| f.uri.to_file_path().ok())
        .collect();
    #[allow(deprecated)] // Older editors only send `root_uri`.
    if roots.is_empty() {
        roots.extend(params.root_uri.as_ref().and_then(|u| u.to_file_path().ok()));
    }
    roots
}

fn main() -> Result<()> {
    let (connection, io_threads) = Connection::stdio();
    let init = connection.initialize(serde_json::to_value(capabilities())?)?;
    let params: InitializeParams = serde_json::from_value(init)?;

    let mut server = Server { connection: &connection, ws: Workspace::new(roots_of(&params)) };
    server.run()?;
    io_threads.join()?;
    Ok(())
}

struct Server<'a> {
    connection: &'a Connection,
    ws: Workspace,
}

impl Server<'_> {
    fn run(&mut self) -> Result<()> {
        for message in &self.connection.receiver {
            match message {
                Message::Request(request) => {
                    if self.connection.handle_shutdown(&request)? {
                        return Ok(());
                    }
                    self.request(request)?;
                }
                Message::Notification(notification) => self.notification(notification)?,
                Message::Response(_) => {}
            }
        }
        Ok(())
    }

    // ---- requests ----

    fn request(&self, request: Request) -> Result<()> {
        let mut pending = Some(request);
        self.on::<HoverRequest>(&mut pending, |ws, p| {
            let at = p.text_document_position_params;
            hover::hover(ws, &at.text_document.uri, at.position)
        })?;
        self.on::<GotoDefinition>(&mut pending, |ws, p| {
            let at = p.text_document_position_params;
            navigate::definition(ws, &at.text_document.uri, at.position)
        })?;
        self.on::<References>(&mut pending, |ws, p| {
            let at = p.text_document_position;
            navigate::references(ws, &at.text_document.uri, at.position, p.context.include_declaration).map(Some)
        })?;
        self.on::<PrepareRenameRequest>(&mut pending, |ws, p| navigate::prepare_rename(ws, &p.text_document.uri, p.position))?;
        self.on::<Rename>(&mut pending, |ws, p| {
            let at = p.text_document_position;
            navigate::rename(ws, &at.text_document.uri, at.position, &p.new_name)
        })?;
        self.on::<DocumentSymbolRequest>(&mut pending, |ws, p| {
            navigate::document_symbols(ws, &p.text_document.uri).map(|s| Some(DocumentSymbolResponse::Nested(s)))
        })?;
        self.on::<SemanticTokensFullRequest>(&mut pending, |ws, p| {
            semantic::tokens(ws, &p.text_document.uri).map(|t| Some(SemanticTokensResult::Tokens(t)))
        })?;
        self.on::<Completion>(&mut pending, |ws, p| {
            let at = p.text_document_position;
            let typed_space = p.context.as_ref().and_then(|c| c.trigger_character.as_deref()) == Some(" ");
            let found = completion::complete(ws, &at.text_document.uri, at.position)?;
            // A space only triggers completion where what comes next is clear.
            let items = if typed_space && !found.specific { Vec::new() } else { found.items };
            Ok(Some(CompletionResponse::Array(items)))
        })?;

        if let Some(request) = pending {
            let response = Response::new_err(
                request.id,
                ErrorCode::MethodNotFound as i32,
                format!("propulsion-lsp does not support `{}`", request.method),
            );
            self.connection.sender.send(Message::Response(response))?;
        }
        Ok(())
    }

    /// If `pending` is a request of type `R`, answer it with `handler` and clear it.
    fn on<R>(
        &self,
        pending: &mut Option<Request>,
        handler: impl FnOnce(&Workspace, R::Params) -> std::result::Result<R::Result, String>,
    ) -> Result<()>
    where
        R: lsp_types::request::Request,
        R::Params: DeserializeOwned,
        R::Result: Serialize,
    {
        let Some(request) = pending.take_if(|r| r.method == R::METHOD) else { return Ok(()) };
        let response = match request.extract::<R::Params>(R::METHOD) {
            Ok((id, params)) => match handler(&self.ws, params) {
                Ok(result) => Response::new_ok(id, result),
                // The message is shown to the person, for example as the reason a rename was refused.
                Err(message) => Response::new_err(id, ErrorCode::RequestFailed as i32, message),
            },
            Err(lsp_server::ExtractError::JsonError { method, error }) => {
                return Err(format!("bad parameters for {method}: {error}").into())
            }
            Err(lsp_server::ExtractError::MethodMismatch(_)) => return Ok(()),
        };
        self.connection.sender.send(Message::Response(response))?;
        Ok(())
    }

    // ---- notifications ----

    fn notification(&mut self, n: Notification) -> Result<()> {
        match n.method.as_str() {
            DidOpenTextDocument::METHOD => {
                let p: lsp_types::DidOpenTextDocumentParams = serde_json::from_value(n.params)?;
                let doc = p.text_document;
                self.ws.open.insert(doc.uri, Document { text: doc.text, version: doc.version });
                self.check_all()
            }
            DidChangeTextDocument::METHOD => {
                let p: lsp_types::DidChangeTextDocumentParams = serde_json::from_value(n.params)?;
                // The sync mode is FULL, so the last change holds the whole text.
                if let Some(change) = p.content_changes.into_iter().last() {
                    let doc = p.text_document;
                    self.ws.open.insert(doc.uri, Document { text: change.text, version: doc.version });
                }
                self.check_all()
            }
            DidSaveTextDocument::METHOD => self.check_all(),
            DidCloseTextDocument::METHOD => {
                let p: lsp_types::DidCloseTextDocumentParams = serde_json::from_value(n.params)?;
                self.ws.open.remove(&p.text_document.uri);
                // Clear what was shown for the file, then re-check the rest: they may have needed it.
                self.publish(p.text_document.uri, Vec::new(), None)?;
                self.check_all()
            }
            _ => Ok(()),
        }
    }

    /// Check every open file. A change in one file can break another that needs it, and a check
    /// takes milliseconds, so it's simpler to check them all than to track who needs whom.
    fn check_all(&self) -> Result<()> {
        let overrides: HashMap<String, String> = self.ws.overrides();

        for (uri, doc) in &self.ws.open {
            let Ok(path) = uri.to_file_path() else { continue };
            let found = match diagnostics::diagnose(&path, &overrides) {
                Ok(found) => found,
                Err(e) => {
                    eprintln!("propulsion-lsp: couldn't check {}: {e}", path.display());
                    continue;
                }
            };
            self.publish(uri.clone(), found, Some(doc.version))?;
        }
        Ok(())
    }

    fn publish(&self, uri: Url, diagnostics: Vec<lsp_types::Diagnostic>, version: Option<i32>) -> Result<()> {
        let params = PublishDiagnosticsParams { uri, diagnostics, version };
        let notification = Notification::new(PublishDiagnostics::METHOD.to_string(), params);
        self.connection.sender.send(Message::Notification(notification))?;
        Ok(())
    }
}
