//! What the engine offers to scripts: components, systems, and their actions.
//!
//! The spec (decision 3) plans a generated `engine.propapi` file. Until the engine side
//! exists, `Manifest::builtin()` describes the parts the examples use.

use crate::ty::Type;
use std::collections::HashMap;
use std::sync::RwLock;

/// The Input Actions a game defines, which scripts may read: buttons (`Input is pressing jump`) and axes
/// (`axis moveForward of Input`).
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct InputActions {
    pub buttons: Vec<String>,
    pub axes: Vec<String>,
}

/// Set by an engine whose input is data (Input Action assets): the names scripts are checked against, in place of the
/// built-in list below. Process-wide, because every check (a session, the graph editor, the compiler) starts from
/// `Manifest::builtin()`.
static PROJECT_INPUTS: RwLock<Option<InputActions>> = RwLock::new(None);

/// Replace the input names scripts are checked against (`None` goes back to the built-in list).
pub fn set_project_inputs(inputs: Option<InputActions>) {
    *PROJECT_INPUTS.write().unwrap_or_else(|e| e.into_inner()) = inputs;
}

/// The name of the file an engine writes next to a project's scripts, listing its Input Actions, so tools that don't
/// run inside the engine (`propc`, the editor's language server) check scripts against the same names:
///
/// ```text
/// // Input Actions of this project
/// button jump
/// axis moveForward
/// ```
pub const INPUTS_FILE: &str = "inputs.propapi";

/// The Input Actions in the text of an inputs file (`button <name>` and `axis <name>` lines; `//` starts a comment).
pub fn parse_inputs(text: &str) -> InputActions {
    let mut inputs = InputActions::default();
    for line in text.lines() {
        let line = line.split("//").next().unwrap_or("").trim();
        let mut words = line.split_whitespace();
        match (words.next(), words.next(), words.next()) {
            (Some("button"), Some(name), None) => inputs.buttons.push(name.to_string()),
            (Some("axis"), Some(name), None) => inputs.axes.push(name.to_string()),
            _ => {}
        }
    }
    inputs
}

/// Look for the project's inputs file near `path` (a script, or a folder): in its folder and the folder's `Scripts`
/// folder, then in each folder above it, a few levels up.
pub fn inputs_near(path: &std::path::Path) -> Option<InputActions> {
    let mut dir = if path.is_dir() { Some(path.to_path_buf()) } else { path.parent().map(|p| p.to_path_buf()) };
    for _ in 0..5 {
        let here = dir?;
        for candidate in [here.join(INPUTS_FILE), here.join("Scripts").join(INPUTS_FILE)] {
            if let Ok(text) = std::fs::read_to_string(&candidate) {
                return Some(parse_inputs(&text));
            }
        }
        dir = here.parent().map(|p| p.to_path_buf());
    }
    None
}

/// Check scripts near `path` against their project's inputs file, or against the built-in names if there isn't one.
pub fn use_inputs_near(path: &std::path::Path) {
    set_project_inputs(inputs_near(path));
}

#[derive(Debug, Clone, Default)]
pub struct SystemDef {
    /// Action name -> parameters.
    pub actions: HashMap<String, Vec<(String, Type)>>,
    /// Action name -> the parameters a call may leave out (`do Play on Audio sound:"x.wav" volume:0.5`).
    pub optional: HashMap<String, Vec<String>>,
    /// Input names this system understands (`Input is pressing <name>`). Empty for systems with no inputs.
    pub inputs: Vec<String>,
    /// Analog inputs (`axis moveForward of Input`), each between -1 and 1.
    pub axes: Vec<String>,
}

#[derive(Debug, Clone, Default)]
pub struct Manifest {
    /// Component name -> fields.
    pub components: HashMap<String, Vec<(String, Type)>>,
    pub systems: HashMap<String, SystemDef>,
}

fn params(list: &[(&str, Type)]) -> Vec<(String, Type)> {
    list.iter().map(|(n, t)| (n.to_string(), t.clone())).collect()
}

