//! Nodes and text (SPEC 10).
//!
//! PropulsionMKI's visual scripting and PropulsionScript are two views of the same logic, so every node
//! has exactly one sentence. This module holds the table of nodes, works out which nodes a piece
//! of text implements, and compares that with the `// node 6: Move Object` markers the editor
//! leaves above each sentence. A marker whose node is no longer in the sentence means someone
//! edited that node by hand: the "Modified" badge from the engine design doc (6.10.2).

use crate::ast::*;
use crate::format::{condition_text, item_head, simple_text, stmt_head, term_text, value_text};
use std::collections::HashSet;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum NodeClass {
    /// A handler or action definition: where execution starts.
    Event,
    /// A statement.
    Action,
    /// A statement that holds other statements (branches, loops, delays).
    Flow,
    /// A value computed inside a sentence.
    Data,
}

/// One node: its name, the sentence pattern it produces, and its input pins.
#[derive(Debug)]
pub struct NodeDef {
    pub name: &'static str,
    pub class: NodeClass,
    pub category: &'static str,
    /// The sentence, with the pins in `<angle brackets>`.
    pub pattern: &'static str,
    /// Input pin names. A trailing `?` means the pin may be absent.
    pub pins: &'static [&'static str],
    /// Header lines a sample needs to make sense (like `needs system:Audio`).
    pub prelude: &'static [&'static str],
    /// Example sentences (or, for data nodes, statements that contain the node).
    pub samples: &'static [&'static str],
}

const fn node(
    name: &'static str,
    class: NodeClass,
    category: &'static str,
    pattern: &'static str,
    pins: &'static [&'static str],
    prelude: &'static [&'static str],
    samples: &'static [&'static str],
) -> NodeDef {
    NodeDef { name, class, category, pattern, pins, prelude, samples }
}

use NodeClass::{Action, Data, Event, Flow};

