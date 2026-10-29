//! What the server knows about the files being edited: the text of open files, and the folders
//! to look in for the rest.

use propulsion::index::Index;
use propulsion::{Manifest, Project};
use lsp_types::Url;
use std::collections::HashMap;
use std::path::{Path, PathBuf};

/// A file the editor has open.
pub struct Document {
    pub text: String,
    pub version: i32,
}

pub struct Workspace {
    pub open: HashMap<Url, Document>,
    /// The folders the editor has open. Rename and find-references look for other files here.
    pub roots: Vec<PathBuf>,
    pub manifest: Manifest,
}

/// A file in the form the compiler names it, so the same file always compares equal.
pub fn key_of(path: &Path) -> String {
    Project::key_for(path)
}

/// Whether two keys name the same file. Windows paths ignore case.
pub fn same_key(a: &str, b: &str) -> bool {
    if cfg!(windows) {
        a.eq_ignore_ascii_case(b)
    } else {
        a == b
    }
}

pub fn url_for_key(key: &str) -> Option<Url> {
    Url::from_file_path(Path::new(key)).ok()
}

impl Workspace {
    pub fn new(roots: Vec<PathBuf>) -> Workspace {
        // A game defines its own Input Actions (an inputs file next to its scripts): complete and check against those.
        if let Some(root) = roots.first() {
            propulsion::use_inputs_near(root);
        }
        Workspace { open: HashMap::new(), roots, manifest: Manifest::builtin() }
    }

    /// The text of the open files, by the compiler's key, so unsaved edits are what gets checked.
    pub fn overrides(&self) -> HashMap<String, String> {
        self.open
            .iter()
            .filter_map(|(uri, doc)| Some((key_of(&uri.to_file_path().ok()?), doc.text.clone())))
            .collect()
    }

    /// The current text of a file: what the editor has, else what is on disk.
    pub fn source_for_key(&self, key: &str) -> Option<String> {
        for (uri, doc) in &self.open {
            if uri.to_file_path().ok().is_some_and(|p| same_key(&key_of(&p), key)) {
                return Some(doc.text.clone());
            }
        }
        std::fs::read_to_string(key).ok()
    }

    /// The index of the file at `uri`, with its path.
    pub fn index_of(&self, uri: &Url) -> Result<(PathBuf, Index), String> {
        let path = uri.to_file_path().map_err(|_| format!("{uri} is not a file"))?;
        let text = match self.open.get(uri) {
            Some(doc) => doc.text.clone(),
            None => std::fs::read_to_string(&path).map_err(|e| format!("can't read {}: {e}", path.display()))?,
        };
        Ok((path, Index::build(&text)))
    }

    /// Every `.prop` file in the open folders and every open file.
    pub fn all_propulsion_files(&self) -> Vec<PathBuf> {
        fn walk(dir: &Path, out: &mut Vec<PathBuf>) {
            let Ok(entries) = std::fs::read_dir(dir) else { return };
            for entry in entries.flatten() {
                let path = entry.path();
                if path.is_dir() {
                    let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("");
                    if !matches!(name, "target" | "node_modules" | ".git" | "build" | ".vs") && out.len() < 5000 {
                        walk(&path, out);
                    }
                } else if path.extension().is_some_and(|e| e == "prop") {
                    out.push(path);
                }
            }
        }
        let mut files = Vec::new();
        for root in &self.roots {
            walk(root, &mut files);
        }
        for uri in self.open.keys() {
            if let Ok(path) = uri.to_file_path() {
                files.push(path);
            }
        }
        files.sort_by_key(|p| key_of(p).to_lowercase());
        files.dedup_by(|a, b| same_key(&key_of(a), &key_of(b)));
        files
    }
}
