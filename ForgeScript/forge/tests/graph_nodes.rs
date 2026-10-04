//! The graph and the nodes taken from NODELIST.md: every one can be added, plugged in and edited, and what the
//! graph writes is always valid ForgeScript.

use forge::graph::{self, Graph, GraphNode};
use forge::nodes::{NodeClass, NODES};
use std::fs;
use std::path::{Path, PathBuf};

fn script(name: &str, text: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("forge_graph_nodes_{name}_{}", std::process::id()));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    let path = dir.join(format!("{name}.forge"));
    fs::write(&path, text).unwrap();
    path
}

fn node<'a>(g: &'a Graph, id: &str) -> &'a GraphNode {
    g.nodes.iter().find(|n| n.id == id).unwrap_or_else(|| panic!("no node {id}; have {:?}", g.nodes.iter().map(|n| (&n.id, &n.name)).collect::<Vec<_>>()))
}

fn source(path: &Path) -> String {
    fs::read_to_string(path).unwrap()
}

const BASE: &str = "header:\n\tneeds component:Transform\n\n\tvariable name:Door type:gate\n\tvariable name:Items type:list of integer\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n";

/// The same with a struct and a table to make and look up.
const BASE_WITH_TYPES: &str = "header:
	needs component:Transform

	struct name:Thing
		field name:Cost type:integer

	table name:Things type:Thing
		row key:\"Sword\" Cost:5

	variable name:Door type:gate
	variable name:Items type:list of integer

implement:
	when created:
		print \"hi\"
";

fn stable(path: &Path, what: &str) {
    let text = source(path);
    assert_eq!(forge::format::format_source(&text).unwrap(), text, "{what}: what the graph writes should already be canonical");
}

#[test]
fn every_statement_and_flow_node_can_be_added_and_comes_back_as_itself() {
    let mut added = 0;
    for def in NODES.iter().filter(|n| matches!(n.class, NodeClass::Action | NodeClass::Flow)) {
        let path = script(&format!("add_{}", def.name.replace(' ', "_")), BASE);
        let g = graph::edit(&path, "add", &[def.name, "h0", "0", "0"]).unwrap_or_else(|e| panic!("{}: {e}", def.name));
        assert_eq!(node(&g, "h0.0.0").name, def.name, "{} should come back as itself; the script is\n{}", def.name, source(&path));
        stable(&path, def.name);
        added += 1;
    }
    assert!(added > 50, "only {added} nodes were tried");
}

#[test]
fn every_event_node_can_be_added() {
    // (`On Event From` needs another script to listen to, which the graph can't pick yet: it is written in the text.)
    for def in NODES.iter().filter(|n| n.class == NodeClass::Event && n.name != "Define Action" && n.name != "On Event From") {
        let path = script(&format!("event_{}", def.name.replace(' ', "_")), BASE);
        let g = graph::edit(&path, "add", &[def.name, "", "0", "0"]).unwrap_or_else(|e| panic!("{}: {e}", def.name));
        assert!(g.nodes.iter().any(|n| n.name == def.name), "{} should be in the graph: {}", def.name, source(&path));
        stable(&path, def.name);
    }
}

#[test]
fn every_value_node_can_be_plugged_into_a_pin() {
    const CONDITIONS: &[&str] = &["Compare", "Input State", "Is Valid", "Contains", "And", "Or"];
    let mut plugged = 0;
    for def in NODES.iter().filter(|n| n.class == NodeClass::Data) {
        let path = script(&format!("plug_{}", def.name.replace(' ', "_")), BASE_WITH_TYPES);
        let condition = CONDITIONS.contains(&def.name);
        let g = if condition {
            graph::edit(&path, "add", &["Branch", "h0", "0", "0"]).unwrap();
            graph::edit(&path, "plug", &["h0.0.0", "Condition", def.name])
        } else {
            graph::edit(&path, "add", &["Print Debug Message", "h0", "0", "0"]).unwrap();
            graph::edit(&path, "plug", &["h0.0.0", "Message", def.name])
        }
        .unwrap_or_else(|e| panic!("{}: {e}", def.name));
        assert!(g.nodes.iter().any(|n| n.name == def.name && n.feeds.is_some()), "{} should be wired into the pin; script:\n{}", def.name, source(&path));
        stable(&path, def.name);
        plugged += 1;
    }
    assert!(plugged > 40, "only {plugged} value nodes were tried");
}

