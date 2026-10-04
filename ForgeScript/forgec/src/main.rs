use std::process::ExitCode;

const USAGE: &str = "usage: forgec <file.forge>... [--ast | --tokens]
       forgec run <file.forge> [--frames N] [--press <input>]...
       forgec fmt <file.forge>... [--check | --write]
       forgec nodes <file.forge> [--json]
       forgec nodes --table [--json]
       forgec cpp <file.forge>... --out <dir> [--no-runtime]";

/// The runtime that generated code stands on, so an output folder is self-contained.
const RUNTIME_HEADER: &str = include_str!("../../cpp/ForgeScriptRuntime.h");

/// `forgec cpp`: check each file and everything it needs, then write C++ for all of it, plus a
/// registry that finds each script by its path.
fn cpp(args: &[String]) -> ExitCode {
    let mut paths: Vec<String> = Vec::new();
    let mut out = None;
    let mut runtime = true;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--out" => {
                i += 1;
                out = args.get(i).cloned();
            }
            "--no-runtime" => runtime = false,
            other => paths.push(other.to_string()),
        }
        i += 1;
    }
    let Some(out) = out else {
        eprintln!("{USAGE}");
        return ExitCode::from(2);
    };
    if paths.is_empty() {
        eprintln!("{USAGE}");
        return ExitCode::from(2);
    }

    // Each entry is loaded with what it needs. A file that several entries need is generated once.
    let mut generated: Vec<forge::codegen::Generated> = Vec::new();
    let mut problems = 0;
    for path in &paths {
        let project = match forge::Project::load(std::path::Path::new(path)) {
            Ok(p) => p,
            Err(e) => {
                eprintln!("error: {e}");
                return ExitCode::FAILURE;
            }
        };
        for report in project.check(&forge::Manifest::builtin()) {
            for e in &report.errors {
                eprint!("{}", e.render(&report.source, &report.path));
                problems += 1;
            }
        }
        if problems > 0 {
            continue;
        }
        match forge::codegen::generate_all(&project) {
            Ok(all) => {
                for g in all {
                    if !generated.iter().any(|seen| seen.key == g.key) {
                        generated.push(g);
                    }
                }
            }
            Err(e) => {
                eprintln!("error: {e}");
                return ExitCode::FAILURE;
            }
        }
    }
    if problems > 0 {
        eprintln!("can't generate C++: fix the {problems} problem(s) above first");
        return ExitCode::FAILURE;
    }
    for (n, a) in generated.iter().enumerate() {
        if let Some(b) = generated[n + 1..].iter().find(|b| b.class_name == a.class_name) {
            eprintln!("error: `{}` and `{}` would both become the C++ class `{}`; rename one of them", a.key, b.key, a.class_name);
            return ExitCode::FAILURE;
        }
    }

    if let Err(e) = std::fs::create_dir_all(&out) {
        eprintln!("error: can't create {out}: {e}");
        return ExitCode::FAILURE;
    }
    let dir = std::path::Path::new(&out);
    let registry = forge::codegen::generate_registry(&generated);
    let mut files: Vec<(String, &str)> = Vec::new();
    for g in &generated {
        files.push((g.header_name.clone(), g.header.as_str()));
        files.push((g.source_name.clone(), g.source.as_str()));
    }
    files.push(("ForgeScriptRegistry.generated.cpp".to_string(), registry.as_str()));
    if runtime {
        files.push(("ForgeScriptRuntime.h".to_string(), RUNTIME_HEADER));
    }
    for (name, text) in files {
        let target = dir.join(&name);
        // Leave an identical file alone so the build doesn't recompile it.
        if std::fs::read_to_string(&target).is_ok_and(|old| old == text) {
            continue;
        }
        if let Err(e) = std::fs::write(&target, text) {
            eprintln!("error: can't write {name}: {e}");
            return ExitCode::FAILURE;
        }
        println!("wrote {}", target.display());
    }
    ExitCode::SUCCESS
}

