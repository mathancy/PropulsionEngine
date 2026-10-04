use super::value::Val;
use crate::ast::PressKind;
use std::collections::{HashMap, HashSet};

/// Everything the interpreter needs from the engine. A real engine implements this to play
/// sounds, show UI, and report input; the interpreter handles the rest.
pub trait Host {
    /// A system action: `do Play on Audio sound:"coin.wav"` calls `call("Audio", "Play", ...)`.
    fn call(&mut self, system: &str, action: &str, args: &[(String, Val)]);

    /// `Input is pressing jump`, `Input starts pressing jump`, ...
    fn pressing(&self, system: &str, input: &str, kind: PressKind) -> bool;

    /// `axis moveForward of Input`: an analog input between -1 and 1. Defaults to 0 (no input).
    fn axis(&self, _system: &str, _input: &str) -> f32 {
        0.0
    }

    /// `print "..."`: a debug message from a script.
    fn print(&mut self, _text: &str) {}

    /// `level name`: the name of the level being played.
    fn level_name(&self) -> String {
        String::new()
    }

    /// Called once at the end of every frame, so edge-triggered input (started/stopped) can reset.
    fn end_frame(&mut self) {}
}

/// A host with no engine behind it: it records every system call and lets you press inputs by hand.
#[derive(Default)]
pub struct StubHost {
    /// One line per call, like `UI.SetText element="coins" text="Coins: 1"`.
    pub calls: Vec<String>,
    /// Every `print` message, in order.
    pub printed: Vec<String>,
    held: HashSet<String>,
    started: HashSet<String>,
    stopped: HashSet<String>,
    axes: HashMap<String, f32>,
    /// What `level name` says.
    pub level: String,
}

impl StubHost {
    pub fn press(&mut self, input: &str) {
        if self.held.insert(input.to_string()) {
            self.started.insert(input.to_string());
        }
    }

    pub fn release(&mut self, input: &str) {
        if self.held.remove(input) {
            self.stopped.insert(input.to_string());
        }
    }

    /// Set an analog input (-1 to 1) by hand.
    pub fn set_axis(&mut self, input: &str, value: f32) {
        self.axes.insert(input.to_string(), value);
    }
}

impl Host for StubHost {
    fn call(&mut self, system: &str, action: &str, args: &[(String, Val)]) {
        let mut line = format!("{system}.{action}");
        for (name, value) in args {
            line.push_str(&format!(" {name}={}", value.literal()));
        }
        self.calls.push(line);
    }

    fn pressing(&self, _system: &str, input: &str, kind: PressKind) -> bool {
        match kind {
            PressKind::Held => self.held.contains(input),
            PressKind::NotHeld => !self.held.contains(input),
            PressKind::Started => self.started.contains(input),
            PressKind::Stopped => self.stopped.contains(input),
        }
    }

    fn axis(&self, _system: &str, input: &str) -> f32 {
        self.axes.get(input).copied().unwrap_or(0.0)
    }

    fn print(&mut self, text: &str) {
        self.printed.push(text.to_string());
    }

    fn level_name(&self) -> String {
        self.level.clone()
    }

    fn end_frame(&mut self) {
        self.started.clear();
        self.stopped.clear();
    }
}
