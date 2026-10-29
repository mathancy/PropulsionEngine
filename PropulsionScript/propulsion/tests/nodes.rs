//! The node table: every node has a sentence, markers are checked against the text, and a
//! hand-edited node is noticed.

use propulsion::ast::*;
use propulsion::nodes::{self, analyze, parse_marker, Ctx, NodeClass, NodeStatus, NODES};
use std::collections::HashSet;
use std::fs;
use std::path::PathBuf;

fn behaviors() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..").join("behaviors")
}

fn read(name: &str) -> String {
    fs::read_to_string(behaviors().join(name)).unwrap().replace("\r\n", "\n")
}

// ---- the table itself ----

#[test]
fn node_names_are_unique_and_every_node_has_a_sentence_and_a_sample() {
    let mut seen = HashSet::new();
    for n in NODES {
        assert!(seen.insert(n.name), "duplicate node name {}", n.name);
        assert!(!n.pattern.is_empty(), "{} has no pattern", n.name);
        assert!(!n.samples.is_empty(), "{} has no sample", n.name);
    }
    assert!(NODES.len() >= 60, "only {} nodes", NODES.len());
}

/// Wrap a sample in the smallest file that parses, and return that file's AST.
fn wrap(def: &nodes::NodeDef, sample: &str) -> File {
    // (a struct, a table and an enum for the samples that use them)
    let mut src = String::from(
        "header:\n\tevent name:Placeholder\n\tenum name:Mode values:Idle Chasing\n\tstruct name:Item\n\t\tfield name:Name type:text\n\t\tfield name:Cost type:integer\n\ttable name:Items type:Item\n\t\trow key:\"Sword\" Cost:5\n",
    );
    for line in def.prelude {
        src.push_str(&format!("\t{line}\n"));
    }
    src.push_str("\nimplement:\n");
    match def.class {
        NodeClass::Event => src.push_str(&format!("\t{sample}:\n\t\tfinish\n")),
        _ => {
            src.push_str("\tevery frame:\n");
            if sample.contains('\n') {
                // A sample that needs a body of its own is written out in full.
                for line in sample.lines() {
                    src.push_str(&format!("\t\t{line}\n"));
                }
            } else if sample.ends_with(':') {
                src.push_str(&format!("\t\t{sample}\n\t\t\tfinish\n"));
            } else {
                src.push_str(&format!("\t\t{sample}\n"));
            }
        }
    }
    propulsion::parse(&src).unwrap_or_else(|e| panic!("sample for {} should parse: {e}\n{src}", def.name))
}

#[test]
fn every_sample_is_classified_as_the_node_it_belongs_to_and_has_the_pins_the_table_lists() {
    for def in NODES {
        for sample in def.samples {
            let file = wrap(def, sample);
            let ctx = Ctx::of(&file);
            let item = &file.implement[0];

            let inst = match def.class {
                NodeClass::Event => {
                    let inst = nodes::item_inst(item);
                    assert_eq!(inst.name, def.name, "sample `{sample}`");
                    inst
                }
                NodeClass::Action | NodeClass::Flow => {
                    let ItemKind::Handler { body, .. } = &item.kind else { panic!("expected a handler") };
                    let insts = nodes::stmt_insts(&body[0], &ctx);
                    assert_eq!(insts[0].name, def.name, "sample `{sample}` is a {}, not a {}", insts[0].name, def.name);
                    insts.into_iter().next().unwrap()
                }
                NodeClass::Data => {
                    let ItemKind::Handler { body, .. } = &item.kind else { panic!("expected a handler") };
                    let insts = nodes::stmt_insts(&body[0], &ctx);
                    let names: Vec<_> = insts.iter().map(|i| i.name).collect();
                    insts
                        .into_iter()
                        .find(|i| i.name == def.name)
                        .unwrap_or_else(|| panic!("sample `{sample}` should contain {} but has {names:?}", def.name))
                }
            };

            let allowed: HashSet<&str> = def.pins.iter().map(|p| p.trim_end_matches('?')).collect();
            let actual: HashSet<&str> = inst.pins.iter().map(|(k, _)| k.as_str()).collect();
            for required in def.pins.iter().filter(|p| !p.ends_with('?')) {
                // Pins that depend on how the sentence is written (arguments) are listed as optional.
                assert!(actual.contains(required), "{} sample `{sample}` is missing pin {required}; has {actual:?}", def.name);
            }
            // A call's arguments are pins named after the action's parameters, so the table can
            // only say "arguments go here" (`Argument?`).
            let takes_arguments = def.pins.contains(&"Argument?");
            for a in &actual {
                assert!(
                    allowed.contains(a) || takes_arguments,
                    "{} sample `{sample}` has pin {a} that the table doesn't list",
                    def.name
                );
            }
        }
    }
}

