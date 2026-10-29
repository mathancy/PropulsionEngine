//! The C++ generator: what it writes, and (on Windows with Visual Studio) that the result
//! compiles against the engine's real headers and behaves like the interpreter.

use propulsion::codegen::{class_name, generate_all, Generated};
use propulsion::Project;
use std::fs;
use std::path::{Path, PathBuf};

fn root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..")
}

fn generate(entry: &Path) -> Vec<Generated> {
    let project = Project::load(entry).unwrap();
    let problems: Vec<String> = project
        .check(&propulsion::Manifest::builtin())
        .iter()
        .flat_map(|r| r.errors.iter().map(move |e| e.render(&r.source, &r.path)))
        .collect();
    assert!(problems.is_empty(), "{} has problems:\n{}", entry.display(), problems.join("\n"));
    generate_all(&project).unwrap_or_else(|e| panic!("{e}"))
}

fn find<'a>(all: &'a [Generated], class: &str) -> &'a Generated {
    all.iter().find(|g| g.class_name == class).unwrap_or_else(|| panic!("no {class}"))
}

fn propulsion_files(dir: PathBuf) -> Vec<PathBuf> {
    let mut files: Vec<PathBuf> = fs::read_dir(dir)
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|x| x == "prop"))
        .collect();
    files.sort();
    files
}

// ---- fast checks of the generated text ----

#[test]
fn class_names_come_from_the_file_name() {
    assert_eq!(class_name("behaviors/game_state.prop"), "FG_GameState");
    assert_eq!(class_name("examples/ui/hud.prop"), "FG_Hud");
    assert_eq!(class_name("player.prop"), "FG_Player");
}

#[test]
fn every_example_and_behavior_generates() {
    let mut count = 0;
    for dir in ["examples", "behaviors"] {
        for entry in propulsion_files(root().join(dir)) {
            let all = generate(&entry);
            for g in &all {
                assert!(g.header.contains("#pragma once") && g.header.contains(&format!("class {} : public ScriptBehavior", g.class_name)));
                assert!(g.source.contains(&format!("#include \"{}\"", g.header_name)));
            }
            count += 1;
        }
    }
    // 5 example files + 9 behaviors + 5 node kits (`ui/hud.prop` is generated as a dependency of the player).
    assert_eq!(count, 19, "expected every top-level example and behavior to generate");
}

#[test]
fn node_markers_become_the_engine_docs_node_comments() {
    let all = generate(&root().join("behaviors").join("door.prop"));
    let door = find(&all, "FG_Door");
    assert!(door.source.contains("// ─── Node: \"Lerp\" (ID: 13) ───"), "{}", door.source);
    assert!(door.source.contains("// ─── Node: \"On Overlap Begin\" (ID: 1) ───"));
    assert!(door.source.contains("self->getTransform()->position.z = Lerp(ClosedHeight, OpenHeight, Progress);"));
}

#[test]
fn writing_a_rotation_refreshes_the_transforms_quaternion() {
    let all = generate(&root().join("behaviors").join("rotator.prop"));
    let rot = find(&all, "FG_Rotator");
    assert!(rot.source.contains("self->getTransform()->rotation.z += (SpinSpeed * world->dt);"), "{}", rot.source);
    assert!(rot.source.contains("self->getTransform()->updateQuaternion();"), "{}", rot.source);
}

#[test]
fn exposed_variables_get_a_setter_and_nothing_else_does() {
    let all = generate(&root().join("behaviors").join("door.prop"));
    let door = find(&all, "FG_Door");
    assert!(door.source.contains("if (name == \"OpenHeight\") return AssignValue(OpenHeight, value);"));
    assert!(!door.source.contains("name == \"IsOpen\""), "IsOpen isn't exposed");
    assert!(door.header.contains("float OpenHeight = 3.0f;  // exposed"));
    assert!(door.header.contains("static constexpr float ClosedHeight = 0.0f;"));
}

#[test]
fn calls_on_another_script_check_that_it_exists_and_warn_when_it_does_not() {
    let all = generate(&root().join("examples").join("enemy.prop"));
    let enemy = find(&all, "FG_Enemy");
    assert!(enemy.source.contains("world->As<FG_Player>(Player())"), "{}", enemy.source);
    assert!(enemy.source.contains("`TakeDamage` was skipped because `Player` is missing"));
    assert!(enemy.header.contains("Object* Player() const { return world->FindByTag(\"player\"); }"));
}