/// Every node. The names follow the engine design doc (6.4) where it has one.
pub static NODES: &[NodeDef] = &[
    // Events
    node("On Begin Play", Event, "Events", "when created:", &[], &[], &["when created"]),
    node("On Destroy", Event, "Events", "when destroyed:", &[], &[], &["when destroyed"]),
    node("On Tick", Event, "Events", "every frame:  or  every <Seconds> seconds:", &["Seconds?"], &[], &["every frame", "every 5 seconds"]),
    node("When Action Pressed", Event, "Events", "when <System> starts pressing <Input>:", &["System", "Input"], &["needs system:Input"], &["when Input starts pressing jump"]),
    node("When Action Released", Event, "Events", "when <System> stops pressing <Input>:", &["System", "Input"], &["needs system:Input"], &["when Input stops pressing jump"]),
    node("On Overlap Begin", Event, "Events", "when Self touches <File> as <Name>:", &["File", "Name"], &[], &["when Self touches Player as Visitor"]),
    node("On Overlap End", Event, "Events", "when Self stops touching <File> as <Name>:", &["File", "Name"], &[], &["when Self stops touching Player as Visitor"]),
    node("On Variable Changed", Event, "Events", "when <Variable> changes:", &["Variable"], &[], &["when Score changes"]),
    node("On Event", Event, "Events", "when <Event>:", &["Event"], &[], &["when Died"]),
    node("On Event From", Event, "Events", "when <File> sends <Event> as <Name>:", &["File", "Event", "Name"], &[], &["when Coin sends Collected as Item"]),
    node("On Damaged", Event, "Events", "when damaged as <Amount> from <Source>:", &["Amount", "Source?"], &[], &["when damaged as Amount", "when damaged as Amount from Attacker"]),
    node("On Hit", Event, "Events", "when Self hits <File> as <Name>:", &["File", "Name"], &[], &["when Self hits Wall as Obstacle"]),
    node("Define Action", Event, "Functions", "action <Name> <Parameter>... gives <Gives>:", &["Name", "Parameter?", "Gives?"], &[], &["action Heal amount:integer", "action Total gives integer"]),
    // Actions
    node("Move Object", Action, "Actions", "move <Target> <Direction> by <Amount>", &["Target", "Direction", "Amount"], &[], &["move Self forward by 5.0"]),
    node("Move Toward", Action, "Actions", "move <Target> toward <Point> by <Amount>", &["Target", "Point", "Amount"], &[], &["move Transform.Loc toward Goal by 1.0"]),
    node("Set Variable", Action, "Actions", "make <Target> <Value>", &["Target", "Value", "Max?", "Min?"], &[], &["make Health 100", "make Health 100, capped at 100"]),
    node("Set Location", Action, "Actions", "make Transform.Loc <Value>", &["Target", "Value", "Max?", "Min?"], &[], &["make Transform.Loc Spawn", "make Transform.Loc.Y 2.0"]),
    node("Set Rotation", Action, "Actions", "make Transform.Rot <Value>", &["Target", "Value", "Max?", "Min?"], &[], &["make Transform.Rot Spin"]),
    node("Set Scale", Action, "Actions", "make Transform.Scale <Value>", &["Target", "Value", "Max?", "Min?"], &[], &["make Transform.Scale Big"]),
    node("Rotate Object", Action, "Actions", "add <Amount> to Transform.Rot.<Axis>", &["Target", "Amount", "Operation", "Max?", "Min?"], &[], &["add 90 per second to Transform.Rot.Y"]),
    node("Offset Location", Action, "Actions", "add <Offset> to Transform.Loc", &["Target", "Amount", "Operation", "Max?", "Min?"], &[], &["add Velocity per second to Transform.Loc"]),
    node("Change Variable", Action, "Actions", "add / subtract / multiply / divide <Target> <Amount>", &["Target", "Amount", "Operation", "Max?", "Min?"], &[], &["add 1 to Score", "subtract 1 from Score, floored at 0", "multiply Score by 2", "divide Score by 2", "add Fresh to Alive"]),
    node("Remove From List", Action, "Actions", "remove <Item> from <List>", &["Item", "List"], &[], &["remove Fallen from Alive"]),
    node("Add Unique", Action, "Lists", "add <Item> to <List>, if new", &["Item", "List"], &[], &["add Fresh to Alive, if new"]),
    node("Remove At", Action, "Lists", "remove item <Index> from <List>", &["Index", "List"], &[], &["remove item 2 from Alive"]),
    node("Clear List", Action, "Lists", "clear <List>", &["List"], &[], &["clear Alive"]),
    node("Set Local", Action, "Actions", "remember <Name> <Value>", &["Name", "Value"], &[], &["remember Total Score times 2"]),
    node("Call Action", Action, "Actions", "do <Action> [on <Target>] <Argument>...", &["Action", "Target", "Argument?"], &[], &["do Heal on Player amount:5", "do Respawn"]),
    node("Play Sound", Action, "Actions", "do Play on Audio sound:<Sound>", &["Action", "Target", "Argument?"], &["needs system:Audio"], &["do Play on Audio sound:\"jump.wav\""]),
    node("Play Sound At", Action, "Audio", "do Play on Audio sound:<Sound> at:<Location>", &["Action", "Target", "Argument?"], &["needs system:Audio"], &["do Play on Audio sound:\"boom.wav\" at:Transform.Loc"]),
    node("Play Sound Attached", Action, "Audio", "do Play on Audio sound:<Sound> attachedTo:<Entity>", &["Action", "Target", "Argument?"], &["needs system:Audio"], &["do Play on Audio sound:\"engine.wav\" attachedTo:Self"]),
    node("Play Animation", Action, "Animation", "do Play on Animation animation:<Animation>", &["Action", "Target", "Argument?"], &["needs system:Animation"], &["do Play on Animation animation:\"walk\""]),
    node("Play Montage", Action, "Animation", "do PlayMontage on Animation montage:<Montage>", &["Action", "Target", "Argument?"], &["needs system:Animation"], &["do PlayMontage on Animation montage:\"attack\" section:\"swing\""]),
    node("Create Widget", Action, "UI", "do Create on UI widget:<Widget>", &["Action", "Target", "Argument?"], &["needs system:UI"], &["do Create on UI widget:\"hud\""]),
    node("Add To Viewport", Action, "UI", "do Show on UI element:<Element>", &["Action", "Target", "Argument?"], &["needs system:UI"], &["do Show on UI element:\"hud\""]),
    node("Remove From Parent", Action, "UI", "do Remove on UI element:<Element>", &["Action", "Target", "Argument?"], &["needs system:UI"], &["do Remove on UI element:\"hud\""]),
    node("Set Text", Action, "UI", "do SetText on UI element:<Element> text:<Text>", &["Action", "Target", "Argument?"], &["needs system:UI"], &["do SetText on UI element:\"coins\" text:\"0\""]),
    node("Open Level", Action, "Game", "do OpenLevel on Game level:<Level>", &["Action", "Target", "Argument?"], &["needs system:Game"], &["do OpenLevel on Game level:\"forest\""]),
    node("Set Game Paused", Action, "Game", "do SetPaused on Game paused:<Paused>", &["Action", "Target", "Argument?"], &["needs system:Game"], &["do SetPaused on Game paused:true"]),
    node("System Action", Action, "Actions", "do <Action> on <System> <Argument>...", &["Action", "Target", "Argument?"], &["needs system:UI"], &["do Hide on UI element:\"win\""]),
    node("Send Message", Action, "Actions", "tell <Target> to <Action> <Argument>...", &["Target", "Action", "Argument?"], &[], &["tell Foe to TakeDamage amount:5", "tell Foe to Wake"]),
    node("Damage", Action, "Actions", "damage <Target> by <Amount>", &["Target", "Amount"], &[], &["damage Foe by 5.0"]),
    node("Attach", Action, "Actions", "attach <Child> to <Parent>", &["Child", "Parent"], &[], &["attach Hat to Self"]),
    node("Detach", Action, "Actions", "detach <Child>", &["Child"], &[], &["detach Hat"]),
    node("Open Gate", Action, "Flow", "open <Gate>", &["Gate"], &[], &["open Door"]),
    node("Close Gate", Action, "Flow", "close <Gate>", &["Gate"], &[], &["close Door"]),
    node("Toggle Gate", Action, "Flow", "toggle <Gate>", &["Gate"], &[], &["toggle Door"]),
    node("Send Event", Action, "Actions", "send <Event>", &["Event"], &[], &["send Died"]),
    node("Reset Timer", Action, "Actions", "reset <Timer>", &["Timer"], &[], &["reset SinceHit", "reset Greeting\nonly once as Greeting:\n\tfinish"]),
    node("Spawn Object", Action, "Actions", "spawn <File> at:<Location> as <Name>", &["File", "Location", "Name"], &[], &["spawn Coin at:Spot as Reward"]),
    node("Destroy Object", Action, "Actions", "destroy <Target>", &["Target"], &[], &["destroy Self"]),
    node("Return Value", Action, "Actions", "give back <Value>", &["Value"], &[], &["give back 5"]),
    node("Finish", Action, "Actions", "finish", &[], &[], &["finish"]),
    node("Print Debug Message", Action, "Actions", "print <Message>", &["Message"], &[], &["print \"hello\""]),
    node("Break", Action, "Actions", "stop loop", &[], &[], &["stop loop"]),
    node("Continue", Action, "Actions", "skip", &[], &[], &["skip"]),
    // Flow
    node("Branch", Flow, "Flow", "if <Condition>:  otherwise if <Else If>:  otherwise:", &["Condition", "Else If?", "Else?"], &[], &["if Health is at most 0:", "if Health is at most 0, then finish"]),
    node("Loop", Flow, "Flow", "for <Variable> from <From> to <To>:", &["Variable", "From", "To"], &[], &["for Index from 0 to 4:"]),
    node("Repeat", Flow, "Flow", "repeat <Count> times:", &["Count"], &[], &["repeat 3 times:"]),
    node("For Each", Flow, "Flow", "for each <Item> in <List>:", &["Item", "List"], &[], &["for each Foe in Foes:"]),
    node("While", Flow, "Flow", "while <Condition>:", &["Condition"], &[], &["while Health is above 0:"]),
    node("Delay", Flow, "Flow", "after <Seconds> seconds:", &["Seconds"], &[], &["after 2 seconds:"]),
    node("Do Once", Flow, "Flow", "only once [as <Name>]:", &["Name?"], &[], &["only once:", "only once as Greeting:"]),
    node("Do N", Flow, "Flow", "first <Count> times [as <Name>]:", &["Count", "Name?"], &[], &["first 3 times:", "first 3 times as Warmup:"]),
    node("Flip Flop", Flow, "Flow", "alternate [as <Name>]:  otherwise:", &["Name?"], &[], &["alternate:\n\tfinish\notherwise:\n\tfinish", "alternate as Side:\n\tfinish\notherwise:\n\tfinish"]),
    node("Gate", Flow, "Flow", "through <Gate>:", &["Gate"], &[], &["through Door:"]),
    node("Switch", Flow, "Flow", "choose <Value>:  case <Case>:  otherwise:", &["Value", "Case 1?", "Case 2?", "Case 3?", "Case 4?", "Case 5?", "Case 6?", "Case 7?", "Case 8?"], &[], &["choose Lives:\n\tcase 1:\n\t\tfinish\n\tcase 2 or 3:\n\t\tfinish\n\totherwise:\n\t\tfinish"]),
    node("Timeline", Flow, "Flow", "animate <Name> from <From> to <To> over <Seconds> seconds:", &["Name", "From", "To", "Seconds"], &[], &["animate Lift from 0.0 to 5.0 over 2 seconds:"]),
    // Data
    node("Lerp", Data, "Math", "<Alpha> of the way from <From> to <To>", &["Alpha", "From", "To"], &[], &["make X 0.5 of the way from 1.0 to 2.0"]),
    node("Get Axis", Data, "Input", "axis <Input> of <System>", &["System", "Input"], &["needs system:Input"], &["make X axis moveForward of Input"]),
    node("Distance", Data, "Math", "distance from <From> to <To>", &["From", "To"], &[], &["make X distance from Self to Player"]),
    node("Count Of", Data, "Lists", "count of <List>", &["List"], &[], &["make X count of Alive"]),
    node("Item Of", Data, "Lists", "item <Index> of <List>", &["Index", "List"], &[], &["make X item 0 of Points"]),
    node("Join Text", Data, "Text", "<Left> joined with <Right>", &["Left", "Right"], &[], &["make X \"Score: \" joined with Score"]),
    node("To Text", Data, "Text", "text of <Value>", &["Value"], &[], &["make X text of Score"]),
    node("To Decimal", Data, "Math", "decimal of <Value>", &["Value"], &[], &["make X decimal of Score"]),
    node("To Integer", Data, "Math", "integer of <Value>", &["Value"], &[], &["make X integer of Speed"]),
    node("Absolute Value", Data, "Math", "absolute value of <Value>", &["Value"], &[], &["make X absolute value of Speed"]),
    node("Call Action Result", Data, "Functions", "result of <Action> <Argument>...", &["Action", "Argument?"], &[], &["make X result of IsAlive"]),
    node("Make Vector", Data, "Math", "vector x:<X> y:<Y> z:<Z>", &["X", "Y", "Z"], &[], &["make X vector x:1 y:2 z:3"]),
    node("Time Since Start", Data, "Math", "time since start", &[], &[], &["make X time since start"]),
    node("Add", Data, "Math", "<Left> plus <Right>", &["Left", "Right"], &[], &["make X A plus B"]),
    node("Subtract", Data, "Math", "<Left> minus <Right>", &["Left", "Right"], &[], &["make X A minus B"]),
    node("Multiply", Data, "Math", "<Left> times <Right>", &["Left", "Right"], &[], &["make X A times B"]),
    node("Divide", Data, "Math", "<Left> divided by <Right>", &["Left", "Right"], &[], &["make X A divided by B"]),
    node("Remainder", Data, "Math", "<Left> remainder after dividing by <Right>", &["Left", "Right"], &[], &["make X A remainder after dividing by B"]),
    node("Per Second", Data, "Math", "<Value> per second", &["Value"], &[], &["make X Speed per second"]),
    node("Compare", Data, "Logic", "<Left> is above / below / at least / at most / not <Right>", &["Left", "Operator", "Right"], &[], &["remember Ok Health is above 0"]),
    node("Input State", Data, "Input", "<System> is pressing / starts pressing / stops pressing <Input>", &["System", "Input", "State"], &["needs system:Input"], &["remember Ok Input is pressing jump"]),
    node("Is Valid", Data, "Logic", "<Value> is missing  or  is present", &["Value", "Check"], &[], &["remember Ok Player is missing"]),
    node("Contains", Data, "Lists", "<List> contains / does not contain <Item>", &["List", "Match", "Item"], &[], &["remember Ok Alive contains Fresh"]),
    node("Select", Data, "Logic", "<Yes> if <Condition> otherwise <No>", &["Yes", "Condition", "No"], &[], &["make X 1 if Ready is true otherwise 2"]),
    node("Cast To", Data, "Objects", "cast <Value> to <File>", &["Value", "File"], &[], &["remember Gob cast Foe to Goblin"]),
    node("Format Text", Data, "Text", "format \"text {Name}\"", &["Text"], &[], &["make X format \"Score: {Score} of {Best}\""]),
    node("Make Struct", Data, "Structs", "new <Struct> <Field>:<Value>...", &["Struct", "Argument?"], &[], &["make X new Item Name:\"Sword\" Cost:5", "make X new Item"]),
    node("Data Table Row", Data, "Structs", "row <Key> of <Table>", &["Key", "Table"], &[], &["make X row \"Sword\" of Items"]),
    node("Line Trace", Data, "Physics", "trace from <From> to <To>", &["From", "To"], &[], &["make X trace from Self to Self"]),
    node("Sphere Trace", Data, "Physics", "sweep from <From> to <To> radius <Radius>", &["From", "To", "Radius"], &[], &["make X sweep from Self to Self radius 0.5"]),
    node("Clamp", Data, "Math", "clamp <Value> between <Min> and <Max>", &["Value", "Min", "Max"], &[], &["make X clamp Health between 0 and 100"]),
    node("Map Range", Data, "Math", "map <Value> from <FromMin> to <FromMax> into <ToMin> to <ToMax>", &["Value", "FromMin", "FromMax", "ToMin", "ToMax"], &[], &["make X map Speed from 0.0 to 10.0 into 0.0 to 1.0"]),
    node("Random Decimal", Data, "Math", "random decimal from <Min> to <Max>", &["Min", "Max"], &[], &["make X random decimal from 0.0 to 1.0"]),
    node("Random Integer", Data, "Math", "random integer from <Min> to <Max>", &["Min", "Max"], &[], &["make X random integer from 1 to 6"]),
    node("Normalize", Data, "Vectors", "normalized <Vector>", &["Vector"], &[], &["make X normalized Offset"]),
    node("Vector Length", Data, "Vectors", "length of <Vector>", &["Vector"], &[], &["make X length of Offset"]),
    node("Dot Product", Data, "Vectors", "dot product of <A> and <B>", &["A", "B"], &[], &["make X dot product of Facing and Offset"]),
    node("Make Rotator", Data, "Vectors", "rotation pitch:<Pitch> yaw:<Yaw> roll:<Roll>", &["Pitch", "Yaw", "Roll"], &[], &["make X rotation pitch:0 yaw:90 roll:0"]),
    node("Look At Rotation", Data, "Vectors", "rotation looking from <From> to <To>", &["From", "To"], &[], &["make X rotation looking from Self to Player"]),
    node("Forward Vector", Data, "Vectors", "forward of <Entity>", &["Entity"], &[], &["make X forward of Self"]),
    node("Right Vector", Data, "Vectors", "right of <Entity>", &["Entity"], &[], &["make X right of Self"]),
    node("Up Vector", Data, "Vectors", "up of <Entity>", &["Entity"], &[], &["make X up of Self"]),
    node("Not", Data, "Logic", "not <Value>", &["Value"], &[], &["make X not Ready"]),
    node("Find In List", Data, "Lists", "position of <Item> in <List>", &["Item", "List"], &[], &["make X position of Fresh in Alive"]),
    node("Ease", Data, "Math", "ease <Current> toward <Target> at <Speed>", &["Current", "Target", "Speed"], &[], &["make X ease X toward Goal at 5.0"]),
    node("Turn", Data, "Vectors", "turn <Current> toward <Target> at <Speed>", &["Current", "Target", "Speed"], &[], &["make X turn X toward Goal at 90.0"]),
    node("Level Name", Data, "Game", "level name", &[], &[], &["make X level name"]),
    node("And", Data, "Logic", "<Condition> and <Condition>", &[], &[], &["remember Ok A is true and B is true"]),
    node("Or", Data, "Logic", "<Condition> or <Condition>", &[], &[], &["remember Ok A is true or B is true"]),
];