#[test]
fn a_value_node_can_be_edited_and_the_phrase_follows() {
    let path = script("edit_phrase", BASE);
    graph::edit(&path, "add", &["Set Variable", "h0", "0", "0"]).unwrap();
    graph::edit(&path, "set_pin", &["h0.0.0", "Target", "Transform.Loc.Y"]).unwrap();
    graph::edit(&path, "plug", &["h0.0.0", "Value", "Clamp"]).unwrap();
    let g = graph::edit(&path, "set_pin", &["h0.0.0.v.Value", "Max", "9.0"]).unwrap();
    assert!(source(&path).contains("make Transform.Loc.Y clamp 0.0 between 0.0 and 9.0"), "{}", source(&path));
    let clamp = node(&g, "h0.0.0.v.Value");
    assert_eq!(clamp.name, "Clamp");
    assert_eq!(clamp.pins.iter().map(|p| (p.name.as_str(), p.text.as_str())).collect::<Vec<_>>(), vec![("Value", "0.0"), ("Min", "0.0"), ("Max", "9.0")]);

    // A phrase with keys in it: `rotation pitch:... yaw:... roll:...`
    graph::edit(&path, "plug", &["h0.0.0", "Value", "Make Rotator"]).unwrap();
    graph::edit(&path, "set_pin", &["h0.0.0.v.Value", "Yaw", "90.0"]).unwrap();
    assert!(source(&path).contains("rotation pitch:0.0 yaw:90.0 roll:0.0"), "{}", source(&path));

    // A pick, whose middle is a condition.
    graph::edit(&path, "plug", &["h0.0.0", "Value", "Select"]).unwrap();
    let g = graph::edit(&path, "set_pin", &["h0.0.0.v.Value", "Condition", "Items contains 3"]).unwrap();
    assert!(source(&path).contains("1 if Items contains 3 otherwise 0"), "{}", source(&path));
    assert_eq!(node(&g, "h0.0.0.v.Value").pins[1].kind, "condition");

    // The text of a format.
    graph::edit(&path, "plug", &["h0.0.0", "Value", "Format Text"]).unwrap();
    // Putting a gate in the text is accepted (placing nodes is never blocked) and Compile says what is wrong.
    let g = graph::edit(&path, "set_pin", &["h0.0.0.v.Value", "Text", "\"{Door}\""]).unwrap();
    assert!(g.problems.iter().any(|p| p.severity == "error" && p.message.contains("is a gate")), "{:?}", g.problems);
    graph::edit(&path, "set_pin", &["h0.0.0.v.Value", "Text", "\"Items: {Items}\""]).unwrap();
    assert!(source(&path).contains("format \"Items: {Items}\""), "{}", source(&path));
}

#[test]
fn a_node_that_names_a_gate_a_list_or_a_latch_is_a_dropdown_of_what_fits() {
    let path = script(
        "picks",
        "header:\n\tneeds component:Transform\n\n\tvariable name:Door type:gate\n\tvariable name:Count type:integer\n\tvariable name:Items type:list of integer\n\nimplement:\n\twhen created:\n\t\tonly once as Intro:\n\t\t\tfinish\n",
    );
    for (name, pin, expect) in [
        ("Open Gate", "Gate", vec!["Door"]),
        ("Toggle Gate", "Gate", vec!["Door"]),
        ("Reset Timer", "Timer", vec!["Intro"]),
        ("Clear List", "List", vec!["Items"]),
        ("Add Unique", "List", vec!["Items"]),
        ("Remove At", "List", vec!["Items"]),
    ] {
        let g = graph::edit(&path, "add", &[name, "h0", "0", "1"]).unwrap_or_else(|e| panic!("{name}: {e}"));
        let p = node(&g, "h0.0.1").pins.iter().find(|p| p.name == pin).unwrap();
        assert!(p.picks, "{name}.{pin} should be a dropdown");
        assert_eq!(p.choices, expect, "{name}.{pin}");
        graph::edit(&path, "delete", &["h0.0.1"]).unwrap();
    }
    // A gate can't be set with Set Variable, so the dropdown there leaves it out.
    let g = graph::edit(&path, "add", &["Set Variable", "h0", "0", "1"]).unwrap();
    let p = node(&g, "h0.0.1").pins.iter().find(|p| p.name == "Target").unwrap();
    assert_eq!(p.choices, vec!["Count", "Items"]);
}