// ---- markers ----

#[test]
fn markers_are_read_with_or_without_a_detail() {
    let m = parse_marker("node 6: Move Object").unwrap();
    assert_eq!((m.id, m.name.as_str(), m.detail.as_deref()), (6, "Move Object", None));

    let m = parse_marker("node 12: Get Axis [moveForward]").unwrap();
    assert_eq!((m.id, m.name.as_str(), m.detail.as_deref()), (12, "Get Axis", Some("moveForward")));

    assert!(parse_marker("an ordinary comment").is_none());
    assert!(parse_marker("node six: Move Object").is_none());
    assert!(parse_marker("node 6:").is_none());
}

#[test]
fn every_marker_in_the_behaviors_still_matches_its_sentence() {
    let mut total = 0;
    for entry in fs::read_dir(behaviors()).unwrap() {
        let path = entry.unwrap().path();
        if path.extension().is_none_or(|x| x != "prop") {
            continue;
        }
        let file = propulsion::parse(&fs::read_to_string(&path).unwrap()).unwrap();
        for node in analyze(&file).nodes {
            assert_eq!(node.status, NodeStatus::Matches, "{}: node {} {} (line {})", path.display(), node.id, node.name, node.line);
            total += 1;
        }
    }
    assert!(total >= 25, "expected the behaviors to carry many markers, found {total}");
}

#[test]
fn a_file_written_by_the_node_editor_has_a_marker_on_every_sentence() {
    let a = analyze_source(&read("door.prop"));
    assert!(a.custom.is_empty(), "door.prop should have no custom code: {:?}", a.custom);
    assert_eq!(a.nodes.len(), 14);
    assert!(a.nodes.iter().all(|n| n.status == NodeStatus::Matches));
}

#[test]
fn a_node_reads_its_pins_from_the_text() {
    let file = propulsion::parse(&read("door.prop")).unwrap();
    let a = analyze(&file);
    let by_name = |n: &str| a.nodes.iter().find(|r| r.name == n).unwrap_or_else(|| panic!("no node {n}"));

    let set_loc = by_name("Set Location");
    assert_eq!(set_loc.id, 14);
    assert_eq!(
        set_loc.pins,
        vec![
            ("Target".to_string(), "Transform.Loc.Z".to_string()),
            ("Value".to_string(), "Progress of the way from ClosedHeight to OpenHeight".to_string()),
        ]
    );

    let lerp = by_name("Lerp");
    assert_eq!(lerp.id, 13);
    assert_eq!(
        lerp.pins,
        vec![
            ("Alpha".to_string(), "Progress".to_string()),
            ("From".to_string(), "ClosedHeight".to_string()),
            ("To".to_string(), "OpenHeight".to_string()),
        ]
    );
    let changed = by_name("On Variable Changed");
    assert_eq!(changed.pins, vec![("Variable".to_string(), "IsOpen".to_string())]);
}

// ---- hand edits ----

fn analyze_source(src: &str) -> nodes::Analysis {
    analyze(&propulsion::parse(src).unwrap())
}