/// Escape text for a JSON string.
fn js(s: &str) -> String {
    let mut out = String::from("\"");
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

/// `forgec fmt`: print, check, or rewrite files in the canonical style.
fn fmt(args: &[String]) -> ExitCode {
    let check = args.iter().any(|a| a == "--check");
    let write = args.iter().any(|a| a == "--write");
    let paths: Vec<&String> = args.iter().filter(|a| !a.starts_with("--")).collect();
    if paths.is_empty() || (!check && !write && paths.len() > 1) {
        eprintln!("{USAGE}");
        return ExitCode::from(2);
    }

    let mut failed = false;
    for path in paths {
        let source = match std::fs::read_to_string(path) {
            Ok(s) => s,
            Err(e) => {
                eprintln!("error: can't read {path}: {e}");
                failed = true;
                continue;
            }
        };
        let formatted = match forge::format_source(&source) {
            Ok(f) => f,
            Err(e) => {
                eprint!("{}", e.render(&source, path));
                failed = true;
                continue;
            }
        };
        let unchanged = source.replace("\r\n", "\n") == formatted;
        if check {
            if !unchanged {
                println!("needs formatting: {path}");
                failed = true;
            }
        } else if write {
            if unchanged {
                println!("already formatted: {path}");
            } else if let Err(e) = std::fs::write(path, &formatted) {
                eprintln!("error: can't write {path}: {e}");
                failed = true;
            } else {
                println!("formatted: {path}");
            }
        } else {
            print!("{formatted}");
        }
    }
    if failed {
        ExitCode::FAILURE
    } else {
        ExitCode::SUCCESS
    }
}

/// `forgec nodes`: show the node table, or compare a file's node markers with its text.
fn nodes(args: &[String]) -> ExitCode {
    use forge::nodes::{analyze, NodeStatus, NODES};

    let json = args.iter().any(|a| a == "--json");

    if args.iter().any(|a| a == "--table") {
        if json {
            let items: Vec<String> = NODES
                .iter()
                .map(|n| {
                    let pins: Vec<String> = n.pins.iter().map(|p| js(p)).collect();
                    format!(
                        "{{\"name\":{},\"class\":{},\"category\":{},\"pattern\":{},\"pins\":[{}]}}",
                        js(n.name),
                        js(&format!("{:?}", n.class).to_lowercase()),
                        js(n.category),
                        js(n.pattern),
                        pins.join(",")
                    )
                })
                .collect();
            println!("[{}]", items.join(","));
        } else {
            let mut last = "";
            for n in NODES {
                if n.category != last {
                    println!("\n{}", n.category);
                    last = n.category;
                }
                println!("  {:<22} {}", n.name, n.pattern);
                if !n.pins.is_empty() {
                    println!("  {:<22}   pins: {}", "", n.pins.join(", "));
                }
            }
        }
        return ExitCode::SUCCESS;
    }

    let Some(path) = args.iter().find(|a| !a.starts_with("--")) else {
        eprintln!("{USAGE}");
        return ExitCode::from(2);
    };
    let source = match std::fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("error: can't read {path}: {e}");
            return ExitCode::FAILURE;
        }
    };
    let file = match forge::parse(&source) {
        Ok(f) => f,
        Err(e) => {
            eprint!("{}", e.render(&source, path));
            return ExitCode::FAILURE;
        }
    };
    let a = analyze(&file);
    let problems = a.nodes.iter().filter(|n| n.status != NodeStatus::Matches).count();

    if json {
        let status_text = |s: &NodeStatus| match s {
            NodeStatus::Matches => "matches".to_string(),
            NodeStatus::Modified { .. } => "modified".to_string(),
            NodeStatus::UnknownNode => "unknown".to_string(),
            NodeStatus::DuplicateId => "duplicate".to_string(),
        };
        let nodes: Vec<String> = a
            .nodes
            .iter()
            .map(|n| {
                let pins: Vec<String> = n.pins.iter().map(|(k, v)| format!("{}:{}", js(k), js(v))).collect();
                let now = match &n.status {
                    NodeStatus::Modified { now } => format!(",\"now\":[{}]", now.iter().map(|s| js(s)).collect::<Vec<_>>().join(",")),
                    _ => String::new(),
                };
                format!(
                    "{{\"id\":{},\"name\":{},\"line\":{},\"status\":{}{now},\"pins\":{{{}}}}}",
                    n.id,
                    js(&n.name),
                    n.line,
                    js(&status_text(&n.status)),
                    pins.join(",")
                )
            })
            .collect();
        let custom: Vec<String> = a
            .custom
            .iter()
            .map(|c| {
                format!(
                    "{{\"line\":{},\"text\":{},\"nodes\":[{}]}}",
                    c.line,
                    js(&c.text),
                    c.nodes.iter().map(|s| js(s)).collect::<Vec<_>>().join(",")
                )
            })
            .collect();
        println!("{{\"file\":{},\"nodes\":[{}],\"custom\":[{}]}}", js(path), nodes.join(","), custom.join(","));
    } else {
        println!("{path}: {} node(s), {problems} not matching, {} sentence(s) with no marker", a.nodes.len(), a.custom.len());
        for n in &a.nodes {
            let status = match &n.status {
                NodeStatus::Matches => "ok".to_string(),
                NodeStatus::Modified { now } => format!("MODIFIED by hand; the sentence now has: {}", now.join(", ")),
                NodeStatus::UnknownNode => "UNKNOWN node name".to_string(),
                NodeStatus::DuplicateId => "DUPLICATE id".to_string(),
            };
            println!("  #{:<3} {:<22} line {:<4} {status}", n.id, n.name, n.line);
        }
        if !a.custom.is_empty() {
            println!("custom code (no marker):");
            for c in &a.custom {
                println!("  line {:<4} {}   [{}]", c.line, c.text, c.nodes.join(", "));
            }
        }
    }
    if problems > 0 {
        ExitCode::FAILURE
    } else {
        ExitCode::SUCCESS
    }
}