#[test]
fn headers_do_not_include_each_other_so_scripts_can_need_each_other() {
    let all = generate(&root().join("behaviors").join("trap.prop"));
    for g in &all {
        assert!(!g.header.contains("generated.h\"\n\nnamespace") || g.header.matches("#include").count() == 1, "{} header includes: {}", g.class_name, g.header);
        assert_eq!(g.header.matches("#include").count(), 1, "{} header should only include the runtime", g.class_name);
    }
    let trap = find(&all, "FG_Trap");
    assert!(trap.source.contains("#include \"FG_Player.generated.h\"") && trap.source.contains("#include \"FG_Coin.generated.h\""));
}

#[test]
fn the_registry_finds_each_script_by_its_path_and_includes_every_source() {
    let all = generate(&root().join("behaviors").join("door.prop"));
    let registry = propulsion::codegen::generate_registry(&all);
    assert!(registry.contains("#include \"FG_Door.generated.cpp\""), "{registry}");
    assert!(registry.contains("world.Spawn<FG_Door>(at, overrides)"), "{registry}");
    // The path is what a scene stores, so it is the key, not the class name.
    assert!(registry.contains(".prop\", FG_Door::kScriptName"), "{registry}");
    let paths: Vec<&str> = registry.lines().filter(|l| l.contains("::kScriptName,")).collect();
    assert_eq!(paths.len(), all.len(), "one entry per generated file:\n{registry}");
}

fn write_project(dir: &str, files: &[(&str, &str)]) -> PathBuf {
    let base = std::env::temp_dir().join(format!("propulsion_codegen_{dir}_{}", std::process::id()));
    for (name, text) in files {
        let path = base.join(name);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, text).unwrap();
    }
    base
}

#[test]
fn names_that_are_c_plus_plus_keywords_or_runtime_names_are_renamed() {
    let base = write_project(
        "mangle",
        &[(
            "tricky.prop",
            "header:\n\tvariable name:new type:integer\n\tvariable name:Item type:integer\n\nimplement:\n\taction Clone:\n\t\tremember Timer 5\n\t\tadd Timer to new\n\t\tmake Item new\n",
        )],
    );
    let all = generate(&base.join("tricky.prop"));
    let g = find(&all, "FG_Tricky");
    assert!(g.header.contains("int new_ = 0;") && g.header.contains("int Item_ = 0;"), "{}", g.header);
    assert!(g.header.contains("void Clone_();"), "{}", g.header);
    assert!(g.source.contains("auto Timer_ = 5;") && g.source.contains("new_ += Timer_;"), "{}", g.source);
}

#[test]
fn two_files_that_would_share_a_class_name_are_refused() {
    let base = write_project(
        "clash",
        &[
            ("main.prop", "header:\n\tneeds file:\"a/thing.prop\" as A when:used\n\tneeds file:\"b/thing.prop\" as B when:used\n\nimplement:\n\twhen created:\n\t\tfinish\n"),
            ("a/thing.prop", "header:\n\tvariable name:X type:integer\n\nimplement:\n\twhen created:\n\t\tfinish\n"),
            ("b/thing.prop", "header:\n\tvariable name:Y type:integer\n\nimplement:\n\twhen created:\n\t\tfinish\n"),
        ],
    );
    let project = Project::load(&base.join("main.prop")).unwrap();
    let err = generate_all(&project).unwrap_err();
    assert!(err.contains("FG_Thing"), "{err}");
}

// ---- compile it and run it ----

#[cfg(windows)]
mod compiled {
    use super::*;
    use std::process::Command;

    fn find_vcvars() -> Option<PathBuf> {
        if let Ok(p) = std::env::var("VCVARS64") {
            return Some(PathBuf::from(p));
        }
        for base in ["C:\\Program Files\\Microsoft Visual Studio", "C:\\Program Files (x86)\\Microsoft Visual Studio"] {
            let Ok(versions) = fs::read_dir(base) else { continue };
            for version in versions.flatten() {
                let Ok(editions) = fs::read_dir(version.path()) else { continue };
                for edition in editions.flatten() {
                    let candidate = edition.path().join("VC").join("Auxiliary").join("Build").join("vcvars64.bat");
                    if candidate.exists() {
                        return Some(candidate);
                    }
                }
            }
        }
        None
    }