impl Manifest {
    pub fn builtin() -> Manifest {
        let mut m = Manifest::default();

        m.components.insert(
            "Transform".into(),
            params(&[("Loc", Type::Vector3), ("Rot", Type::Vector3), ("Scale", Type::Vector3)]),
        );
        // Whether the entity is drawn, and whether it takes part in collisions.
        m.components.insert("Visibility".into(), params(&[("Shown", Type::Boolean)]));
        m.components.insert("Collision".into(), params(&[("Enabled", Type::Boolean)]));

        // The game's own Input Actions when it has set them, else the built-in names (what the examples and tests use).
        let mut input = SystemDef {
            actions: HashMap::new(),
            optional: HashMap::new(),
            inputs: ["moveForward", "moveBack", "moveLeft", "moveRight", "jump", "crouch", "interact"]
                .iter()
                .map(|s| s.to_string())
                .collect(),
            axes: ["moveForward", "moveRight", "lookX", "lookY"].iter().map(|s| s.to_string()).collect(),
        };
        if let Some(own) = PROJECT_INPUTS.read().unwrap_or_else(|e| e.into_inner()).clone() {
            input.inputs = own.buttons;
            input.axes = own.axes;
        }
        m.systems.insert("Input".into(), input);

        // `at:` plays it at a place in the world, `attachedTo:` makes it follow an entity.
        let mut audio = SystemDef::default();
        audio.actions.insert(
            "Play".into(),
            params(&[("sound", Type::Text), ("volume", Type::Decimal), ("at", Type::Vector3), ("attachedTo", Type::Entity), ("loop", Type::Boolean)]),
        );
        audio.optional.insert("Play".into(), ["volume", "at", "attachedTo", "loop"].map(String::from).to_vec());
        audio.actions.insert("Stop".into(), params(&[("sound", Type::Text)]));
        m.systems.insert("Audio".into(), audio);

        let mut ui = SystemDef::default();
        ui.actions.insert("SetText".into(), params(&[("element", Type::Text), ("text", Type::Text)]));
        ui.actions.insert("Show".into(), params(&[("element", Type::Text)]));
        ui.actions.insert("Hide".into(), params(&[("element", Type::Text)]));
        ui.actions.insert("Play".into(), params(&[("animation", Type::Text)]));
        ui.actions.insert("Create".into(), params(&[("widget", Type::Text)]));
        ui.actions.insert("Remove".into(), params(&[("element", Type::Text)]));
        m.systems.insert("UI".into(), ui);

        let mut animation = SystemDef::default();
        animation.actions.insert(
            "Play".into(),
            params(&[("animation", Type::Text), ("target", Type::Entity), ("loop", Type::Boolean), ("rate", Type::Decimal)]),
        );
        animation.optional.insert("Play".into(), ["target", "loop", "rate"].map(String::from).to_vec());
        animation.actions.insert("Stop".into(), params(&[("animation", Type::Text), ("target", Type::Entity)]));
        animation.optional.insert("Stop".into(), vec!["target".to_string()]);
        animation.actions.insert(
            "PlayMontage".into(),
            params(&[("montage", Type::Text), ("section", Type::Text), ("rate", Type::Decimal), ("target", Type::Entity)]),
        );
        animation.optional.insert("PlayMontage".into(), ["section", "rate", "target"].map(String::from).to_vec());
        m.systems.insert("Animation".into(), animation);

        let mut game = SystemDef::default();
        game.actions.insert("OpenLevel".into(), params(&[("level", Type::Text)]));
        game.actions.insert("SetPaused".into(), params(&[("paused", Type::Boolean)]));
        m.systems.insert("Game".into(), game);

        m
    }

    pub fn component_names(&self) -> Vec<&str> {
        let mut v: Vec<&str> = self.components.keys().map(|s| s.as_str()).collect();
        v.sort();
        v
    }

    pub fn system_names(&self) -> Vec<&str> {
        let mut v: Vec<&str> = self.systems.keys().map(|s| s.as_str()).collect();
        v.sort();
        v
    }
}
