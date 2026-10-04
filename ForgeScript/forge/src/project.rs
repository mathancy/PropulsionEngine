//! Loading a `.forge` file together with every file it depends on.
//!
//! Paths in `needs file:` / `needs entity ... file:` are relative to the file that mentions them.

use crate::ast::*;
use crate::check;
use crate::error::ForgeError;
use crate::manifest::Manifest;
use crate::parser::parse;
use crate::ty::Type;
use crate::typecheck;
use std::collections::{BTreeMap, HashMap, HashSet};
use std::path::{Component, Path, PathBuf};

#[derive(Debug, Clone, Default)]
pub struct ActionSig {
    pub params: Vec<(String, Type)>,
    pub gives: Option<Type>,
}

/// What other files can see of a file: its actions and the events it can send.
#[derive(Debug, Clone, Default)]
pub struct FileSig {
    pub actions: HashMap<String, ActionSig>,
    pub events: HashSet<String>,
}

impl FileSig {
    fn from_ast(file: &File) -> FileSig {
        let mut sig = FileSig::default();
        for d in &file.header {
            if let DeclKind::Event(name) = &d.kind {
                sig.events.insert(name.clone());
            }
        }
        for item in &file.implement {
            if let ItemKind::Action { name, params, gives, .. } = &item.kind {
                sig.actions.insert(
                    name.clone(),
                    ActionSig {
                        params: params.iter().map(|p| (p.name.clone(), Type::from_expr(&p.ty))).collect(),
                        gives: gives.as_ref().map(Type::from_expr),
                    },
                );
            }
        }
        sig
    }
}

#[derive(Debug, Clone)]
pub struct LoadedFile {
    pub path: String,
    pub source: String,
    pub parsed: Result<File, ForgeError>,
    pub sig: FileSig,
    /// For each path written in a `needs`, the key of the loaded file or the reason it couldn't be loaded.
    pub deps: HashMap<String, Result<String, String>>,
}

/// The problems found in one file.
#[derive(Debug, Clone)]
pub struct FileReport {
    pub path: String,
    pub source: String,
    pub errors: Vec<ForgeError>,
}

#[derive(Debug, Clone)]
pub struct Project {
    pub entry: String,
    pub files: BTreeMap<String, LoadedFile>,
}

impl Project {
    /// Load `entry` and everything it needs. Only fails if `entry` itself can't be read;
    /// problems in any file (including parse errors) are reported by `check`.
    pub fn load(entry: &Path) -> Result<Project, String> {
        Project::load_with(entry, &HashMap::new())
    }

    /// Like `load`, but a file whose key (see `key_for`) is in `overrides` is read from that text
    /// instead of from disk. This is for an editor, where the file being edited isn't saved yet.
    pub fn load_with(entry: &Path, overrides: &HashMap<String, String>) -> Result<Project, String> {
        let mut project = Project { entry: String::new(), files: BTreeMap::new() };
        project.entry = project.load_file(entry, overrides)?;
        Ok(project)
    }

    /// Load several entry files, and everything they need. The first is the project's `entry`.
    /// This is for a host (the editor) that runs many scripts in one world.
    pub fn load_many(entries: &[PathBuf]) -> Result<Project, String> {
        let Some((first, rest)) = entries.split_first() else {
            return Err("there are no scripts to load".to_string());
        };
        let mut project = Project::load(first)?;
        for path in rest {
            project.load_file(path, &HashMap::new())?;
        }
        Ok(project)
    }

    /// The key a file has in a project, so a host can find the file it loaded by path.
    pub fn key_for(path: &Path) -> String {
        normalize(path)
    }

    fn load_file(&mut self, path: &Path, overrides: &HashMap<String, String>) -> Result<String, String> {
        let key = normalize(path);
        if self.files.contains_key(&key) {
            return Ok(key);
        }
        let source = match overrides.get(&key) {
            Some(text) => text.clone(),
            None => std::fs::read_to_string(path).map_err(|e| {
                if e.kind() == std::io::ErrorKind::NotFound {
                    format!("there is no file at `{key}`")
                } else {
                    format!("can't read `{key}`: {e}")
                }
            })?,
        };
        let parsed = parse(&source);
        // Insert before following dependencies so files that need each other don't loop forever.
        self.files.insert(
            key.clone(),
            LoadedFile {
                path: key.clone(),
                source,
                parsed: parsed.clone(),
                sig: FileSig::default(),
                deps: HashMap::new(),
            },
        );
        if let Ok(file) = &parsed {
            let dir = path.parent().map(Path::to_path_buf).unwrap_or_default();
            let mut deps = HashMap::new();
            for rel in file_refs(file) {
                let result = self.load_file(&dir.join(&rel), overrides);
                deps.insert(rel, result);
            }
            let loaded = self.files.get_mut(&key).expect("just inserted");
            loaded.sig = FileSig::from_ast(file);
            loaded.deps = deps;
        }
        Ok(key)
    }

    /// Check every loaded file: syntax, names, and types.
    pub fn check(&self, manifest: &Manifest) -> Vec<FileReport> {
        self.files
            .iter()
            .map(|(key, loaded)| {
                let mut errors = match &loaded.parsed {
                    Err(e) => vec![e.clone()],
                    Ok(ast) => {
                        let mut errs = check::check(ast);
                        errs.extend(typecheck::typecheck(key, self, manifest));
                        errs
                    }
                };
                errors.sort_by_key(|e| (e.line, e.col));
                FileReport { path: key.clone(), source: loaded.source.clone(), errors }
            })
            .collect()
    }
}

/// Load `path` and its dependencies and check them against the built-in engine manifest.
pub fn check_path(path: &Path) -> Result<Vec<FileReport>, String> {
    let project = Project::load(path)?;
    Ok(project.check(&Manifest::builtin()))
}

fn file_refs(file: &File) -> Vec<String> {
    let mut refs = Vec::new();
    for d in &file.header {
        match &d.kind {
            DeclKind::NeedsFile { file, .. } | DeclKind::NeedsEntity { file, .. } => refs.push(file.clone()),
            DeclKind::NeedsEntities { file: Some(file), .. } => refs.push(file.clone()),
            _ => {}
        }
    }
    refs
}

/// Collapse `.` and `..` and use forward slashes, so the same file always gets the same key.
fn normalize(p: &Path) -> String {
    let mut prefix = String::new();
    let mut rooted = false;
    let mut parts: Vec<String> = Vec::new();
    for c in p.components() {
        match c {
            Component::Prefix(pre) => prefix = pre.as_os_str().to_string_lossy().into_owned(),
            Component::RootDir => rooted = true,
            Component::CurDir => {}
            Component::ParentDir => {
                if matches!(parts.last(), Some(last) if last != "..") {
                    parts.pop();
                } else {
                    parts.push("..".into());
                }
            }
            Component::Normal(s) => parts.push(s.to_string_lossy().into_owned()),
        }
    }
    format!("{prefix}{}{}", if rooted { "/" } else { "" }, parts.join("/"))
}
