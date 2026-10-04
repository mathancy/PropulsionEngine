//! The script graph: building it from text, and editing the text through it.

use forge::graph::{self, Graph, GraphNode};
use std::fs;
use std::path::{Path, PathBuf};

fn root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..")
}

/// A script in its own temp folder, so edits never touch the repo's files.
fn script(name: &str, text: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("forge_graph_{name}_{}", std::process::id()));
    // A process id can come round again, so a layout file left by an earlier run must not be picked up.
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    let path = dir.join(format!("{name}.forge"));
    fs::write(&path, text).unwrap();
    path
}

const SPINNER: &str = "\
// spinner.forge: spins, and counts

header:
	needs component:Transform

	variable name:Speed type:decimal value:45.0
	variable name:Count type:integer value:0

implement:
	// node 1: On Tick
	every frame:
		// node 2: Rotate Object
		add Speed per second to Transform.Rot.Y
		if Count is above 3:
			print \"many\"
		otherwise:
			add 1 to Count
";

fn node<'a>(g: &'a Graph, id: &str) -> &'a GraphNode {
    g.nodes.iter().find(|n| n.id == id).unwrap_or_else(|| panic!("no node {id}; have {:?}", g.nodes.iter().map(|n| &n.id).collect::<Vec<_>>()))
}

fn text(g: &Graph, id: &str, pin: &str) -> String {
    node(g, id).pins.iter().find(|p| p.name == pin).unwrap_or_else(|| panic!("{id} has no pin {pin}")).text.clone()
}

fn source(path: &Path) -> String {
    fs::read_to_string(path).unwrap()
}

// ---- the shape of a graph ----

#[test]
fn a_script_becomes_nodes_with_execution_order_and_pins() {
    let path = script("shape", SPINNER);
    let g = graph::load(&path).unwrap();

    let tick = node(&g, "h0");
    assert_eq!(tick.name, "On Tick");
    assert_eq!(tick.execs[0].to.as_deref(), Some("h0.0.0"), "the handler runs its first statement");
    assert_eq!(tick.comment.as_deref(), Some("node 1: On Tick"), "the node marker rides along as the comment");

    let rotate = node(&g, "h0.0.0");
    assert_eq!(rotate.name, "Rotate Object");
    assert_eq!(rotate.execs.last().unwrap().to.as_deref(), Some("h0.0.1"), "Next goes to the following statement");
    assert_eq!(text(&g, "h0.0.0", "Target"), "Transform.Rot.Y");
    assert_eq!(text(&g, "h0.0.0", "Operation"), "add");

    let branch = node(&g, "h0.0.1");
    assert_eq!(branch.name, "Branch");
    let labels: Vec<&str> = branch.execs.iter().map(|e| e.label.as_str()).collect();
    assert_eq!(labels, ["Then", "Otherwise", "Next"]);
    assert_eq!(branch.execs[0].to.as_deref(), Some("h0.0.1.0.0"));
    assert_eq!(branch.execs[1].to.as_deref(), Some("h0.0.1.1.0"));
    assert_eq!(branch.execs[2].to, None, "it is the last statement");
    assert_eq!(node(&g, "h0.0.1.0.0").name, "Print Debug Message");
    assert_eq!(node(&g, "h0.0.1.1.0").name, "Change Variable");
    assert_eq!(node(&g, "h0.0.1.1.0").parent, "h0.0.1");
    assert_eq!(node(&g, "h0.0.1.1.0").slot, 1);
}

#[test]
fn a_phrase_in_a_pin_is_a_value_node_wired_into_it() {
    let path = script("phrase", SPINNER);
    let g = graph::load(&path).unwrap();

    let amount = node(&g, "h0.0.0").pins.iter().find(|p| p.name == "Amount").unwrap();
    assert_eq!(amount.wired.as_deref(), Some("h0.0.0.v.Amount"));
    let per_second = node(&g, "h0.0.0.v.Amount");
    assert_eq!(per_second.name, "Per Second");
    assert_eq!(per_second.feeds, Some(("h0.0.0".to_string(), "Amount".to_string())));
    assert_eq!(text(&g, "h0.0.0.v.Amount", "Value"), "Speed");

    let condition = node(&g, "h0.0.1.v.Condition");
    assert_eq!(condition.name, "Compare");
    assert_eq!(text(&g, "h0.0.1.v.Condition", "Left"), "Count");
    assert_eq!(text(&g, "h0.0.1.v.Condition", "Operator"), "is above");
    assert_eq!(text(&g, "h0.0.1.v.Condition", "Right"), "3");
    assert!(node(&g, "h0.0.1.v.Condition").pins[1].choices.iter().any(|c| c == "is below"), "an operator is picked from a list");

    // A plain name or number is just text in the pin, with no node.
    assert!(node(&g, "h0.0.0").pins.iter().find(|p| p.name == "Target").unwrap().wired.is_none());
}

#[test]
fn every_example_and_behavior_makes_a_graph_whose_wires_all_land_somewhere() {
    let known: Vec<&str> = forge::nodes::NODES.iter().map(|n| n.name).collect();
    let mut checked = 0;
    for dir in ["examples", "behaviors"] {
        for entry in fs::read_dir(root().join(dir)).unwrap() {
            let path = entry.unwrap().path();
            if path.extension().is_none_or(|e| e != "forge") {
                continue;
            }
            let g = graph::load(&path).unwrap_or_else(|e| panic!("{}: {e}", path.display()));
            let ids: Vec<&str> = g.nodes.iter().map(|n| n.id.as_str()).collect();
            let mut unique = ids.clone();
            unique.sort();
            unique.dedup();
            assert_eq!(unique.len(), ids.len(), "{}: node ids repeat", path.display());
            for n in &g.nodes {
                assert!(known.contains(&n.name.as_str()), "{}: `{}` isn't in the node table", path.display(), n.name);
                for e in &n.execs {
                    if let Some(to) = &e.to {
                        assert!(ids.contains(&to.as_str()), "{}: {} runs {to}, which doesn't exist", path.display(), n.id);
                    }
                }
                for p in &n.pins {
                    if let Some(w) = &p.wired {
                        assert!(ids.contains(&w.as_str()), "{}: {}.{} is wired to {w}, which doesn't exist", path.display(), n.id, p.name);
                    }
                }
                if let Some((owner, pin)) = &n.feeds {
                    assert!(g.nodes.iter().any(|o| &o.id == owner && o.pins.iter().any(|p| &p.name == pin)), "{}: {} feeds nothing", path.display(), n.id);
                }
            }
            let json = g.to_json();
            assert_eq!(json.matches('{').count(), json.matches('}').count(), "{}: unbalanced JSON", path.display());
            assert_eq!(json.matches('[').count(), json.matches(']').count());
            checked += 1;
        }
    }
    assert!(checked >= 14, "only checked {checked} files");
}

#[test]
fn checker_problems_mark_the_node_on_that_line() {
    let path = script("marked", "header:\n\tneeds component:Transform\n\nimplement:\n\tevery frame:\n\t\tadd Nothing to Transform.Rot.Y\n");
    let g = graph::load(&path).unwrap();
    assert!(node(&g, "h0.0.0").error.as_deref().is_some_and(|e| e.contains("Nothing")), "{:?}", node(&g, "h0.0.0").error);
    assert_eq!(g.errors.len(), 1);
}

#[test]
fn the_node_catalog_lists_what_can_be_added() {
    let json = graph::catalog_json();
    assert!(json.contains("\"name\":\"Move Object\"") && json.contains("\"name\":\"Branch\""));
    assert!(json.contains("\"name\":\"Compare\"") && json.contains("\"produces\":\"condition\""));
    assert!(json.contains("\"name\":\"Distance\"") && json.contains("\"produces\":\"value\""));
    assert!(!json.contains("Define Action"), "an action's signature isn't added from a menu");
}