#[test]
fn rewriting_a_marked_sentence_as_a_different_kind_of_sentence_marks_the_node_modified() {
    let original = read("rotator.prop");
    let edited = original.replace("add SpinSpeed per second to Transform.Rot.Z", "print \"no longer spinning\"");
    assert_ne!(original, edited);

    let a = analyze_source(&edited);
    let rotate = a.nodes.iter().find(|n| n.name == "Rotate Object").unwrap();
    assert_eq!(rotate.status, NodeStatus::Modified { now: vec!["Print Debug Message".to_string()] });
    assert!(rotate.pins.is_empty(), "a modified node has no pins to hand to the graph");
}

#[test]
fn changing_what_a_sentence_says_but_not_what_node_it_is_does_not_count_as_a_modification() {
    let edited = read("rotator.prop").replace("Transform.Rot.Z", "Transform.Rot.X");
    let a = analyze_source(&edited);
    let rotate = a.nodes.iter().find(|n| n.name == "Rotate Object").unwrap();
    assert_eq!(rotate.status, NodeStatus::Matches);
    assert!(rotate.pins.contains(&("Target".to_string(), "Transform.Rot.X".to_string())), "{:?}", rotate.pins);
}

#[test]
fn removing_the_phrase_a_data_node_marks_it_modified() {
    // The "Get Axis" marker sits above a sentence that used to contain `axis moveForward of Input`.
    let original = read("player.prop");
    let edited = original.replace("remember Forward axis moveForward of Input", "remember Forward 1.0");
    assert_ne!(original, edited);
    let a = analyze_source(&edited);
    let axis = a.nodes.iter().find(|n| n.name == "Get Axis").unwrap(); // the first one is the moveForward read
    assert!(matches!(&axis.status, NodeStatus::Modified { now } if now == &vec!["Set Local".to_string()]), "{:?}", axis.status);
}

#[test]
fn a_sentence_without_a_marker_is_reported_as_custom_code_with_the_nodes_it_would_be() {
    let original = read("rotator.prop");
    let before = analyze_source(&original).custom.len();
    let edited = original.replace(
        "\t\tadd SpinSpeed per second to Transform.Rot.Z\n",
        "\t\tadd SpinSpeed per second to Transform.Rot.Z\n\t\tmake Transform.Scale 2.0 of the way from Transform.Scale to Transform.Scale\n",
    );
    let after = analyze_source(&edited);
    assert_eq!(after.custom.len(), before + 1);
    let added = after.custom.iter().find(|c| c.text.contains("Transform.Scale")).unwrap();
    assert_eq!(added.nodes, vec!["Set Scale".to_string(), "Lerp".to_string()]);
}

#[test]
fn unknown_and_duplicate_markers_are_reported() {
    let src = "\
header:
\tevent name:Placeholder

implement:
\t// node 1: On Tick
\tevery frame:
\t\t// node 2: Teleport Object
\t\tfinish
\t\t// node 2: Finish
\t\tfinish
";
    let a = analyze_source(src);
    let status = |id: u32, name: &str| a.nodes.iter().find(|n| n.id == id && n.name == name).unwrap().status.clone();
    assert_eq!(status(1, "On Tick"), NodeStatus::Matches);
    assert_eq!(status(2, "Teleport Object"), NodeStatus::UnknownNode);
    assert_eq!(status(2, "Finish"), NodeStatus::DuplicateId);
}

#[test]
fn statements_inside_blocks_are_checked_too() {
    let src = "\
header:
\tevent name:Placeholder

implement:
\tevery frame:
\t\t// node 1: Branch
\t\tif A is true:
\t\t\t// node 2: Finish
\t\t\tprint \"oops\"
";
    let a = analyze_source(src);
    let inner = a.nodes.iter().find(|n| n.id == 2).unwrap();
    assert!(matches!(inner.status, NodeStatus::Modified { .. }));
    assert!(a.nodes.iter().find(|n| n.id == 1).unwrap().status == NodeStatus::Matches);
}