/// Look a node up by its name.
pub fn def(name: &str) -> Option<&'static NodeDef> {
    NODES.iter().find(|n| n.name == name)
}

/// What a file's header says that changes how a sentence is read: which names are engine systems.
pub struct Ctx {
    systems: HashSet<String>,
}

impl Ctx {
    pub fn of(file: &File) -> Ctx {
        let systems = file
            .header
            .iter()
            .filter_map(|d| match &d.kind {
                DeclKind::NeedsSystem(n) => Some(n.clone()),
                _ => None,
            })
            .collect();
        Ctx { systems }
    }

    /// The same, with these systems counted as declared too (for nodes that are not in the script yet, whose
    /// header lines are added when they are).
    pub fn with_systems(mut self, systems: &[&str]) -> Ctx {
        self.systems.extend(systems.iter().map(|s| s.to_string()));
        self
    }
}

/// The names `only once as X`, `first N times as X` and `alternate as X` give, in file order.
pub fn latch_names(file: &File) -> Vec<String> {
    fn walk(body: &[Stmt], out: &mut Vec<String>) {
        for s in body {
            match &s.kind {
                StmtKind::OnlyOnce { name, body } | StmtKind::FirstTimes { name, body, .. } => {
                    out.extend(name.clone());
                    walk(body, out);
                }
                StmtKind::Alternate { name, first, second } => {
                    out.extend(name.clone());
                    walk(first, out);
                    walk(second, out);
                }
                StmtKind::If { branches, otherwise, .. } => {
                    for (_, b) in branches {
                        walk(b, out);
                    }
                    if let Some(b) = otherwise {
                        walk(b, out);
                    }
                }
                StmtKind::Choose { cases, otherwise, .. } => {
                    for c in cases {
                        walk(&c.body, out);
                    }
                    if let Some(b) = otherwise {
                        walk(b, out);
                    }
                }
                StmtKind::Repeat { body, .. }
                | StmtKind::ForEach { body, .. }
                | StmtKind::ForRange { body, .. }
                | StmtKind::While { body, .. }
                | StmtKind::After { body, .. }
                | StmtKind::Through { body, .. }
                | StmtKind::Animate { body, .. } => walk(body, out),
                _ => {}
            }
        }
    }
    let mut names = Vec::new();
    for item in &file.implement {
        match &item.kind {
            ItemKind::Handler { body, .. } | ItemKind::Action { body, .. } => walk(body, &mut names),
        }
    }
    names
}

