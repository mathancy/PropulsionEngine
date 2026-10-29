//! A game defines its own Input Actions. The engine writes them to an inputs file next to the scripts, and the tools that
//! run outside the engine read it, so a script is checked against the same names everywhere. (One test function, because
//! the names a check uses are process-wide.)

use propulsion::{inputs_near, parse_inputs, use_inputs_near, InputActions, Manifest, Project};
use std::fs;
use std::path::PathBuf;

const SCRIPT: &str = "// dasher.prop: uses input names only a game that defines them has.\n\
\n\
header:\n\
\tneeds component:Transform\n\
\tneeds system:Input\n\
\n\
implement:\n\
\tevery frame:\n\
\t\tremember Steer axis steer of Input\n\
\t\tadd Steer to Transform.Loc.X\n\
\n\
\twhen Input starts pressing dash:\n\
\t\tadd 3 to Transform.Loc.Y\n";

fn problems(script: &PathBuf) -> usize {
    let project = Project::load(script).unwrap();
    project.check(&Manifest::builtin()).iter().map(|r| r.errors.len()).sum()
}

#[test]
fn a_games_input_actions_come_from_its_inputs_file() {
    // The text of the file: button and axis lines, comments, and anything else ignored.
    let parsed = parse_inputs(
        "// Input Actions of this project\nbutton jump\nbutton dash // fast\naxis moveForward\naxis steer\n\nnonsense here\nbutton two words\n",
    );
    assert_eq!(parsed, InputActions { buttons: vec!["jump".into(), "dash".into()], axes: vec!["moveForward".into(), "steer".into()] });
    assert_eq!(parse_inputs(""), InputActions::default());

    let root = std::env::temp_dir().join(format!("propulsion_inputs_test_{}", std::process::id()));
    let _ = fs::remove_dir_all(&root);
    let scripts = root.join("Scripts");
    let nested = scripts.join("enemies").join("bosses");
    fs::create_dir_all(&nested).unwrap();
    let script = scripts.join("dasher.prop");
    let deep = nested.join("dasher.prop");
    fs::write(&script, SCRIPT).unwrap();
    fs::write(&deep, SCRIPT).unwrap();

    // With no inputs file, the built-in names apply, and this script's names aren't among them.
    assert!(inputs_near(&script).is_none());
    use_inputs_near(&script);
    assert!(problems(&script) > 0, "`steer` and `dash` aren't built in");

    // The engine writes the file in the Scripts folder.
    fs::write(scripts.join("inputs.propapi"), "// written by the engine\nbutton dash\naxis steer\n").unwrap();
    let found = inputs_near(&script).expect("found beside the script");
    assert_eq!(found.buttons, vec!["dash".to_string()]);
    assert_eq!(found.axes, vec!["steer".to_string()]);
    use_inputs_near(&script);
    assert_eq!(problems(&script), 0, "checked against the game's own names");

    // It is found from a script in a folder below it, and from the project folder above it.
    assert!(inputs_near(&deep).is_some());
    assert!(inputs_near(&root).is_some());
    use_inputs_near(&deep);
    assert_eq!(problems(&deep), 0);

    // The game's names replace the built-in ones: `jump` isn't an action of this game.
    let jumper = scripts.join("jumper.prop");
    fs::write(&jumper, "header:\n\tneeds component:Transform\n\tneeds system:Input\n\nimplement:\n\twhen Input starts pressing jump:\n\t\tadd 1 to Transform.Loc.Z\n").unwrap();
    use_inputs_near(&jumper);
    assert!(problems(&jumper) > 0, "the game didn't define `jump`");

    // A project with no inputs file goes back to the built-in names.
    let elsewhere = std::env::temp_dir().join(format!("propulsion_inputs_test_other_{}", std::process::id()));
    let _ = fs::remove_dir_all(&elsewhere);
    fs::create_dir_all(&elsewhere).unwrap();
    let plain = elsewhere.join("jumper.prop");
    fs::write(&plain, "header:\n\tneeds component:Transform\n\tneeds system:Input\n\nimplement:\n\twhen Input starts pressing jump:\n\t\tadd 1 to Transform.Loc.Z\n").unwrap();
    use_inputs_near(&plain);
    assert_eq!(problems(&plain), 0, "built-in names again");

    let _ = fs::remove_dir_all(&root);
    let _ = fs::remove_dir_all(&elsewhere);
    propulsion::set_project_inputs(None);
}