#[test]
fn a_switch_grows_cases_and_an_otherwise_and_shrinks_again() {
    let path = script("switch", BASE);
    let g = graph::edit(&path, "add", &["Switch", "h0", "0", "0"]).unwrap();
    let sw = node(&g, "h0.0.0");
    assert_eq!(sw.name, "Switch");
    assert_eq!(sw.execs.iter().map(|e| e.label.as_str()).collect::<Vec<_>>(), vec!["Case 1", "Next"]);
    assert_eq!(sw.pins.iter().map(|p| (p.name.as_str(), p.text.as_str())).collect::<Vec<_>>(), vec![("Value", "0"), ("Case 1", "1")]);

    let g = graph::edit(&path, "add_branch", &["h0.0.0", "case"]).unwrap();
    let g = graph::edit(&path, "add_branch", &["h0.0.0", "otherwise"]).unwrap_or(g);
    let sw = node(&g, "h0.0.0");
    assert_eq!(sw.execs.iter().map(|e| e.label.as_str()).collect::<Vec<_>>(), vec!["Case 1", "Case 2", "Otherwise", "Next"]);
    assert!(source(&path).contains("case 1:") && source(&path).contains("case 2:") && source(&path).contains("otherwise:"), "{}", source(&path));
    stable(&path, "switch");
    assert!(graph::edit(&path, "add_branch", &["h0.0.0", "otherwise"]).is_err(), "only one otherwise");

    // A case's values are edited as text, and a node goes inside a case like inside any block.
    let g = graph::edit(&path, "set_pin", &["h0.0.0", "Case 2", "5 or 6"]).unwrap();
    assert_eq!(node(&g, "h0.0.0").pins.iter().find(|p| p.name == "Case 2").unwrap().text, "5 or 6");
    assert!(source(&path).contains("case 5 or 6:"), "{}", source(&path));
    graph::edit(&path, "add", &["Print Debug Message", "h0.0.0", "1", "0"]).unwrap();
    assert!(graph::edit(&path, "set_pin", &["h0.0.0", "Case 2", "a b"]).is_err(), "not a value a case can have");

    // The value being switched on.
    graph::edit(&path, "set_pin", &["h0.0.0", "Value", "Items"]).unwrap();
    assert!(source(&path).contains("choose Items:"));

    let g = graph::edit(&path, "remove_branch", &["h0.0.0", "2"]).unwrap();
    assert_eq!(node(&g, "h0.0.0").execs.len(), 3, "the otherwise is gone");
    let g = graph::edit(&path, "remove_branch", &["h0.0.0", "0"]).unwrap();
    assert_eq!(node(&g, "h0.0.0").execs.len(), 2, "so is the first case");
    assert!(graph::edit(&path, "remove_branch", &["h0.0.0", "0"]).is_err(), "the last case stays");
}