/// The node a call on an engine system is: most of the system actions a script is likely to want have a node
/// of their own, so they can be found in the menu by what they do.
fn system_node(system: &str, action: &str, args: &[Arg]) -> &'static str {
    let has = |name: &str| args.iter().any(|a| a.name == name);
    match (system, action) {
        ("Audio", "Play") if has("attachedTo") => "Play Sound Attached",
        ("Audio", "Play") if has("at") => "Play Sound At",
        ("Audio", "Play") => "Play Sound",
        ("UI", "Create") => "Create Widget",
        ("UI", "Show") => "Add To Viewport",
        ("UI", "Remove") => "Remove From Parent",
        ("UI", "SetText") => "Set Text",
        ("Animation", "Play") => "Play Animation",
        ("Animation", "PlayMontage") => "Play Montage",
        ("Game", "OpenLevel") => "Open Level",
        ("Game", "SetPaused") => "Set Game Paused",
        _ => "System Action",
    }
}

/// A node as it appears in a piece of text: its name and its input pins, as PropulsionScript text.
#[derive(Debug, Clone, PartialEq)]
pub struct Inst {
    pub name: &'static str,
    pub pins: Vec<(String, String)>,
}

fn pin(name: &str, text: impl Into<String>) -> (String, String) {
    (name.to_string(), text.into())
}

