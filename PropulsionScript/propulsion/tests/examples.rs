use propulsion::ast::*;
use propulsion::Project;
use std::fs;
use std::path::{Path, PathBuf};

fn workspace() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..")
}

fn propulsion_files(dir: PathBuf) -> Vec<PathBuf> {
    let mut files = Vec::new();
    let mut stack = vec![dir];
    while let Some(d) = stack.pop() {
        for entry in fs::read_dir(&d).unwrap_or_else(|e| panic!("can't read {}: {e}", d.display())) {
            let p = entry.unwrap().path();
            if p.is_dir() {
                stack.push(p);
            } else if p.extension().is_some_and(|x| x == "prop") {
                files.push(p);
            }
        }
    }
    files.sort();
    files
}

/// Load a file and everything it needs, and return every error message found anywhere.
fn check_project(path: &Path) -> Vec<String> {
    let project = Project::load(path).unwrap_or_else(|e| panic!("{e}"));
    project
        .check(&propulsion::Manifest::builtin())
        .iter()
        .flat_map(|r| r.errors.iter().map(move |e| e.render(&r.source, &r.path)))
        .collect()
}

#[test]
fn every_example_parses_and_type_checks() {
    let files = propulsion_files(workspace().join("examples"));
    assert!(files.len() >= 6, "expected the example files, found {}", files.len());
    for path in files {
        let problems = check_project(&path);
        assert!(problems.is_empty(), "{} should be valid but has problems:\n{}", path.display(), problems.join("\n"));
    }
}

/// Each file in tests/fail starts with `// expect: <text>`; checking it must report an error
/// whose message contains that text.
#[test]
fn invalid_files_are_rejected_with_a_helpful_message() {
    let files = propulsion_files(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests").join("fail"));
    assert!(files.len() >= 20, "expected the failing fixtures, found {}", files.len());
    for path in files {
        let src = fs::read_to_string(&path).unwrap();
        let expect = src
            .lines()
            .next()
            .and_then(|l| l.strip_prefix("// expect:"))
            .unwrap_or_else(|| panic!("{} must start with `// expect: ...`", path.display()))
            .trim()
            .to_lowercase();
        let problems: Vec<String> = check_project(&path).iter().map(|p| p.to_lowercase()).collect();
        assert!(!problems.is_empty(), "{} should have been rejected", path.display());
        assert!(
            problems.iter().any(|m| m.contains(&expect)),
            "{}: expected an error containing {expect:?}, got:\n{}",
            path.display(),
            problems.join("\n")
        );
    }
}

#[test]
fn switch_example_has_the_expected_shape() {
    let src = fs::read_to_string(workspace().join("examples").join("switch.prop")).unwrap();
    let file = propulsion::parse(&src).unwrap();
    assert_eq!(file.header.len(), 1);
    assert!(matches!(&file.header[0].kind, DeclKind::Variable { name, ty: TypeExpr::Boolean, .. } if name == "Switch"));
    assert_eq!(file.implement.len(), 1);
    let ItemKind::Handler { trigger: Trigger::EveryFrame, body } = &file.implement[0].kind else {
        panic!("expected an `every frame` handler")
    };
    let StmtKind::If { branches, otherwise: None, .. } = &body[0].kind else { panic!("expected an `if`") };
    assert_eq!(branches.len(), 1);
    assert!(matches!(
        &branches[0].1[0].kind,
        StmtKind::Make { target: Term::Name(t), value: Value::Term(Term::Bool(false)), .. } if t == "Switch"
    ));
}

#[test]
fn per_second_and_field_paths_parse() {
    let src = "header:\n\tneeds component:Transform\n\tconstant name:Speed type:decimal value:5.0\n\nimplement:\n\tevery frame:\n\t\tadd Speed per second to Transform.Loc.Z\n\t\tmake Target Transform.Loc of Player\n";
    let file = propulsion::parse(&src).unwrap();
    let ItemKind::Handler { body, .. } = &file.implement[0].kind else { panic!("expected a handler") };
    assert!(matches!(
        &body[0].kind,
        StmtKind::Add { value: Value::PerSecond(Term::Name(n)), target: Term::Path { segments, owner: None }, .. }
            if n == "Speed" && segments == &["Transform", "Loc", "Z"]
    ));
    assert!(matches!(
        &body[1].kind,
        StmtKind::Make { value: Value::Term(Term::Path { owner: Some(o), .. }), .. } if o == "Player"
    ));
}

#[test]
fn decided_syntax_parses() {
    let src = "header:\n\tneeds system:Input\n\tneeds entity:Player file:\"player.prop\" tag:\"player\"\n\tneeds entities:Foes tag:\"enemy\"\n\tvariable name:Elapsed type:decimal\n\nimplement:\n\tevery frame:\n\t\tif Player is missing, then finish\n\t\tif Input starts pressing jump or Input stops pressing crouch, then make Elapsed time since start\n\t\tif Input is not pressing jump, then finish\n";
    let file = propulsion::parse_and_check(src).unwrap();
    assert!(matches!(&file.header[2].kind, DeclKind::NeedsEntities { name, file: None, .. } if name == "Foes"));
    let ItemKind::Handler { body, .. } = &file.implement[0].kind else { panic!("expected a handler") };
    let StmtKind::If { branches, .. } = &body[0].kind else { panic!("expected an if") };
    assert!(matches!(&branches[0].0.first, Comparison::Presence { missing: true, .. }));
    let StmtKind::If { branches, .. } = &body[1].kind else { panic!("expected an if") };
    let cond = &branches[0].0;
    assert!(matches!(&cond.first, Comparison::Pressing { kind: PressKind::Started, .. }));
    assert!(matches!(&cond.rest[0], Comparison::Pressing { kind: PressKind::Stopped, .. }));
    assert_eq!(cond.joiner, Some(Joiner::Or));
    assert!(matches!(
        &branches[0].1[0].kind,
        StmtKind::Make { value: Value::TimeSinceStart, .. }
    ));
}

#[test]
fn name_that_looks_like_a_phrase_is_still_a_name() {
    // `count` and `text` are ordinary parameter names; they are only phrases when followed by `of`.
    let src = "header:\n\tvariable name:CoinCount type:integer\n\nimplement:\n\taction ShowCoins count:integer:\n\t\tmake CoinCount count\n";
    propulsion::parse(src).unwrap();
}

#[test]
fn whole_number_literals_work_as_decimals_but_variables_do_not() {
    let ok = "header:\n\tvariable name:Speed type:decimal value:5\n\nimplement:\n\tevery frame:\n\t\tmake Speed 2\n";
    let dir = std::env::temp_dir().join("propulsion_literal_test");
    fs::create_dir_all(&dir).unwrap();
    let path = dir.join("ok.prop");
    fs::write(&path, ok).unwrap();
    assert!(check_project(&path).is_empty());

    let bad = "header:\n\tvariable name:Speed type:decimal\n\tvariable name:Count type:integer\n\nimplement:\n\tevery frame:\n\t\tmake Speed Count\n";
    let path = dir.join("bad.prop");
    fs::write(&path, bad).unwrap();
    let problems = check_project(&path).join("\n");
    assert!(problems.contains("decimal of"), "{problems}");
}
