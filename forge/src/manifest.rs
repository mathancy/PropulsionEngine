//! What the engine offers to scripts: components, systems, and their actions.
//!
//! The spec (decision 3) plans a generated `engine.forgeapi` file. Until the engine side
//! exists, `Manifest::builtin()` describes the parts the examples use.

use crate::ty::Type;
use std::collections::HashMap;

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

        m.systems.insert(
            "Input".into(),
            SystemDef {
                actions: HashMap::new(),
                optional: HashMap::new(),
                inputs: ["moveForward", "moveBack", "moveLeft", "moveRight", "jump", "crouch", "interact"]
                    .iter()
                    .map(|s| s.to_string())
                    .collect(),
                axes: ["moveForward", "moveRight", "lookX", "lookY"].iter().map(|s| s.to_string()).collect(),
            },
        );

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