#[test]
fn flip_flop_gate_do_once_and_timeline_edit_through_their_pins() {
    let path = script("flowpins", BASE);
    graph::edit(&path, "add", &["Do Once", "h0", "0", "0"]).unwrap();
    let g = graph::edit(&path, "set_pin", &["h0.0.0", "Name", "Intro"]).unwrap();
    assert!(source(&path).contains("only once as Intro:"), "{}", source(&path));
    assert_eq!(node(&g, "h0.0.0").execs[0].label, "Do");
    graph::edit(&path, "set_pin", &["h0.0.0", "Name", ""]).unwrap();
    assert!(source(&path).contains("only once:"), "{}", source(&path));

    graph::edit(&path, "add", &["Do N", "h0", "0", "1"]).unwrap();
    graph::edit(&path, "set_pin", &["h0.0.1", "Count", "5"]).unwrap();
    assert!(source(&path).contains("first 5 times:"), "{}", source(&path));

    let g = graph::edit(&path, "add", &["Flip Flop", "h0", "0", "2"]).unwrap();
    assert_eq!(node(&g, "h0.0.2").execs.iter().map(|e| e.label.as_str()).collect::<Vec<_>>(), vec!["A", "B", "Next"]);
    graph::edit(&path, "set_pin", &["h0.0.2", "Name", "Side"]).unwrap();
    assert!(source(&path).contains("alternate as Side:"), "{}", source(&path));
    stable(&path, "flip flop");

    graph::edit(&path, "add", &["Gate", "h0", "0", "3"]).unwrap();
    graph::edit(&path, "set_pin", &["h0.0.3", "Gate", "Door"]).unwrap();
    assert!(source(&path).contains("through Door:"), "{}", source(&path));

    let g = graph::edit(&path, "add", &["Timeline", "h0", "0", "4"]).unwrap();
    assert_eq!(node(&g, "h0.0.4").execs[0].label, "Update");
    graph::edit(&path, "set_pin", &["h0.0.4", "Seconds", "3"]).unwrap();
    graph::edit(&path, "set_pin", &["h0.0.4", "To", "10.0"]).unwrap();
    assert!(source(&path).contains("animate Progress from 0.0 to 10.0 over 3 seconds:"), "{}", source(&path));
    stable(&path, "timeline");

    // Each block keeps what was inside it when its own line is edited.
    graph::edit(&path, "add", &["Print Debug Message", "h0.0.4", "0", "0"]).unwrap();
    graph::edit(&path, "set_pin", &["h0.0.4", "Seconds", "2"]).unwrap();
    assert!(source(&path).contains("over 2 seconds:\n\t\t\tprint"), "{}", source(&path));
    graph::edit(&path, "add", &["Print Debug Message", "h0.0.2", "1", "0"]).unwrap();
    graph::edit(&path, "set_pin", &["h0.0.2", "Name", "Other"]).unwrap();
    let text = source(&path);
    assert!(text.contains("alternate as Other:\n\t\t\tfinish\n\t\totherwise:\n\t\t\tprint"), "{text}");
}

#[test]
fn the_new_nodes_show_problems_in_the_editors_words() {
    let path = script("problems", BASE);
    let g = graph::edit(&path, "add", &["Open Gate", "h0", "0", "0"]).unwrap();
    assert!(g.problems.iter().any(|p| p.message.contains("Open Gate: Gate isn't plugged in yet")), "{:?}", g.problems);
    let g = graph::edit(&path, "set_pin", &["h0.0.0", "Gate", "Count"]).unwrap();
    assert!(g.problems.iter().any(|p| p.severity == "error" && p.message.contains("`Count`")), "{:?}", g.problems);
    let g = graph::edit(&path, "set_pin", &["h0.0.0", "Gate", "Door"]).unwrap();
    assert!(g.problems.iter().all(|p| p.severity != "error"), "{:?}", g.problems);
}

#[test]
fn system_nodes_add_the_system_they_need_to_the_header() {
    for (name, system, line) in [
        ("Play Sound At", "Audio", "do Play on Audio sound:\"sound.wav\" at:Transform.Loc"),
        ("Play Sound Attached", "Audio", "attachedTo:Self"),
        ("Play Animation", "Animation", "do Play on Animation animation:\"animation\""),
        ("Play Montage", "Animation", "do PlayMontage on Animation montage:\"montage\""),
        ("Create Widget", "UI", "do Create on UI widget:\"widget\""),
        ("Open Level", "Game", "do OpenLevel on Game level:\"level\""),
        ("Set Game Paused", "Game", "do SetPaused on Game paused:true"),
    ] {
        let path = script(&format!("system_{}", name.replace(' ', "_")), BASE);
        let g = graph::edit(&path, "add", &[name, "h0", "0", "0"]).unwrap();
        let text = source(&path);
        assert!(text.contains(&format!("needs system:{system}")), "{name}: {text}");
        assert!(text.contains(line), "{name}: {text}");
        assert!(g.problems.iter().all(|p| p.severity != "error"), "{name}: {:?}", g.problems);
        // Its arguments are pins named after the parameters.
        let n = node(&g, "h0.0.0");
        assert!(n.pins.iter().any(|p| p.name == "Action") && n.pins.len() >= 3, "{name}: {:?}", n.pins.iter().map(|p| &p.name).collect::<Vec<_>>());
    }
}

#[test]
fn renaming_a_variable_also_renames_it_inside_format_text() {
    let path = script(
        "rename_format",
        "header:\n\tvariable name:Score type:integer value:0\n\nimplement:\n\twhen created:\n\t\tprint format \"Score is {Score} (not Score)\"\n",
    );
    graph::edit(&path, "rename_variable", &["Score", "Points"]).unwrap();
    let text = source(&path);
    assert!(text.contains("variable name:Points"), "{text}");
    assert!(text.contains("print format \"Score is {Points} (not Score)\""), "only the braces are a use of the name: {text}");
}