fn limit_pins(l: &Limit, out: &mut Vec<(String, String)>) {
    if let Some(t) = &l.capped {
        out.push(pin("Max", term_text(t)));
    }
    if let Some(t) = &l.floored {
        out.push(pin("Min", term_text(t)));
    }
}

fn transform_field(t: &Term) -> Option<&str> {
    match t {
        Term::Path { segments, .. } if segments.first().is_some_and(|s| s == "Transform") => segments.get(1).map(|s| s.as_str()),
        _ => None,
    }
}

/// The node a handler or action definition is.
pub fn item_inst(item: &Item) -> Inst {
    match &item.kind {
        ItemKind::Handler { trigger, .. } => match trigger {
            Trigger::Created => Inst { name: "On Begin Play", pins: vec![] },
            Trigger::Destroyed => Inst { name: "On Destroy", pins: vec![] },
            Trigger::EveryFrame => Inst { name: "On Tick", pins: vec![] },
            Trigger::EverySeconds(n) => Inst { name: "On Tick", pins: vec![pin("Seconds", format!("{n}"))] },
            Trigger::Touches { file, name } => Inst { name: "On Overlap Begin", pins: vec![pin("File", file), pin("Name", name)] },
            Trigger::StopsTouching { file, name } => Inst { name: "On Overlap End", pins: vec![pin("File", file), pin("Name", name)] },
            Trigger::Changes(v) => Inst { name: "On Variable Changed", pins: vec![pin("Variable", v)] },
            Trigger::Input { system, input, kind } => Inst {
                name: if *kind == PressKind::Started { "When Action Pressed" } else { "When Action Released" },
                pins: vec![pin("System", system), pin("Input", input)],
            },
            Trigger::Sends { file, event, name } => {
                Inst { name: "On Event From", pins: vec![pin("File", file), pin("Event", event), pin("Name", name)] }
            }
            Trigger::Event(e) => Inst { name: "On Event", pins: vec![pin("Event", e)] },
            Trigger::Damaged { amount, source } => {
                let mut pins = vec![pin("Amount", amount)];
                if let Some(s) = source {
                    pins.push(pin("Source", s));
                }
                Inst { name: "On Damaged", pins }
            }
            Trigger::Hits { file, name } => Inst { name: "On Hit", pins: vec![pin("File", file), pin("Name", name)] },
        },
        ItemKind::Action { name, params, gives, .. } => {
            let mut pins = vec![pin("Name", name)];
            for p in params {
                pins.push(pin("Parameter", format!("{}:{}", p.name, crate::format::type_text_pub(&p.ty))));
            }
            if let Some(g) = gives {
                pins.push(pin("Gives", crate::format::type_text_pub(g)));
            }
            Inst { name: "Define Action", pins }
        }
    }
}

fn args_pins(args: &[Arg], out: &mut Vec<(String, String)>) {
    for a in args {
        out.push(pin(&a.name, value_text(&a.value)));
    }
}