/// `forgec run`: spawn the file as one entity in a world with a stub engine and step it.
fn run(args: &[String]) -> ExitCode {
    use forge::runtime::{StubHost, World};

    let mut path = None;
    let mut frames = 60usize;
    let mut presses = Vec::new();
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--frames" => {
                i += 1;
                match args.get(i).and_then(|s| s.parse().ok()) {
                    Some(n) => frames = n,
                    None => {
                        eprintln!("error: --frames needs a number\n{USAGE}");
                        return ExitCode::from(2);
                    }
                }
            }
            "--press" => {
                i += 1;
                match args.get(i) {
                    Some(name) => presses.push(name.clone()),
                    None => {
                        eprintln!("error: --press needs an input name\n{USAGE}");
                        return ExitCode::from(2);
                    }
                }
            }
            other => path = Some(other.to_string()),
        }
        i += 1;
    }
    let Some(path) = path else {
        eprintln!("{USAGE}");
        return ExitCode::from(2);
    };

    let project = match forge::Project::load(std::path::Path::new(&path)) {
        Ok(p) => p,
        Err(e) => {
            eprintln!("error: {e}");
            return ExitCode::FAILURE;
        }
    };
    let mut world = match World::new(project, StubHost::default()) {
        Ok(w) => w,
        Err(reports) => {
            for r in &reports {
                for e in &r.errors {
                    eprint!("{}", e.render(&r.source, &r.path));
                }
            }
            eprintln!("can't run {path}: fix the problems above first");
            return ExitCode::FAILURE;
        }
    };

    for input in &presses {
        world.host.press(input);
    }
    world.spawn_entry([0.0; 3]);
    for _ in 0..frames {
        world.step(1.0 / 60.0);
    }

    println!("ran {frames} frames ({:.2} seconds)", world.time);
    println!("\nsystem calls ({}):", world.host.calls.len());
    for c in &world.host.calls {
        println!("  {c}");
    }
    println!("\nentities:");
    for e in world.entities.iter().filter(|e| e.alive) {
        let mut vars: Vec<_> = e.vars.iter().collect();
        vars.sort_by(|a, b| a.0.cmp(b.0));
        let vars: Vec<String> = vars.iter().map(|(k, v)| format!("{k}={v}")).collect();
        println!(
            "  #{} {} at ({:.2}, {:.2}, {:.2})\n      {}",
            e.id,
            e.file,
            e.loc[0],
            e.loc[1],
            e.loc[2],
            vars.join(", ")
        );
    }
    if !world.warnings.is_empty() {
        println!("\nwarnings (skipped, see SPEC 4.6):");
        for w in &world.warnings {
            println!("  {w}");
        }
    }
    ExitCode::SUCCESS
}

fn main() -> ExitCode {
    let all: Vec<String> = std::env::args().skip(1).collect();
    match all.first().map(String::as_str) {
        Some("run") => return run(&all[1..]),
        Some("fmt") => return fmt(&all[1..]),
        Some("nodes") => return nodes(&all[1..]),
        Some("cpp") => return cpp(&all[1..]),
        _ => {}
    }

    let mut paths = Vec::new();
    let mut show_ast = false;
    let mut show_tokens = false;
    for arg in std::env::args().skip(1) {
        match arg.as_str() {
            "--ast" => show_ast = true,
            "--tokens" => show_tokens = true,
            "-h" | "--help" => {
                println!("{USAGE}");
                return ExitCode::SUCCESS;
            }
            _ => paths.push(arg),
        }
    }
    if paths.is_empty() {
        eprintln!("{USAGE}");
        return ExitCode::from(2);
    }

    let mut failed = false;
    for path in &paths {
        let source = match std::fs::read_to_string(path) {
            Ok(s) => s,
            Err(e) => {
                eprintln!("error: can't read {path}: {e}");
                failed = true;
                continue;
            }
        };

        if show_tokens {
            match forge::lexer::lex(&source) {
                Ok(tokens) => {
                    for t in tokens {
                        println!("{}:{}\t{:?}", t.line, t.col, t.tok);
                    }
                }
                Err(e) => {
                    eprint!("{}", e.render(&source, path));
                    failed = true;
                }
            }
            continue;
        }

        if show_ast {
            match forge::parse(&source) {
                Ok(file) => println!("{file:#?}"),
                Err(e) => {
                    eprint!("{}", e.render(&source, path));
                    failed = true;
                }
            }
            continue;
        }

        // Check the file and every file it needs: syntax, names, and types.
        match forge::check_path(std::path::Path::new(path)) {
            Ok(reports) => {
                let mut problems = 0;
                for report in &reports {
                    for e in &report.errors {
                        eprint!("{}", e.render(&report.source, &report.path));
                        problems += 1;
                    }
                }
                if problems == 0 {
                    println!("ok: {path} ({} file(s) checked)", reports.len());
                } else {
                    eprintln!("{problems} problem(s) found while checking {path}");
                    failed = true;
                }
            }
            Err(e) => {
                eprintln!("error: {e}");
                failed = true;
            }
        }
    }

    if failed {
        ExitCode::FAILURE
    } else {
        ExitCode::SUCCESS
    }
}