// ---- edits ----

#[test]
fn editing_a_pin_rewrites_the_sentence_and_keeps_the_comments() {
    let path = script("setpin", SPINNER);
    let g = graph::edit(&path, "set_pin", &["h0.0.0", "Target", "Transform.Rot.X"]).unwrap();
    assert_eq!(text(&g, "h0.0.0", "Target"), "Transform.Rot.X");
    let s = source(&path);
    assert!(s.contains("add Speed per second to Transform.Rot.X"), "{s}");
    assert!(s.contains("// node 2: Rotate Object") && s.contains("// spinner.forge: spins, and counts"), "comments survive:\n{s}");
}

#[test]
fn a_choice_pin_changes_what_the_node_does() {
    let path = script("choice", SPINNER);
    graph::edit(&path, "set_pin", &["h0.0.1.1.0", "Operation", "subtract"]).unwrap();
    assert!(source(&path).contains("subtract 1 from Count"), "{}", source(&path));
    graph::edit(&path, "set_pin", &["h0.0.1.1.0", "Operation", "multiply"]).unwrap();
    assert!(source(&path).contains("multiply Count by 1"), "{}", source(&path));
}

#[test]
fn editing_a_value_node_rebuilds_the_phrase_in_the_pin_it_feeds() {
    let path = script("valuenode", SPINNER);
    let g = graph::edit(&path, "set_pin", &["h0.0.0.v.Amount", "Value", "Count"]);
    // Count is an integer and the target is a decimal rotation, so the checker may object; what matters
    // is the text it tried. Use a decimal variable instead for the success case.
    let _ = g;
    let g = graph::edit(&path, "set_pin", &["h0.0.1.v.Condition", "Right", "9"]).unwrap();
    assert_eq!(text(&g, "h0.0.1.v.Condition", "Right"), "9");
    assert!(source(&path).contains("if Count is above 9:"), "{}", source(&path));
    graph::edit(&path, "set_pin", &["h0.0.1.v.Condition", "Operator", "is at most"]).unwrap();
    assert!(source(&path).contains("if Count is at most 9:"), "{}", source(&path));
}

#[test]
fn a_field_that_cannot_be_a_sentence_is_refused_and_the_file_is_untouched() {
    let path = script("refuse", SPINNER);
    let before = source(&path);
    let err = graph::edit(&path, "set_pin", &["h0.0.0", "Target", "two words"]).unwrap_err();
    assert!(!err.is_empty());
    assert_eq!(source(&path), before);

    let err = graph::edit(&path, "set_pin", &["h0.0.0", "Target", "Transform.Rot.Y\nfinish"]).unwrap_err();
    assert!(err.contains("one line"), "{err}");
    assert_eq!(source(&path), before);

    assert!(graph::edit(&path, "set_pin", &["h9.0.0", "Target", "x"]).is_err(), "no such node");
    assert!(graph::edit(&path, "nonsense", &[]).is_err(), "no such operation");
    assert_eq!(source(&path), before);
}

// ---- replacing the whole text (typing in the code view) ----

#[test]
fn replacing_the_text_writes_it_as_typed_and_rebuilds_the_graph() {
    let path = script("set_source", SPINNER);
    // Odd spacing and an extra blank line: the file keeps exactly what was typed (no re-formatting).
    let typed = SPINNER.replace("add 1 to Count", "add 2   to Count\n\n\t\t\tprint \"again\"");
    let g = graph::edit(&path, "set_source", &[&typed]).unwrap();
    assert_eq!(source(&path), typed);
    assert_eq!(text(&g, "h0.0.1.1.0", "Amount"), "2", "the graph is the new text");
    assert!(g.nodes.iter().any(|n| n.name == "Print Debug Message" && n.line > 0));

    // A graph edit afterwards formats it like any other.
    graph::edit(&path, "set_pin", &["h0.0.1.1.0", "Amount", "3"]).unwrap();
    assert!(source(&path).contains("add 3 to Count"), "{}", source(&path));
    assert!(!source(&path).contains("   to Count"), "{}", source(&path));
}

#[test]
fn replacing_the_text_with_one_that_does_not_parse_is_refused_and_the_file_is_untouched() {
    let path = script("set_source_bad", SPINNER);
    let before = source(&path);
    let err = graph::edit(&path, "set_source", &[&SPINNER.replace("\t\t\tprint \"many\"", "    print \"many\"")]).unwrap_err();
    assert!(err.contains("tabs"), "{err}");
    assert_eq!(source(&path), before);
    assert!(graph::edit(&path, "set_source", &[&SPINNER.replace("every frame:", "every frame")]).is_err());
    assert_eq!(source(&path), before);
    // No stray file is left beside it.
    let leftovers: Vec<_> = fs::read_dir(path.parent().unwrap()).unwrap().map(|e| e.unwrap().file_name().to_string_lossy().into_owned()).filter(|n| n.starts_with('.')).collect();
    assert!(leftovers.is_empty(), "{leftovers:?}");
}

#[test]
fn a_script_that_does_not_parse_can_be_replaced_by_one_that_does() {
    let path = script("set_source_fix", "header:\n\tbogus\n");
    assert!(graph::load(&path).is_err(), "the file is broken");
    // Any other edit starts from the old text and so refuses; replacing the text does not.
    assert!(graph::edit(&path, "add", &["Finish", "", "0", "0"]).is_err());
    let g = graph::edit(&path, "set_source", &[SPINNER]).unwrap();
    assert!(g.nodes.iter().any(|n| n.name == "On Tick"));
    assert_eq!(source(&path), SPINNER);
}

#[test]
fn replacing_the_text_keeps_the_positions_of_lines_that_did_not_change() {
    let path = script("set_source_layout", SPINNER);
    graph::edit(&path, "set_positions", &["h0\t5\t7\nh0.0.0\t9\t11\n"]).unwrap();
    // Change the second statement's line only; the event and the first statement are the same lines.
    let g = graph::edit(&path, "set_source", &[&SPINNER.replace("if Count is above 3:", "if Count is above 9:")]).unwrap();
    assert_eq!(node(&g, "h0").pos, Some((5, 7)));
    assert_eq!(node(&g, "h0.0.0").pos, Some((9, 11)));
    assert_eq!(node(&g, "h0.0.1").pos, None, "the line that changed is placed afresh");
}

#[test]
fn a_text_can_be_checked_without_a_file() {
    assert!(graph::check_source(SPINNER).is_ok());
    // A problem the checker would raise is not a reason to refuse the text: the graph shows it on its node.
    assert!(graph::check_source(&SPINNER.replace("Transform.Rot.Y", "Nowhere")).is_ok());
    // One that does not parse says where it stopped (1-based) and why.
    let (line, _col, message) = graph::check_source(&SPINNER.replace("\t\t\tprint \"many\"", "    print \"many\"")).unwrap_err();
    assert_eq!(line, 15, "the line with the spaces");
    assert!(message.contains("tabs"), "{message}");
}

#[test]
fn a_wrong_name_is_accepted_and_shows_up_when_compiling() {
    let path = script("lenient", SPINNER);
    // Like Blueprints: the edit goes through, the node shows the problem, and Compile lists it.
    let g = graph::edit(&path, "set_pin", &["h0.0.0", "Target", "Nowhere"]).unwrap();
    assert!(source(&path).contains("Nowhere"), "{}", source(&path));
    let rotate = node(&g, "h0.0.0");
    assert!(rotate.error.as_deref().unwrap_or("").contains("Nowhere"), "{:?}", rotate.error);
    let problem = g.problems.iter().find(|p| p.severity == "error").expect("an error is listed");
    assert_eq!(problem.node.as_deref(), Some("h0.0.0"));
    assert!(problem.message.contains("`Nowhere` isn't declared"), "{}", problem.message);

    // Fixing it clears it.
    let g = graph::edit(&path, "set_pin", &["h0.0.0", "Target", "Transform.Rot.Y"]).unwrap();
    assert!(g.problems.iter().all(|p| p.severity != "error"), "{:?}", g.problems);
    assert!(node(&g, "h0.0.0").error.is_none());
}