/// The statement-level node a statement is (not counting nodes inside its values).
pub(crate) fn stmt_inst(kind: &StmtKind, ctx: &Ctx) -> Inst {
    match kind {
        StmtKind::Make { target, value, limit } => {
            let name = match transform_field(target) {
                Some("Loc") => "Set Location",
                Some("Rot") => "Set Rotation",
                Some("Scale") => "Set Scale",
                _ => "Set Variable",
            };
            let mut pins = vec![pin("Target", term_text(target)), pin("Value", value_text(value))];
            limit_pins(limit, &mut pins);
            Inst { name, pins }
        }
        StmtKind::Add { value, target, limit } | StmtKind::Subtract { value, target, limit } => {
            let op = if matches!(kind, StmtKind::Add { .. }) { "add" } else { "subtract" };
            let name = match transform_field(target) {
                Some("Rot") => "Rotate Object",
                Some("Loc") => "Offset Location",
                _ => "Change Variable",
            };
            let mut pins = vec![pin("Target", term_text(target)), pin("Amount", value_text(value)), pin("Operation", op)];
            limit_pins(limit, &mut pins);
            Inst { name, pins }
        }
        StmtKind::Multiply { target, value } | StmtKind::Divide { target, value } => {
            let op = if matches!(kind, StmtKind::Multiply { .. }) { "multiply" } else { "divide" };
            Inst { name: "Change Variable", pins: vec![pin("Target", term_text(target)), pin("Amount", value_text(value)), pin("Operation", op)] }
        }
        StmtKind::Remove { value, list } => Inst { name: "Remove From List", pins: vec![pin("Item", value_text(value)), pin("List", term_text(list))] },
        StmtKind::RemoveAt { index, list } => Inst { name: "Remove At", pins: vec![pin("Index", term_text(index)), pin("List", term_text(list))] },
        StmtKind::AddUnique { value, list } => Inst { name: "Add Unique", pins: vec![pin("Item", value_text(value)), pin("List", term_text(list))] },
        StmtKind::Clear(list) => Inst { name: "Clear List", pins: vec![pin("List", term_text(list))] },
        StmtKind::GateChange { gate, change } => {
            let name = match change {
                GateChange::Open => "Open Gate",
                GateChange::Close => "Close Gate",
                GateChange::Toggle => "Toggle Gate",
            };
            Inst { name, pins: vec![pin("Gate", gate)] }
        }
        StmtKind::Damage { target, amount } => Inst { name: "Damage", pins: vec![pin("Target", term_text(target)), pin("Amount", value_text(amount))] },
        StmtKind::Attach { child, parent } => Inst { name: "Attach", pins: vec![pin("Child", term_text(child)), pin("Parent", term_text(parent))] },
        StmtKind::Detach(child) => Inst { name: "Detach", pins: vec![pin("Child", term_text(child))] },
        StmtKind::Tell { target, action, args } => {
            let mut pins = vec![pin("Target", term_text(target)), pin("Action", action)];
            args_pins(args, &mut pins);
            Inst { name: "Send Message", pins }
        }
        StmtKind::OnlyOnce { name, .. } => Inst { name: "Do Once", pins: name.iter().map(|n| pin("Name", n)).collect() },
        StmtKind::FirstTimes { count, name, .. } => {
            let mut pins = vec![pin("Count", term_text(count))];
            pins.extend(name.iter().map(|n| pin("Name", n)));
            Inst { name: "Do N", pins }
        }
        StmtKind::Alternate { name, .. } => Inst { name: "Flip Flop", pins: name.iter().map(|n| pin("Name", n)).collect() },
        StmtKind::Through { gate, .. } => Inst { name: "Gate", pins: vec![pin("Gate", gate)] },
        StmtKind::Choose { value, cases, .. } => {
            let mut pins = vec![pin("Value", term_text(value))];
            for (i, c) in cases.iter().enumerate() {
                pins.push(pin(&format!("Case {}", i + 1), crate::format::case_values_text(c)));
            }
            Inst { name: "Switch", pins }
        }
        StmtKind::Animate { var, from, to, seconds, .. } => {
            let text = match seconds {
                Term::Duration(d) => format!("{d}"),
                other => term_text(other),
            };
            Inst { name: "Timeline", pins: vec![pin("Name", var), pin("From", term_text(from)), pin("To", term_text(to)), pin("Seconds", text)] }
        }
        StmtKind::Do { action, on, args } => {
            let name = match on.as_deref() {
                Some(n) if ctx.systems.contains(n) => system_node(n, action, args),
                _ => "Call Action",
            };
            let mut pins = vec![pin("Action", action), pin("Target", on.clone().unwrap_or_else(|| "Self".to_string()))];
            args_pins(args, &mut pins);
            Inst { name, pins }
        }
        StmtKind::Send(e) => Inst { name: "Send Event", pins: vec![pin("Event", e)] },
        StmtKind::Remember { name, value } => Inst { name: "Set Local", pins: vec![pin("Name", name), pin("Value", value_text(value))] },
        StmtKind::Reset(t) => Inst { name: "Reset Timer", pins: vec![pin("Timer", t)] },
        StmtKind::Spawn { file, at, name } => {
            Inst { name: "Spawn Object", pins: vec![pin("File", file), pin("Location", value_text(at)), pin("Name", name)] }
        }
        StmtKind::Move { target, toward, by } => {
            Inst { name: "Move Toward", pins: vec![pin("Target", term_text(target)), pin("Point", value_text(toward)), pin("Amount", value_text(by))] }
        }
        StmtKind::MoveDir { target, dir, by } => {
            let d = match dir {
                Direction::Forward => "forward",
                Direction::Back => "back",
                Direction::Left => "left",
                Direction::Right => "right",
                Direction::Up => "up",
                Direction::Down => "down",
            };
            Inst { name: "Move Object", pins: vec![pin("Target", term_text(target)), pin("Direction", d), pin("Amount", value_text(by))] }
        }
        StmtKind::Print(v) => Inst { name: "Print Debug Message", pins: vec![pin("Message", value_text(v))] },
        StmtKind::Destroy(t) => Inst { name: "Destroy Object", pins: vec![pin("Target", term_text(t))] },
        StmtKind::GiveBack(v) => Inst { name: "Return Value", pins: vec![pin("Value", value_text(v))] },
        StmtKind::Finish => Inst { name: "Finish", pins: vec![] },
        StmtKind::StopLoop => Inst { name: "Break", pins: vec![] },
        StmtKind::Skip => Inst { name: "Continue", pins: vec![] },
        StmtKind::If { branches, otherwise, .. } => {
            let mut pins = vec![pin("Condition", condition_text(&branches[0].0))];
            for (c, _) in &branches[1..] {
                pins.push(pin("Else If", condition_text(c)));
            }
            // (Whether there is an `otherwise` is shown by the False output, not by a pin.)
            let _ = otherwise;
            Inst { name: "Branch", pins }
        }
        StmtKind::Repeat { times, .. } => Inst { name: "Repeat", pins: vec![pin("Count", term_text(times))] },
        StmtKind::ForEach { var, list, .. } => Inst { name: "For Each", pins: vec![pin("Item", var), pin("List", term_text(list))] },
        StmtKind::ForRange { var, from, to, .. } => {
            Inst { name: "Loop", pins: vec![pin("Variable", var), pin("From", term_text(from)), pin("To", term_text(to))] }
        }
        StmtKind::While { cond, .. } => Inst { name: "While", pins: vec![pin("Condition", condition_text(cond))] },
        StmtKind::After { seconds, .. } => {
            let text = match seconds {
                Term::Duration(d) => format!("{d}"),
                other => term_text(other),
            };
            Inst { name: "Delay", pins: vec![pin("Seconds", text)] }
        }
    }
}