    fn engine_dir() -> Option<PathBuf> {
        // PropulsionScript lives inside the engine repo (`<engine>/PropulsionScript`); a separate checkout sits beside it.
        let default = [root().join(".."), root().join("..").join("PropulsionMKI")]
            .into_iter()
            .find(|d| d.join("include").join("core").join("Object.h").exists());
        let dir = std::env::var("PROPULSION_MK_I_DIR").map(PathBuf::from).ok().or(default)?;
        dir.join("include").join("core").join("Object.h").exists().then_some(dir)
    }

    /// Generate C++ for `entries`, compile it with `harness`, run it, and return what it printed.
    /// Returns `None` (a skip) when Visual Studio or the engine's headers aren't available.
    fn build_and_run(name: &str, folder: &str, entries: &[&str], harness: &str) -> Option<String> {
        let (Some(vcvars), Some(engine)) = (find_vcvars(), engine_dir()) else {
            eprintln!("skipping {name}: needs Visual Studio (or VCVARS64) and the PropulsionMKI headers (or PROPULSION_MK_I_DIR)");
            return None;
        };
        let out = std::env::temp_dir().join(format!("propulsion_cpp_{name}_{}", std::process::id()));
        let _ = fs::remove_dir_all(&out);
        fs::create_dir_all(&out).unwrap();

        for entry in entries {
            for g in generate(&root().join(folder).join(entry)) {
                fs::write(out.join(&g.header_name), &g.header).unwrap();
                fs::write(out.join(&g.source_name), &g.source).unwrap();
            }
        }
        fs::copy(root().join("cpp").join("PropulsionScriptRuntime.h"), out.join("PropulsionScriptRuntime.h")).unwrap();

        let tests = root().join("cpp").join("tests");
        let bat = format!(
            "@echo off\r\ncall \"{vcvars}\" >nul 2>&1\r\nif errorlevel 1 ( echo vcvars failed & exit /b 1 )\r\ncd /d \"{out}\"\r\n\
             cl /nologo /std:c++20 /EHsc /W3 /I \"{engine}\\include\" /I \"{engine}\\external\\glm\" /I . /I \"{tests}\" FG_*.cpp \"{tests}\\{harness}\" /Fe:run.exe\r\n\
             if errorlevel 1 exit /b 1\r\n\"{out}\\run.exe\"\r\n",
            vcvars = vcvars.display(),
            out = out.display(),
            engine = engine.display(),
            tests = tests.display(),
        );
        let bat_path = out.join("build.bat");
        fs::write(&bat_path, bat).unwrap();

        let output = Command::new("cmd").arg("/C").arg("call").arg(&bat_path).output().expect("cmd should run");
        Some(format!("{}{}", String::from_utf8_lossy(&output.stdout), String::from_utf8_lossy(&output.stderr)))
    }

    #[test]
    fn generated_behaviors_compile_against_the_engine_and_match_the_interpreter() {
        let entries = ["trap.prop", "game_state.prop", "door.prop", "health_pickup.prop", "rotator.prop", "moving_platform.prop", "drifter.prop"];
        if let Some(output) = build_and_run("behaviors", "behaviors", &entries, "differential.cpp") {
            assert!(output.contains("ALL PASSED"), "the generated C++ disagreed with the interpreter:\n{output}");
        }
    }

    #[test]
    fn generated_node_kits_compile_against_the_engine_and_match_the_interpreter() {
        let entries = ["math_kit.prop", "list_kit.prop", "flow_kit.prop", "world_kit.prop", "types_kit.prop"];
        if let Some(output) = build_and_run("kits", "behaviors", &entries, "differential_kits.cpp") {
            assert!(output.contains("ALL PASSED"), "the generated C++ disagreed with the interpreter:
{output}");
        }
    }

    #[test]
    fn generated_examples_compile_against_the_engine_and_match_the_interpreter() {
        let entries = ["spawner.prop", "enemy.prop", "switch.prop", "coin.prop"];
        if let Some(output) = build_and_run("examples", "examples", &entries, "differential_examples.cpp") {
            assert!(output.contains("ALL PASSED"), "the generated C++ disagreed with the interpreter:\n{output}");
        }
    }
}