#[test]
fn problems_that_were_already_there_do_not_block_an_unrelated_edit() {
    let path = script(
        "preexisting",
        "header:\n\tneeds component:Transform\n\nimplement:\n\tevery frame:\n\t\tadd 1.0 to Transform.Rot.Y\n\t\tadd Nothing to Transform.Rot.X\n",
    );
    let g = graph::edit(&path, "set_pin", &["h0.0.0", "Amount", "2.0"]).unwrap();
    assert_eq!(text(&g, "h0.0.0", "Amount"), "2.0");
    assert_eq!(g.errors.len(), 1, "the old problem is still reported");
}

#[test]
fn adding_a_node_puts_it_where_asked_and_the_script_still_checks() {
    let path = script("addnode", SPINNER);
    let g = graph::edit(&path, "add", &["Print Debug Message", "h0", "0", "1"]).unwrap();
    assert_eq!(node(&g, "h0.0.1").name, "Print Debug Message");
    assert_eq!(node(&g, "h0.0.2").name, "Branch", "what was after it moved down");

    let g = graph::edit(&path, "add", &["Repeat", "h0.0.2", "0", "0"]).unwrap();
    assert_eq!(node(&g, "h0.0.2.0.0").name, "Repeat", "a node can go inside a block's body");
    assert_eq!(node(&g, "h0.0.2.0.0.0.0").name, "Finish", "a new block starts with something in it");

    // Adding a node that needs a part of the header adds that part.
    graph::edit(&path, "add", &["Play Sound", "h0", "0", "0"]).unwrap();
    assert!(source(&path).contains("needs system:Audio"), "{}", source(&path));
}

#[test]
fn a_node_that_needs_a_variable_is_placed_unplugged_and_compile_says_so() {
    let path = script("needs", "header:\n\tneeds component:Transform\n\n\tvariable name:Targets type:list of entity\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");

    // The script has a list, but the node does not take it: it is placed with the list unplugged.
    let g = graph::edit(&path, "add", &["For Each", "h0", "0", "0"]).unwrap();
    assert!(source(&path).contains("for each Item in unset:"), "{}", source(&path));
    let each = node(&g, "h0.0.0");
    assert_eq!(each.name, "For Each");
    assert_eq!(each.error.as_deref(), Some("List isn't plugged in yet"));
    let problem = g.problems.iter().find(|p| p.severity == "error").unwrap();
    assert_eq!(problem.message, "For Each: List isn't plugged in yet");
    assert_eq!(problem.node.as_deref(), Some("h0.0.0"));

    // Plug the list in (what dropping the variable on the pin does): the problem is gone.
    let g = graph::edit(&path, "set_pin", &["h0.0.0", "List", "Targets"]).unwrap();
    assert!(g.problems.iter().all(|p| p.severity != "error"), "{:?}", g.problems);
    assert!(node(&g, "h0.0.0").error.is_none());
    assert!(source(&path).contains("for each Item in Targets:"));
}

#[test]
fn a_variable_can_not_be_called_unset() {
    let path = script("reserved", SPINNER);
    assert!(graph::edit(&path, "add_variable", &["variable name:unset type:integer"]).unwrap_err().contains("unplugged"));
    assert!(graph::edit(&path, "rename_variable", &["Count", "unset"]).unwrap_err().contains("unplugged"));
}