pub(crate) fn value_insts(v: &Value, out: &mut Vec<Inst>) {
    let t = term_text;
    match v {
        Value::Term(_) | Value::TimeSinceStart => {
            if matches!(v, Value::TimeSinceStart) {
                out.push(Inst { name: "Time Since Start", pins: vec![] });
            }
        }
        Value::PerSecond(a) => out.push(Inst { name: "Per Second", pins: vec![pin("Value", t(a))] }),
        Value::Distance { from, to } => out.push(Inst { name: "Distance", pins: vec![pin("From", t(from)), pin("To", t(to))] }),
        Value::CountOf(a) => out.push(Inst { name: "Count Of", pins: vec![pin("List", t(a))] }),
        Value::ItemOf { index, list } => out.push(Inst { name: "Item Of", pins: vec![pin("Index", t(index)), pin("List", t(list))] }),
        Value::JoinedWith { left, right } => out.push(Inst { name: "Join Text", pins: vec![pin("Left", t(left)), pin("Right", t(right))] }),
        Value::TextOf(a) => out.push(Inst { name: "To Text", pins: vec![pin("Value", t(a))] }),
        Value::DecimalOf(a) => out.push(Inst { name: "To Decimal", pins: vec![pin("Value", t(a))] }),
        Value::IntegerOf(a) => out.push(Inst { name: "To Integer", pins: vec![pin("Value", t(a))] }),
        Value::AbsoluteValueOf(a) => out.push(Inst { name: "Absolute Value", pins: vec![pin("Value", t(a))] }),
        Value::ResultOf { action, args } => {
            let mut pins = vec![pin("Action", action)];
            args_pins(args, &mut pins);
            out.push(Inst { name: "Call Action Result", pins });
            for a in args {
                value_insts(&a.value, out);
            }
        }
        Value::Vector { x, y, z } => out.push(Inst { name: "Make Vector", pins: vec![pin("X", t(x)), pin("Y", t(y)), pin("Z", t(z))] }),
        Value::Lerp { t: alpha, from, to } => {
            out.push(Inst { name: "Lerp", pins: vec![pin("Alpha", t(alpha)), pin("From", t(from)), pin("To", t(to))] })
        }
        Value::Axis { system, input } => out.push(Inst { name: "Get Axis", pins: vec![pin("System", system), pin("Input", input)] }),
        Value::Arith { left, op, right } => {
            let name = match op {
                ArithOp::Plus => "Add",
                ArithOp::Minus => "Subtract",
                ArithOp::Times => "Multiply",
                ArithOp::DividedBy => "Divide",
                ArithOp::Remainder => "Remainder",
            };
            out.push(Inst { name, pins: vec![pin("Left", t(left)), pin("Right", t(right))] });
        }
        Value::Condition(c) => condition_insts(c, out),
        Value::Phrase { node, args } => {
            if let Some(def) = crate::phrases::find(node) {
                let pins = def.pins().iter().zip(args).map(|(n, a)| pin(n, t(a))).collect();
                out.push(Inst { name: def.node, pins });
            }
        }
        Value::Pick { yes, cond, no } => {
            out.push(Inst { name: "Select", pins: vec![pin("Yes", t(yes)), pin("Condition", condition_text(cond)), pin("No", t(no))] });
            condition_insts(cond, out);
        }
        Value::CastTo { value, file } => out.push(Inst { name: "Cast To", pins: vec![pin("Value", t(value)), pin("File", file)] }),
        Value::NewStruct { ty, args } => {
            let mut pins = vec![pin("Struct", ty)];
            args_pins(args, &mut pins);
            out.push(Inst { name: "Make Struct", pins });
            for a in args {
                value_insts(&a.value, out);
            }
        }
        Value::RowOf { key, table } => out.push(Inst { name: "Data Table Row", pins: vec![pin("Key", t(key)), pin("Table", table)] }),
        Value::Format { .. } => {
            let text = value_text(v);
            let template = text.strip_prefix("format ").unwrap_or(&text).to_string();
            out.push(Inst { name: "Format Text", pins: vec![pin("Text", template)] });
        }
    }
}

pub(crate) fn condition_insts(c: &Condition, out: &mut Vec<Inst>) {
    for cmp in std::iter::once(&c.first).chain(c.rest.iter()) {
        match cmp {
            Comparison::Compare { left, op, right } => {
                let word = match op {
                    CmpOp::Eq => "is",
                    CmpOp::NotEq => "is not",
                    CmpOp::Above => "is above",
                    CmpOp::Below => "is below",
                    CmpOp::AtLeast => "is at least",
                    CmpOp::AtMost => "is at most",
                };
                out.push(Inst { name: "Compare", pins: vec![pin("Left", value_text(left)), pin("Operator", word), pin("Right", value_text(right))] });
                value_insts(left, out);
                value_insts(right, out);
            }
            Comparison::Pressing { system, input, kind } => {
                let state = match kind {
                    PressKind::Held => "is pressing",
                    PressKind::NotHeld => "is not pressing",
                    PressKind::Started => "starts pressing",
                    PressKind::Stopped => "stops pressing",
                };
                out.push(Inst { name: "Input State", pins: vec![pin("System", system), pin("Input", input), pin("State", state)] });
            }
            Comparison::Presence { value, missing } => {
                out.push(Inst { name: "Is Valid", pins: vec![pin("Value", value_text(value)), pin("Check", if *missing { "missing" } else { "present" })] });
                value_insts(value, out);
            }
            Comparison::Contains { list, item, negated } => {
                out.push(Inst {
                    name: "Contains",
                    pins: vec![
                        pin("List", value_text(list)),
                        pin("Match", if *negated { "does not contain" } else { "contains" }),
                        pin("Item", value_text(item)),
                    ],
                });
                value_insts(list, out);
                value_insts(item, out);
            }
        }
    }
    if !c.rest.is_empty() {
        out.push(Inst { name: if c.joiner == Some(Joiner::Or) { "Or" } else { "And" }, pins: vec![] });
    }
}

fn stmt_data(kind: &StmtKind, out: &mut Vec<Inst>) {
    match kind {
        StmtKind::Make { value, .. }
        | StmtKind::AddUnique { value, .. }
        | StmtKind::Damage { amount: value, .. }
        | StmtKind::Add { value, .. }
        | StmtKind::Subtract { value, .. }
        | StmtKind::Multiply { value, .. }
        | StmtKind::Divide { value, .. }
        | StmtKind::Remove { value, .. }
        | StmtKind::Remember { value, .. }
        | StmtKind::Print(value)
        | StmtKind::GiveBack(value) => value_insts(value, out),
        StmtKind::Do { args, .. } | StmtKind::Tell { args, .. } => {
            for a in args {
                value_insts(&a.value, out);
            }
        }
        StmtKind::Spawn { at, .. } => value_insts(at, out),
        StmtKind::Move { toward, by, .. } => {
            value_insts(toward, out);
            value_insts(by, out);
        }
        StmtKind::MoveDir { by, .. } => value_insts(by, out),
        StmtKind::If { branches, .. } => {
            for (c, _) in branches {
                condition_insts(c, out);
            }
        }
        StmtKind::While { cond, .. } => condition_insts(cond, out),
        _ => {}
    }
}