/// The nodes a variable can be dragged into when they are not in the script yet: the checker decides.
#[test]
fn a_variable_is_offered_the_new_nodes_that_can_take_it() {
    let text = "header:\n\tneeds component:Transform\n\n\tvariable name:Alive type:boolean value:true\n\tvariable name:Score type:integer value:0\n\tvariable name:Items type:list of integer\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n";
    let path = script("fits", text);
    let offered = |variable: &str| -> Vec<(String, String)> {
        let json = graph::fits(&path, variable).unwrap();
        json.trim_matches(|c| c == '[' || c == ']').split("},{").filter(|s| !s.is_empty()).map(|s| {
            let field = |key: &str| s.split(&format!("\"{key}\":\"")).nth(1).unwrap().split('"').next().unwrap().to_string();
            (field("node"), field("pin"))
        }).collect()
    };
    let has = |list: &[(String, String)], node: &str, pin: &str| list.iter().any(|(n, p)| n == node && p == pin);

    let alive = offered("Alive");
    eprintln!("Alive: {alive:?}");
    assert!(has(&alive, "Branch", "Condition") && has(&alive, "While", "Condition"), "a boolean goes into a condition: {alive:?}");
    assert!(!has(&alive, "Clear List", "List") && !has(&alive, "For Each", "List"), "but not into a list pin: {alive:?}");

    let score = offered("Score");
    eprintln!("Score: {score:?}");
    assert!(has(&score, "Repeat", "Count") && has(&score, "Delay", "Seconds"), "a number goes where a number does: {score:?}");
    assert!(!has(&score, "Branch", "Condition"), "but not into a condition: {score:?}");

    let items = offered("Items");
    eprintln!("Items: {items:?}");
    assert!(has(&items, "For Each", "List") && has(&items, "Clear List", "List"), "a list goes into a list pin: {items:?}");
    assert!(!has(&items, "Repeat", "Count"), "but not where a number goes: {items:?}");

    assert_eq!(source(&path), text, "asking changes nothing");
    assert!(!path.with_file_name(".fits.forge.graph-fit").exists(), "and leaves nothing behind");
}

/// What a Set Variable sets a boolean to is a checkbox: its Value pin is marked as a toggle, and pointing the node at a
/// boolean turns the number it started with into `false`.
#[test]
fn set_variable_on_a_boolean_has_a_true_or_false_value() {
    let text = "header:\n\tneeds component:Transform\n\n\tvariable name:Alive type:boolean value:true\n\tvariable name:Score type:integer value:0\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n";
    let path = script("toggle", text);
    let toggle = |g: &Graph| node(g, "h0.0.0").pins.iter().find(|p| p.name == "Value").map(|p| (p.toggle, p.text.clone())).unwrap();

    let g = graph::edit(&path, "add", &["Set Variable", "h0", "0", "0"]).unwrap();
    assert_eq!(toggle(&g).0, false, "no target yet, so an ordinary value");

    let g = graph::edit(&path, "set_pin", &["h0.0.0", "Target", "Score"]).unwrap();
    assert_eq!(toggle(&g), (false, "0".to_string()), "a number stays a number");

    let g = graph::edit(&path, "set_pin", &["h0.0.0", "Target", "Alive"]).unwrap();
    assert_eq!(toggle(&g), (true, "false".to_string()), "a boolean target turns the leftover number into false: {}", source(&path));
    assert!(g.to_json().contains("\"toggle\":true"));
    assert!(source(&path).contains("make Alive false"), "{}", source(&path));

    let g = graph::edit(&path, "set_pin", &["h0.0.0", "Value", "true"]).unwrap();
    assert_eq!(toggle(&g), (true, "true".to_string()));

    // A value that is something else (another variable) is not forced into a checkbox, and is not overwritten.
    graph::edit(&path, "set_pin", &["h0.0.0", "Value", "Alive"]).unwrap();
    let g = graph::edit(&path, "set_pin", &["h0.0.0", "Target", "Alive"]).unwrap();
    assert_eq!(toggle(&g), (false, "Alive".to_string()));
}