#[test]
fn nodes_that_need_a_variable_all_start_unplugged() {
    for (name, shown) in [
        ("Set Variable", "make unset 0"),
        ("Change Variable", "add 1 to unset"),
        ("Remove From List", "remove 0 from unset"),
        ("Reset Timer", "reset unset"),
    ] {
        let path = script(
            &format!("unplugged_{}", name.replace(' ', "_")),
            "header:\n\tneeds component:Transform\n\n\tvariable name:Count type:integer value:0\n\tvariable name:Items type:list of integer\n\tvariable name:Wait type:timer\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n",
        );
        let g = graph::edit(&path, "add", &[name, "h0", "0", "0"]).unwrap();
        assert!(source(&path).contains(shown), "{name}: {}", source(&path));
        assert!(g.problems.iter().any(|p| p.severity == "error" && p.message.contains("isn't plugged in")), "{name}: {:?}", g.problems);
    }
    let path = script("unplugged_event", "header:\n\tneeds component:Transform\n\n\tvariable name:Count type:integer value:0\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
    let g = graph::edit(&path, "add", &["On Variable Changed", "", "0", "0"]).unwrap();
    assert!(source(&path).contains("when unset changes:"), "{}", source(&path));
    assert!(g.problems.iter().any(|p| p.message.contains("isn't plugged in")), "{:?}", g.problems);
}

#[test]
fn compile_lists_errors_with_their_nodes_and_warns_about_unused_variables() {
    let path = script(
        "compile",
        "header:\n\tneeds component:Transform\n\n\tvariable name:Used type:integer value:0\n\tvariable name:Idle type:integer value:0\n\tvariable name:Shown type:integer value:0 exposed:true\n\nimplement:\n\twhen created:\n\t\tadd 1 to Used\n\t\tadd 1 to Missing\n",
    );
    let g = graph::load(&path).unwrap();
    let errors: Vec<_> = g.problems.iter().filter(|p| p.severity == "error").collect();
    let warnings: Vec<_> = g.problems.iter().filter(|p| p.severity == "warning").collect();
    assert_eq!(errors.len(), 1, "{:?}", g.problems);
    assert_eq!(errors[0].node.as_deref(), Some("h0.0.1"));
    assert!(errors[0].message.contains("`Missing` isn't declared"), "{}", errors[0].message);
    assert_eq!(warnings.len(), 1, "an exposed variable is for placed Objects to set, so only Idle is flagged: {:?}", warnings);
    assert_eq!(warnings[0].variable.as_deref(), Some("Idle"));
    assert!(g.to_json().contains("\"problems\":[{\"severity\":\"error\""), "{}", g.to_json());
}

#[test]
fn every_node_that_needs_something_can_be_added_to_a_bare_script() {
    for name in [
        "Set Variable", "Change Variable", "Remove From List", "Call Action", "Send Event", "Reset Timer", "Spawn Object", "For Each", "Branch", "Loop", "Delay",
    ] {
        let path = script(&format!("bare_{}", name.replace(' ', "_")), "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
        let g = graph::edit(&path, "add", &[name, "h0", "0", "0"]).unwrap_or_else(|e| panic!("{name}: {e}"));
        assert_eq!(node(&g, "h0.0.0").name, name);
        // Whatever it does or doesn't have yet, the file still parses and formats to itself.
        let text = source(&path);
        assert_eq!(forge::format::format_source(&text).unwrap(), text, "{name}");
    }
    for name in ["On Variable Changed", "On Event", "On Overlap Begin", "On Overlap End"] {
        let path = script(&format!("bare_{}", name.replace(' ', "_")), "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
        graph::edit(&path, "add", &[name, "", "0", "0"]).unwrap_or_else(|e| panic!("{name}: {e}"));
    }
    // Value nodes that need a list or an action can be plugged in too.
    let path = script("bare_values", "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
    let g = graph::edit(&path, "plug", &["h0.0.0", "Message", "Count Of"]).unwrap();
    assert!(g.nodes.iter().any(|n| n.name == "Count Of"));
}

fn vars(g: &Graph) -> Vec<String> {
    g.variables.iter().map(|v| v.name.clone()).collect()
}

#[test]
fn variables_are_listed_with_their_type_value_and_exposure() {
    let path = script("varlist", SPINNER);
    let g = graph::load(&path).unwrap();
    assert_eq!(g.components, vec!["Transform".to_string()]);
    let speed = g.variables.iter().find(|v| v.name == "Speed").unwrap();
    assert_eq!((speed.kind, speed.ty.as_str(), speed.value.as_str(), speed.exposed), ("variable", "decimal", "45.0", false));
    let json = g.to_json();
    assert!(json.contains("\"components\":[\"Transform\"]"), "{json}");
    assert!(json.contains("\"name\":\"Speed\"") && json.contains("\"type\":\"decimal\"") && json.contains("\"value\":\"45.0\""), "{json}");
}

#[test]
fn a_variable_can_be_added_changed_and_deleted_in_the_header() {
    let path = script("varedit", SPINNER);

    // Added after the other variables, in the header.
    let g = graph::edit(&path, "add_variable", &["variable name:Health type:integer value:100 exposed:true"]).unwrap();
    assert_eq!(vars(&g), vec!["Speed", "Count", "Health"]);
    let health = g.variables.iter().find(|v| v.name == "Health").unwrap();
    assert!(health.exposed && health.value == "100");
    let text = source(&path);
    assert!(text.find("variable name:Health").unwrap() < text.find("implement:").unwrap(), "{text}");

    // Vectors have parts, lists have none.
    let g = graph::edit(&path, "add_variable", &["variable name:Home type:vector3 x:1 y:2 z:3"]).unwrap();
    assert_eq!(g.variables.iter().find(|v| v.name == "Home").unwrap().parts, "x:1 y:2 z:3");
    let g = graph::edit(&path, "add_variable", &["variable name:Targets type:list of entity"]).unwrap();
    assert_eq!(g.variables.iter().find(|v| v.name == "Targets").unwrap().ty, "list of entity");

    // A name already taken, or something that is not a declaration, is refused and nothing changes.
    let before = source(&path);
    assert!(graph::edit(&path, "add_variable", &["variable name:Health type:integer"]).unwrap_err().contains("already declared"));
    assert!(graph::edit(&path, "add_variable", &["needs component:Audio"]).is_err());
    assert_eq!(source(&path), before);
    // A value of the wrong type is accepted, and Compile reports it.
    let g = graph::edit(&path, "add_variable", &["variable name:Bad type:integer value:\"text\""]).unwrap();
    assert!(g.problems.iter().any(|p| p.severity == "error" && p.variable.as_deref() == Some("Bad")), "{:?}", g.problems);
    graph::edit(&path, "delete_variable", &["Bad"]).unwrap();

    // Change a variable's type, value and exposure, keeping its place.
    let g = graph::edit(&path, "set_variable", &["Health", "variable name:Health type:decimal value:50.5"]).unwrap();
    let health = g.variables.iter().find(|v| v.name == "Health").unwrap();
    assert_eq!((health.ty.as_str(), health.value.as_str(), health.exposed), ("decimal", "50.5", false));
    assert!(graph::edit(&path, "set_variable", &["Health", "variable name:Other type:decimal"]).unwrap_err().contains("rename"));

    // Changing or deleting a variable the script uses is allowed: the nodes that use it show the problem.
    let g = graph::edit(&path, "set_variable", &["Count", "variable name:Count type:text value:\"x\""]).unwrap();
    assert!(g.problems.iter().any(|p| p.severity == "error"), "{:?}", g.problems);
    graph::edit(&path, "set_variable", &["Count", "variable name:Count type:integer value:0"]).unwrap();
    let g = graph::edit(&path, "delete_variable", &["Count"]).unwrap();
    assert!(g.problems.iter().any(|p| p.severity == "error" && p.message.contains("`Count` isn't declared")), "{:?}", g.problems);
    graph::edit(&path, "add_variable", &["variable name:Count type:integer value:0"]).unwrap();

    // One nothing uses can go.
    let g = graph::edit(&path, "delete_variable", &["Health"]).unwrap();
    assert!(!vars(&g).contains(&"Health".to_string()));
    assert!(graph::edit(&path, "delete_variable", &["Nope"]).is_err());
}

#[test]
fn the_first_variable_of_a_script_that_had_none_gets_a_header_of_its_own() {
    let path = script("firstvar", "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
    graph::edit(&path, "add_variable", &["variable name:Score type:integer value:0"]).unwrap();
    assert_eq!(source(&path), "header:\n\tneeds component:Transform\n\n\tvariable name:Score type:integer value:0\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
}

#[test]
fn renaming_a_variable_updates_every_use_and_nothing_else() {
    let path = script(
        "rename",
        "// r.forge\n\nheader:\n\tneeds component:Transform\n\n\tvariable name:Speed type:decimal value:45.0\n\nimplement:\n\tevery frame:\n\t\t// spins at Speed\n\t\tadd Speed per second to Transform.Rot.Y\n\t\tprint \"Speed\"\n\t\tif Speed is above 3.0:\n\t\t\tmake Speed 1.0\n",
    );
    graph::edit(&path, "set_positions", &["h0\t1\t1\nh0.0.0\t2\t2\nh0.0.2\t3\t3"]).unwrap();
    let g = graph::edit(&path, "rename_variable", &["Speed", "Spin"]).unwrap();
    let text = source(&path);
    assert!(text.contains("variable name:Spin type:decimal value:45.0"), "{text}");
    assert!(text.contains("add Spin per second to Transform.Rot.Y"), "{text}");
    assert!(text.contains("if Spin is above 3.0:") && text.contains("make Spin 1.0"), "{text}");
    assert!(text.contains("print \"Speed\"") && text.contains("// spins at Speed"), "strings and comments are not touched: {text}");
    assert!(g.errors.is_empty(), "{:?}", g.errors);
    assert_eq!(node(&g, "h0.0.0").pos, Some((2, 2)), "nodes keep their positions");
    assert_eq!(node(&g, "h0").pos, Some((1, 1)));
    assert_eq!(node(&g, "h0.0.2").pos, Some((3, 3)));

    // Names that can't be used are refused, and the script is not changed.
    let before = source(&path);
    assert!(graph::edit(&path, "rename_variable", &["Spin", "two words"]).is_err());
    assert!(graph::edit(&path, "rename_variable", &["Spin", "Transform.X"]).is_err());
    assert!(graph::edit(&path, "rename_variable", &["Missing", "Other"]).is_err());
    assert_eq!(source(&path), before);
    // Renaming to the same name is nothing.
    graph::edit(&path, "rename_variable", &["Spin", "Spin"]).unwrap();
    assert_eq!(source(&path), before);
}

#[test]
fn a_rename_leaves_fields_with_the_same_name_alone() {
    let path = script(
        "rename_field",
        "header:\n\tneeds component:Transform\n\n\tvariable name:X type:decimal value:1.0\n\nimplement:\n\twhen created:\n\t\tmake Transform.Loc.X X\n",
    );
    graph::edit(&path, "rename_variable", &["X", "Offset"]).unwrap();
    assert!(source(&path).contains("make Transform.Loc.X Offset"), "{}", source(&path));
}

#[test]
fn adding_an_event_adds_a_handler() {
    let path = script("addevent", SPINNER);
    let g = graph::edit(&path, "add", &["On Begin Play", "", "0", "0"]).unwrap();
    assert_eq!(node(&g, "h1").name, "On Begin Play");
    assert!(source(&path).contains("when created:"));
    let g = graph::edit(&path, "set_pin", &["h0", "Seconds", "2"]).unwrap();
    assert_eq!(text(&g, "h0", "Seconds"), "2");
    assert!(source(&path).contains("every 2 seconds:"));
    graph::edit(&path, "set_pin", &["h0", "Seconds", ""]).unwrap();
    assert!(source(&path).contains("every frame:"), "an empty Seconds goes back to every frame");
}

#[test]
fn deleting_removes_a_node_and_what_is_inside_it() {
    let path = script("delete", SPINNER);
    let g = graph::edit(&path, "delete", &["h0.0.1"]).unwrap();
    assert!(g.nodes.iter().all(|n| n.name != "Branch" && n.name != "Print Debug Message"), "the branch and its bodies are gone");
    assert!(!source(&path).contains("otherwise"));

    graph::edit(&path, "add", &["On Begin Play", "", "0", "0"]).unwrap();
    graph::edit(&path, "delete", &["h0"]).unwrap();
    assert!(!source(&path).contains("every frame") && source(&path).contains("when created"), "a whole handler can be deleted");
}

#[test]
fn anything_can_be_deleted_and_an_empty_block_is_written_do_nothing() {
    let path = script("empty", SPINNER);
    // The branch, then the one statement left in the handler: the handler is now empty, which is fine.
    graph::edit(&path, "delete", &["h0.0.1"]).unwrap();
    let g = graph::edit(&path, "delete", &["h0.0.0"]).unwrap();
    assert!(source(&path).contains("every frame:\n\t\tdo nothing"), "{}", source(&path));
    assert_eq!(g.nodes.len(), 1, "just the event is left");
    assert!(node(&g, "h0").execs[0].to.is_none());
    assert!(g.errors.is_empty(), "{:?}", g.errors);

    // The only handler can go too; the script is still a script.
    let g = graph::edit(&path, "delete", &["h0"]).unwrap();
    assert!(g.nodes.is_empty());
    assert!(source(&path).contains("implement:\n\tdo nothing"), "{}", source(&path));
    assert!(g.errors.is_empty(), "{:?}", g.errors);

    // And it can be built up again from nothing.
    graph::edit(&path, "add", &["On Tick", "", "0", "0"]).unwrap();
    let g = graph::edit(&path, "add", &["Print Debug Message", "h0", "0", "0"]).unwrap();
    assert!(!source(&path).contains("do nothing"), "{}", source(&path));
    assert_eq!(g.nodes.iter().filter(|n| n.name == "Print Debug Message").count(), 1);
}

#[test]
fn a_block_can_be_emptied_by_moving_its_last_statement_out() {
    let path = script("emptymove", SPINNER);
    // The branch's `print "many"` goes to the end of the handler: the Then body is left empty.
    graph::edit(&path, "move", &["h0.0.1.0.0", "h0", "0", "2"]).unwrap();
    let text = source(&path);
    assert!(text.contains("if Count is above 3:\n\t\t\tdo nothing\n\t\totherwise:"), "{text}");
    // A script with empty blocks still parses and formats to itself.
    let formatted = forge::format::format_source(&text).unwrap();
    assert_eq!(formatted, text);
}

#[test]
fn a_script_with_empty_blocks_and_no_handlers_runs_and_generates_code() {
    use forge::runtime::{StubHost, World};
    let path = script("emptyrun", "header:\n\tneeds component:Transform\n\nimplement:\n\tevery frame:\n\t\tdo nothing\n\twhen created:\n\t\tif Transform.Loc.X is above 1:\n\t\t\tdo nothing\n");
    let project = forge::Project::load(&path).unwrap();
    assert!(project.check(&forge::Manifest::builtin()).iter().all(|r| r.errors.is_empty()));
    let mut w = World::new(project.clone(), StubHost::default()).unwrap_or_else(|_| panic!("checker"));
    let id = w.spawn_entry([0.0; 3]);
    for _ in 0..3 {
        w.step(1.0 / 60.0);
    }
    assert!(w.warnings.is_empty(), "{:?}", w.warnings);
    assert_eq!(w.entities[id].loc, [0.0; 3]);
    assert!(forge::codegen::generate_all(&project).is_ok(), "codegen copes with empty bodies");

    let none = script("nohandlers", "header:\n\tneeds component:Transform\n\nimplement:\n\tdo nothing\n");
    let project = forge::Project::load(&none).unwrap();
    assert!(project.check(&forge::Manifest::builtin()).iter().all(|r| r.errors.is_empty()));
    assert!(World::new(project.clone(), StubHost::default()).is_ok());
    assert!(forge::codegen::generate_all(&project).is_ok());
}

// ---- where the nodes are ----

fn layout(path: &Path) -> String {
    fs::read_to_string(graph::layout_path(path)).unwrap_or_default()
}

fn positions(g: &Graph) -> Vec<(String, Option<(i32, i32)>)> {
    g.nodes.iter().map(|n| (n.id.clone(), n.pos)).collect()
}

#[test]
fn a_script_with_no_layout_has_no_positions_and_moving_a_node_writes_only_the_layout() {
    let path = script("pos_basic", SPINNER);
    let g = graph::load(&path).unwrap();
    assert!(g.nodes.iter().all(|n| n.pos.is_none()));

    let before = source(&path);
    let g = graph::edit(&path, "set_positions", &["h0\t5\t6\nh0.0.0\t20\t8"]).unwrap();
    assert_eq!(source(&path), before, "the script is not rewritten for a move (not even re-formatted)");
    assert_eq!(node(&g, "h0").pos, Some((5, 6)));
    assert_eq!(node(&g, "h0.0.0").pos, Some((20, 8)));
    assert_eq!(node(&g, "h0.0.1").pos, None, "nodes that weren't moved have none");
    assert!(layout(&path).starts_with("forge-layout 1\n"));

    // A fresh load (as after closing and reopening the editor) sees the same.
    let again = graph::load(&path).unwrap();
    assert_eq!(positions(&g), positions(&again));
}

#[test]
fn positions_travel_with_their_nodes_through_edits() {
    let path = script("pos_travel", SPINNER);
    let all: String = graph::load(&path).unwrap().nodes.iter().enumerate().map(|(i, n)| format!("{}\t{}\t{}\n", n.id, i * 10, i * 3)).collect();
    let g = graph::edit(&path, "set_positions", &[all.as_str()]).unwrap();
    let rotate = node(&g, "h0.0.0").pos.unwrap();
    let branch = node(&g, "h0.0.1").pos.unwrap();
    let print_many = node(&g, "h0.0.1.0.0").pos.unwrap();
    let add_one = node(&g, "h0.0.1.1.0").pos.unwrap();

    // Insert a node before everything: every other node shifts down a place but keeps its position.
    let g = graph::edit(&path, "add", &["Print Debug Message", "h0", "0", "0"]).unwrap();
    assert_eq!(node(&g, "h0.0.0").pos, None, "the new node has none yet");
    assert_eq!(node(&g, "h0.0.1").pos, Some(rotate), "Rotate Object moved from index 0 to 1 and kept its place");
    assert_eq!(node(&g, "h0.0.2").pos, Some(branch));
    assert_eq!(node(&g, "h0.0.2.0.0").pos, Some(print_many));

    // Delete the first node: still the same.
    let g = graph::edit(&path, "delete", &["h0.0.0"]).unwrap();
    assert_eq!(node(&g, "h0.0.0").pos, Some(rotate));
    assert_eq!(node(&g, "h0.0.1").pos, Some(branch));

    // Move a statement out of the branch: it keeps its own position.
    let g = graph::edit(&path, "move", &["h0.0.1.1.0", "h0", "0", "0"]).unwrap();
    assert_eq!(node(&g, "h0.0.0").pos, Some(add_one), "the moved node carries its position");
    assert_eq!(node(&g, "h0.0.1").pos, Some(rotate));
    assert_eq!(node(&g, "h0.0.2").pos, Some(branch));

    // Delete the whole handler: nothing is left, and the layout file goes away.
    graph::edit(&path, "delete", &["h0"]).unwrap();
    assert_eq!(layout(&path), "", "no positions left, no layout file");
}

#[test]
fn a_value_node_has_its_own_position_which_goes_when_it_is_unplugged() {
    let path = script("pos_value", SPINNER);
    let g = graph::load(&path).unwrap();
    let vid = g.nodes.iter().find(|n| n.feeds.is_some()).map(|n| n.id.clone()).expect("Per Second is a value node");
    let g = graph::edit(&path, "set_positions", &[format!("{vid}\t-12\t4").as_str()]).unwrap();
    assert_eq!(node(&g, &vid).pos, Some((-12, 4)));

    // It survives an unrelated edit...
    let g = graph::edit(&path, "set_pin", &["h0.0.1.0.0", "Message", "\"lots\""]).unwrap();
    assert_eq!(node(&g, &vid).pos, Some((-12, 4)));

    // ...and is dropped from the file when the value node goes.
    graph::edit(&path, "unplug", &[vid.as_str()]).unwrap();
    assert!(!layout(&path).contains("value"), "{}", layout(&path));
}

#[test]
fn editing_the_script_in_a_text_editor_keeps_the_positions_of_lines_that_did_not_change() {
    let path = script("pos_external", SPINNER);
    graph::edit(&path, "set_positions", &["h0\t1\t1\nh0.0.0\t2\t2\nh0.0.1\t3\t3\nh0.0.1.0.0\t4\t4\nh0.0.1.1.0\t5\t5"]).unwrap();

    // Someone adds a line at the top of the handler, and rewrites another.
    let edited = source(&path).replace("\t\t// node 2: Rotate Object\n", "\t\tprint \"hello\"\n\t\t// node 2: Rotate Object\n").replace("print \"many\"", "print \"lots\"");
    fs::write(&path, edited).unwrap();

    let g = graph::load(&path).unwrap();
    assert_eq!(node(&g, "h0").pos, Some((1, 1)));
    assert_eq!(node(&g, "h0.0.0").pos, None, "the new line has no position");
    assert_eq!(node(&g, "h0.0.1").pos, Some((2, 2)), "Rotate Object moved down a line and kept its place");
    assert_eq!(node(&g, "h0.0.2").pos, Some((3, 3)));
    assert_eq!(node(&g, "h0.0.2.0.0").pos, None, "the rewritten line lost its position");
    assert_eq!(node(&g, "h0.0.2.1.0").pos, Some((5, 5)));
}

#[test]
fn identical_lines_keep_their_own_positions_in_order() {
    let path = script("pos_same", "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"x\"\n\t\tprint \"x\"\n\t\tprint \"x\"\n");
    graph::edit(&path, "set_positions", &["h0.0.0\t1\t0\nh0.0.1\t2\t0\nh0.0.2\t3\t0"]).unwrap();
    let g = graph::edit(&path, "delete", &["h0.0.0"]).unwrap();
    assert_eq!(node(&g, "h0.0.0").pos, Some((2, 0)), "the first node left is the old second one");
    assert_eq!(node(&g, "h0.0.1").pos, Some((3, 0)));
    assert_eq!(positions(&graph::load(&path).unwrap()), positions(&g));
}

#[test]
fn a_layout_file_that_is_damaged_or_stale_is_ignored() {
    let path = script("pos_bad", SPINNER);
    fs::write(graph::layout_path(&path), "not a layout\nnode\tx\ty\n").unwrap();
    let g = graph::load(&path).unwrap();
    assert!(g.nodes.iter().all(|n| n.pos.is_none()));
    fs::write(graph::layout_path(&path), "forge-layout 1\nnode\t1\t2\t0\tsomething that is not in the script\nnode\toops\n").unwrap();
    let g = graph::load(&path).unwrap();
    assert!(g.nodes.iter().all(|n| n.pos.is_none()));
    // Editing works, and writes a clean layout.
    graph::edit(&path, "set_positions", &["h0\t1\t1"]).unwrap();
    assert!(!layout(&path).contains("not in the script"));
}

#[test]
fn moving_a_node_reorders_the_script() {
    let path = script("move", SPINNER);
    // Move the rotate statement into the branch's `otherwise` body, at the start.
    let g = graph::edit(&path, "move", &["h0.0.0", "h0.0.0", "1", "0"]);
    assert!(g.is_err(), "a node can't go inside itself");

    // The branch is now the only statement in the handler's body, so it can't be moved out of it...
    let before = source(&path);
    let moved = graph::edit(&path, "move", &["h0.0.1", "h0", "0", "0"]).unwrap();
    // ...except to the front of the same body, which swaps the two.
    assert_eq!(node(&moved, "h0.0.0").name, "Branch");
    assert_eq!(node(&moved, "h0.0.1").name, "Rotate Object");
    assert_ne!(source(&path), before);

    // Into a body of another node.
    let g = graph::edit(&path, "move", &["h0.0.1", "h0.0.0", "1", "0"]).unwrap();
    assert_eq!(node(&g, "h0.0.0").name, "Branch");
    assert_eq!(node(&g, "h0.0.0.1.0").name, "Rotate Object", "it is first in the otherwise body now");
    assert_eq!(node(&g, "h0.0.0.1.1").name, "Change Variable");
    assert!(g.nodes.iter().filter(|n| n.name == "Rotate Object").count() == 1, "moved, not copied");
}

#[test]
fn plugging_a_value_node_into_a_pin_and_unplugging_it() {
    let path = script("plug", "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
    let g = graph::edit(&path, "plug", &["h0.0.0", "Message", "Join Text"]).unwrap();
    assert_eq!(node(&g, "h0.0.0.v.Message").name, "Join Text");
    assert!(source(&path).contains("print \"\" joined with \"\""), "{}", source(&path));

    let g = graph::edit(&path, "set_pin", &["h0.0.0.v.Message", "Left", "\"Score: \""]).unwrap();
    assert_eq!(text(&g, "h0.0.0.v.Message", "Left"), "\"Score: \"");

    let g = graph::edit(&path, "unplug", &["h0.0.0.v.Message"]).unwrap();
    assert!(g.nodes.iter().all(|n| n.class != forge::nodes::NodeClass::Data), "the value node is gone");
    assert!(source(&path).contains("print \"Score: \""), "it falls back to a plain value:\n{}", source(&path));

    // A node that doesn't fit the pin is refused by the checker: `destroy` takes one name, not a phrase.
    graph::edit(&path, "add", &["Destroy Object", "h0", "0", "0"]).unwrap();
    let before = source(&path);
    let err = graph::edit(&path, "plug", &["h0.0.0", "Target", "Distance"]).unwrap_err();
    assert!(!err.is_empty());
    assert_eq!(source(&path), before);
}

#[test]
fn a_branch_can_grow_else_if_and_otherwise_and_shrink_again() {
    let path = script(
        "branch",
        "header:\n\tneeds component:Transform\n\tvariable name:Count type:integer value:0\n\nimplement:\n\twhen created:\n\t\tif Count is above 3:\n\t\t\tprint \"many\"\n",
    );
    let g = graph::edit(&path, "add_branch", &["h0.0.0", "else_if"]).unwrap();
    let labels: Vec<&str> = node(&g, "h0.0.0").execs.iter().map(|e| e.label.as_str()).collect();
    assert_eq!(labels, ["Then", "Else if", "Next"]);
    assert_eq!(text(&g, "h0.0.0", "Else If 1"), "true is true");
    graph::edit(&path, "set_pin", &["h0.0.0", "Else If 1", "Count is below 0"]).unwrap();
    assert!(source(&path).contains("otherwise if Count is below 0:"), "{}", source(&path));

    let g = graph::edit(&path, "add_branch", &["h0.0.0", "otherwise"]).unwrap();
    let labels: Vec<&str> = node(&g, "h0.0.0").execs.iter().map(|e| e.label.as_str()).collect();
    assert_eq!(labels, ["Then", "Else if", "Otherwise", "Next"]);

    assert!(graph::edit(&path, "add_branch", &["h0.0.0", "otherwise"]).is_err(), "only one otherwise");
    let g = graph::edit(&path, "remove_branch", &["h0.0.0", "2"]).unwrap();
    assert_eq!(node(&g, "h0.0.0").execs.len(), 3, "otherwise is gone");
    let g = graph::edit(&path, "remove_branch", &["h0.0.0", "1"]).unwrap();
    assert_eq!(node(&g, "h0.0.0").execs.len(), 2, "else if is gone");
    assert!(graph::edit(&path, "remove_branch", &["h0.0.0", "0"]).is_err(), "the first condition stays");
}

#[test]
fn the_text_after_graph_edits_is_stable_under_the_formatter() {
    let path = script("stable", SPINNER);
    graph::edit(&path, "add", &["Print Debug Message", "h0", "0", "1"]).unwrap();
    graph::edit(&path, "set_pin", &["h0.0.1", "Message", "\"hello\""]).unwrap();
    graph::edit(&path, "set_pin", &["h0.0.2.v.Condition", "Right", "7"]).unwrap();
    let s = source(&path);
    assert_eq!(forge::format_source(&s).unwrap(), s, "what the graph writes is already canonical");
}

/// Setting any pin to the text it already has must change nothing. This runs every kind of node
/// through its sentence builder and back through the parser, on every real script we have.
#[test]
fn setting_every_pin_to_its_own_text_changes_nothing() {
    let work = std::env::temp_dir().join(format!("forge_graph_identity_{}", std::process::id()));
    let _ = fs::remove_dir_all(&work);
    for dir in ["examples", "behaviors"] {
        let target = work.join(dir);
        fs::create_dir_all(target.join("ui")).unwrap();
        for (from, to) in [(root().join(dir), target.clone()), (root().join(dir).join("ui"), target.join("ui"))] {
            if let Ok(entries) = fs::read_dir(&from) {
                for e in entries.flatten() {
                    if e.path().extension().is_some_and(|x| x == "forge") {
                        fs::copy(e.path(), to.join(e.file_name())).unwrap();
                    }
                }
            }
        }
    }
    let mut edits = 0;
    for dir in ["examples", "behaviors"] {
        for entry in fs::read_dir(work.join(dir)).unwrap() {
            let path = entry.unwrap().path();
            if path.extension().is_none_or(|e| e != "forge") {
                continue;
            }
            let canonical = forge::format_source(&source(&path)).unwrap();
            let g = graph::load(&path).unwrap();
            for n in &g.nodes {
                for p in &n.pins {
                    if !p.editable || p.text.is_empty() {
                        continue;
                    }
                    graph::edit(&path, "set_pin", &[&n.id, &p.name, &p.text])
                        .unwrap_or_else(|e| panic!("{} {} pin {} = `{}`: {e}", path.display(), n.id, p.name, p.text));
                    assert_eq!(source(&path), canonical, "{} {} pin {} changed the script", path.display(), n.id, p.name);
                    edits += 1;
                }
            }
        }
    }
    assert!(edits > 150, "only {edits} pins were exercised");
}

#[test]
fn a_refused_edit_never_touches_the_real_file_and_leaves_nothing_behind() {
    let path = script("untouched", SPINNER);
    let modified = || fs::metadata(&path).unwrap().modified().unwrap();
    let before = modified();
    std::thread::sleep(std::time::Duration::from_millis(30));
    assert!(graph::edit(&path, "set_pin", &["h0.0.0", "Target", "two words"]).is_err());
    assert_eq!(modified(), before, "the file wasn't even rewritten");
    let strays: Vec<_> = fs::read_dir(path.parent().unwrap()).unwrap().flatten().filter(|e| e.file_name() != "untouched.forge").collect();
    assert!(strays.is_empty(), "left behind: {:?}", strays.iter().map(|e| e.file_name()).collect::<Vec<_>>());

    graph::edit(&path, "set_pin", &["h0.0.0", "Target", "Transform.Rot.X"]).unwrap();
    let strays: Vec<_> = fs::read_dir(path.parent().unwrap()).unwrap().flatten().filter(|e| e.file_name() != "untouched.forge").collect();
    assert!(strays.is_empty(), "left behind after a good edit: {:?}", strays.iter().map(|e| e.file_name()).collect::<Vec<_>>());
}

fn picks<'a>(g: &'a Graph, id: &str, pin: &str) -> &'a [String] {
    let p = node(g, id).pins.iter().find(|p| p.name == pin).unwrap_or_else(|| panic!("{id} has no pin {pin}"));
    assert!(p.picks, "{id}.{pin} should be picked from a list");
    &p.choices
}

const WORLD: &str = "header:\n\tneeds component:Transform\n\tneeds entities:Foes tag:\"enemy\"\n\n\tvariable name:Targets type:list of entity\n\tvariable name:Count type:integer value:0\n\tvariable name:Wait type:timer\n\tconstant name:Names type:list of text\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n";

#[test]
fn a_pin_that_names_a_variable_offers_what_fits_it() {
    let path = script("picks", WORLD);
    graph::edit(&path, "add", &["For Each", "h0", "0", "0"]).unwrap();
    graph::edit(&path, "add", &["Reset Timer", "h0", "0", "1"]).unwrap();
    graph::edit(&path, "add", &["Set Variable", "h0", "0", "2"]).unwrap();
    let g = graph::edit(&path, "add", &["Remove From List", "h0", "0", "3"]).unwrap();

    // A list pin offers the lists (and the entities the script reaches for), not the integer or the timer.
    assert_eq!(picks(&g, "h0.0.0", "List"), ["Targets", "Names", "Foes"]);
    assert_eq!(picks(&g, "h0.0.3", "List"), ["Targets", "Names", "Foes"]);
    assert_eq!(picks(&g, "h0.0.1", "Timer"), ["Wait"]);
    assert_eq!(picks(&g, "h0.0.2", "Target"), ["Targets", "Count"]);
    // The loop's own name and the value are ordinary fields.
    assert!(!node(&g, "h0.0.0").pins.iter().find(|p| p.name == "Item").unwrap().picks);
    assert!(!node(&g, "h0.0.2").pins.iter().find(|p| p.name == "Value").unwrap().picks);
    // Picking one is an ordinary pin edit.
    let g = graph::edit(&path, "set_pin", &["h0.0.0", "List", "Foes"]).unwrap();
    assert!(node(&g, "h0.0.0").error.is_none(), "{:?}", node(&g, "h0.0.0").error);
    assert!(g.to_json().contains("\"picks\":true"));

    // The event, and a value node inside a sentence, pick too.
    let g = graph::edit(&path, "add", &["On Variable Changed", "", "0", "1"]).unwrap();
    let event = g.nodes.iter().find(|n| n.name == "On Variable Changed").unwrap();
    assert_eq!(picks(&g, &event.id, "Variable"), ["Targets", "Count", "Wait"]);
    graph::edit(&path, "set_pin", &["h0.0.2", "Value", "count of Targets"]).unwrap();
    let g = graph::load(&path).unwrap();
    let count = g.nodes.iter().find(|n| n.name == "Count Of").unwrap();
    assert_eq!(picks(&g, &count.id, "List"), ["Targets", "Names", "Foes"]);
}

#[test]
fn the_choices_follow_the_header() {
    let path = script("picks_follow", WORLD);
    let g = graph::edit(&path, "add", &["For Each", "h0", "0", "0"]).unwrap();
    assert_eq!(picks(&g, "h0.0.0", "List"), ["Targets", "Names", "Foes"]);
    let g = graph::edit(&path, "add_variable", &["variable name:Extra type:list of integer"]).unwrap();
    assert_eq!(picks(&g, "h0.0.0", "List"), ["Targets", "Names", "Extra", "Foes"]);
    let g = graph::edit(&path, "delete_reference", &["Foes"]).unwrap();
    assert_eq!(picks(&g, "h0.0.0", "List"), ["Targets", "Names", "Extra"]);
}

#[test]
fn references_to_other_entities_and_scripts_are_listed_and_edited_in_the_header() {
    let path = script("refs", "header:\n\tneeds component:Transform\n\n\tvariable name:Count type:integer value:0\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
    fs::write(path.with_file_name("coin.forge"), "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"coin\"\n").unwrap();

    graph::edit(&path, "add_reference", &["needs entity:Player file:\"coin.forge\" tag:\"player\""]).unwrap();
    graph::edit(&path, "add_reference", &["needs entities:Foes tag:\"enemy\""]).unwrap();
    let g = graph::edit(&path, "add_reference", &["needs file:\"coin.forge\" as Coin when:startup"]).unwrap();
    let kinds: Vec<(&str, &str)> = g.references.iter().map(|r| (r.name.as_str(), r.kind)).collect();
    assert_eq!(kinds, [("Player", "entity"), ("Foes", "entities"), ("Coin", "file")]);
    let coin = &g.references[2];
    assert_eq!((coin.file.as_str(), coin.when.as_str()), ("coin.forge", "startup"));
    assert_eq!(g.references[0].tag, "player");
    // They sit with the other `needs` lines, above the variables.
    let text = source(&path);
    assert!(text.find("needs entity:Player").unwrap() < text.find("variable name:Count").unwrap(), "{text}");
    assert!(g.to_json().contains("\"references\":[{\"name\":\"Player\""));
    assert!(g.problems.iter().all(|p| p.severity != "error"), "{:?}", g.problems);

    // A name that is taken, a variable, or a bad file is refused or reported.
    assert!(graph::edit(&path, "add_reference", &["needs entities:Count tag:\"x\""]).unwrap_err().contains("already declared"));
    assert!(graph::edit(&path, "add_reference", &["needs entities:unset tag:\"x\""]).unwrap_err().contains("unplugged"));
    assert!(graph::edit(&path, "add_reference", &["variable name:Nope type:integer"]).is_err());
    let g = graph::edit(&path, "add_reference", &["needs file:\"missing.forge\" as Gone"]).unwrap();
    assert!(g.problems.iter().any(|p| p.severity == "error" && p.variable.as_deref() == Some("Gone")), "{:?}", g.problems);
    graph::edit(&path, "delete_reference", &["Gone"]).unwrap();

    // Change one, keeping its place and name.
    let g = graph::edit(&path, "set_reference", &["Foes", "needs entities:Foes tag:\"boss\""]).unwrap();
    assert_eq!(g.references[1].tag, "boss");
    assert!(graph::edit(&path, "set_reference", &["Foes", "needs entities:Other tag:\"boss\""]).unwrap_err().contains("rename"));

    // Rename it everywhere it is used, and delete it.
    graph::edit(&path, "add", &["For Each", "h0", "0", "0"]).unwrap();
    graph::edit(&path, "set_pin", &["h0.0.0", "List", "Foes"]).unwrap();
    graph::edit(&path, "rename_reference", &["Foes", "Bosses"]).unwrap();
    assert!(source(&path).contains("for each Item in Bosses:") && source(&path).contains("needs entities:Bosses"), "{}", source(&path));
    let g = graph::edit(&path, "rename_reference", &["Coin", "Pickup"]).unwrap();
    assert!(g.references.iter().any(|r| r.name == "Pickup"), "{}", source(&path));
    let g = graph::edit(&path, "delete_reference", &["Bosses"]).unwrap();
    assert!(g.problems.iter().any(|p| p.severity == "error" && p.message.contains("`Bosses` isn't declared")), "{:?}", g.problems);
    assert!(graph::edit(&path, "delete_reference", &["Nope"]).is_err());
}

#[test]
fn a_variable_carried_to_a_pin_by_a_get_node_stays_as_a_node() {
    let path = script("getnode", WORLD);
    graph::edit(&path, "add", &["For Each", "h0", "0", "0"]).unwrap();

    // The pin names the variable, but with no place in the layout it is just a field.
    let g = graph::edit(&path, "set_pin", &["h0.0.0", "List", "Targets"]).unwrap();
    assert!(g.nodes.iter().all(|n| n.name != "Get Targets"), "no Get node until it has a place");

    // Giving the pin's value a place shows a Get node there, wired into the pin.
    let g = graph::edit(&path, "set_positions", &["h0.0.0.v.List\t-12\t7\n"]).unwrap();
    let get = node(&g, "h0.0.0.v.List");
    assert_eq!((get.name.as_str(), get.category.as_str()), ("Get Targets", "Variables"));
    assert_eq!(get.pos, Some((-12, 7)));
    assert_eq!(get.feeds, Some(("h0.0.0".to_string(), "List".to_string())));
    assert!(get.pins.is_empty());
    let list = node(&g, "h0.0.0").pins.iter().find(|p| p.name == "List").unwrap();
    assert_eq!(list.wired.as_deref(), Some("h0.0.0.v.List"));
    assert!(!list.picks || list.wired.is_some());
    // The script is plain: only the layout file knows about it.
    assert!(source(&path).contains("for each Item in Targets:") && !source(&path).contains("Get"), "{}", source(&path));
    let layout = fs::read_to_string(graph::layout_path(&path)).unwrap();
    assert!(layout.contains("value\t-12\t7\t0\tList\t"), "{layout}");

    // It is there the next time the script is opened, and a rename follows the variable.
    assert_eq!(node(&graph::load(&path).unwrap(), "h0.0.0.v.List").name, "Get Targets");
    let g = graph::edit(&path, "rename_variable", &["Targets", "Foes2"]).unwrap();
    assert_eq!(node(&g, "h0.0.0.v.List").name, "Get Foes2");
    assert_eq!(node(&g, "h0.0.0.v.List").pos, Some((-12, 7)));

    // Moving it is saved like any value node.
    let g = graph::edit(&path, "set_positions", &["h0.0.0.v.List\t-20\t9\n"]).unwrap();
    assert_eq!(node(&g, "h0.0.0.v.List").pos, Some((-20, 9)));

    // Deleting it unplugs the pin, and the place is forgotten.
    let g = graph::edit(&path, "unplug", &["h0.0.0.v.List"]).unwrap();
    assert!(g.nodes.iter().all(|n| !n.name.starts_with("Get ")), "the node is gone");
    assert!(source(&path).contains("in unset:"), "{}", source(&path));
    assert!(!fs::read_to_string(graph::layout_path(&path)).unwrap_or_default().contains("List"), "its place is not kept");

    // A place given to a pin that names nothing is not kept, so plugging a variable in later shows no node.
    graph::edit(&path, "set_positions", &["h0.0.0.v.List	-20	9
"]).unwrap();
    let g = graph::edit(&path, "set_pin", &["h0.0.0", "List", "Foes2"]).unwrap();
    assert!(g.nodes.iter().all(|n| !n.name.starts_with("Get ")), "no node from a place that was not kept");
}

#[test]
fn a_script_saved_with_a_byte_order_mark_still_reads() {
    let path = script("bom", &format!("\u{feff}{}", SPINNER));
    let g = graph::load(&path).unwrap();
    assert!(g.errors.is_empty(), "{:?}", g.errors);
    assert!(g.nodes.iter().any(|n| n.name == "On Tick"));
}