/// Every node a statement implements: the statement's own node first, then the data nodes in
/// its inputs. (Statements inside a block are separate.)
pub fn stmt_insts(s: &Stmt, ctx: &Ctx) -> Vec<Inst> {
    let mut out = vec![stmt_inst(&s.kind, ctx)];
    stmt_data(&s.kind, &mut out);
    out
}

// ---- markers ----

/// A `// node 6: Move Object` marker.
#[derive(Debug, Clone, PartialEq)]
pub struct Marker {
    pub id: u32,
    pub name: String,
    /// Anything in `[brackets]` after the name; for people, ignored by the checks.
    pub detail: Option<String>,
}

/// Read a comment (without its `//`) as a node marker, if it is one.
pub fn parse_marker(comment: &str) -> Option<Marker> {
    let rest = comment.trim().strip_prefix("node ")?;
    let (id, name) = rest.split_once(':')?;
    let id: u32 = id.trim().parse().ok()?;
    let name = name.trim();
    if name.is_empty() {
        return None;
    }
    let (name, detail) = match name.split_once('[') {
        Some((n, d)) => (n.trim(), Some(d.trim_end().trim_end_matches(']').trim().to_string())),
        None => (name, None),
    };
    Some(Marker { id, name: name.to_string(), detail })
}

#[derive(Debug, Clone, PartialEq)]
pub enum NodeStatus {
    /// The sentence still implements this node.
    Matches,
    /// The sentence no longer implements this node: someone edited it by hand. `now` lists the
    /// nodes it does implement.
    Modified { now: Vec<String> },
    /// The marker names a node that isn't in the table.
    UnknownNode,
    /// Another marker already used this id.
    DuplicateId,
}

#[derive(Debug, Clone, PartialEq)]
pub struct NodeReport {
    pub id: u32,
    pub name: String,
    pub line: usize,
    pub status: NodeStatus,
    /// The node's input pins, read from the text (empty unless the node still matches).
    pub pins: Vec<(String, String)>,
}

/// A sentence with no marker: written by hand, or never produced by a node.
#[derive(Debug, Clone, PartialEq)]
pub struct CustomCode {
    pub line: usize,
    pub text: String,
    /// The nodes this sentence would be, if it became nodes.
    pub nodes: Vec<String>,
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Analysis {
    pub nodes: Vec<NodeReport>,
    pub custom: Vec<CustomCode>,
}

/// Compare every marker in a file with the sentence below it.
pub fn analyze(file: &File) -> Analysis {
    let ctx = Ctx::of(file);
    let mut a = Analysis::default();
    let mut seen = HashSet::new();
    for item in &file.implement {
        let inst = item_inst(item);
        report(&item.notes, item.line, vec![inst.clone()], &item_head(item), &mut a, &mut seen);
        let body = match &item.kind {
            ItemKind::Handler { body, .. } | ItemKind::Action { body, .. } => body,
        };
        walk(body, &ctx, &mut a, &mut seen);
    }
    a
}

fn walk(body: &[Stmt], ctx: &Ctx, a: &mut Analysis, seen: &mut HashSet<u32>) {
    for s in body {
        report(&s.notes, s.line, stmt_insts(s, ctx), &stmt_head(s), a, seen);
        match &s.kind {
            StmtKind::If { branches, otherwise, inline } => {
                if !*inline {
                    for (_, b) in branches {
                        walk(b, ctx, a, seen);
                    }
                    if let Some(b) = otherwise {
                        walk(b, ctx, a, seen);
                    }
                }
            }
            StmtKind::Repeat { body, .. }
            | StmtKind::ForEach { body, .. }
            | StmtKind::ForRange { body, .. }
            | StmtKind::While { body, .. }
            | StmtKind::After { body, .. }
            | StmtKind::OnlyOnce { body, .. }
            | StmtKind::FirstTimes { body, .. }
            | StmtKind::Through { body, .. }
            | StmtKind::Animate { body, .. } => walk(body, ctx, a, seen),
            StmtKind::Alternate { first, second, .. } => {
                walk(first, ctx, a, seen);
                walk(second, ctx, a, seen);
            }
            StmtKind::Choose { cases, otherwise, .. } => {
                for c in cases {
                    walk(&c.body, ctx, a, seen);
                }
                if let Some(b) = otherwise {
                    walk(b, ctx, a, seen);
                }
            }
            _ => {}
        }
    }
}

fn report(notes: &Notes, line: usize, insts: Vec<Inst>, text: &str, a: &mut Analysis, seen: &mut HashSet<u32>) {
    let markers: Vec<Marker> = notes.leading.iter().filter_map(|c| parse_marker(c)).collect();
    if markers.is_empty() {
        a.custom.push(CustomCode { line, text: text.to_string(), nodes: insts.iter().map(|i| i.name.to_string()).collect() });
        return;
    }
    for m in markers {
        let found = insts.iter().find(|i| i.name == m.name);
        let status = if !seen.insert(m.id) {
            NodeStatus::DuplicateId
        } else if def(&m.name).is_none() {
            NodeStatus::UnknownNode
        } else if found.is_some() {
            NodeStatus::Matches
        } else {
            NodeStatus::Modified { now: insts.iter().map(|i| i.name.to_string()).collect() }
        };
        let pins = if status == NodeStatus::Matches { found.map(|i| i.pins.clone()).unwrap_or_default() } else { Vec::new() };
        a.nodes.push(NodeReport { id: m.id, name: m.name, line, status, pins });
    }
}

/// A one-line summary of a sentence, for reports.
pub fn sentence(kind: &StmtKind) -> String {
    simple_text(kind)
}
