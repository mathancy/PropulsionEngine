//! The C interface, called the way the engine calls it.

use propulsion_ffi::*;
use std::ffi::{c_char, c_void, CStr, CString};
use std::path::PathBuf;
use std::ptr;

#[derive(Default)]
struct Recorded {
    held: Vec<String>,
    started: Vec<String>,
    printed: Vec<String>,
    calls: Vec<String>,
    ended: usize,
    traced_by: Vec<i64>,
    /// Who asked for an input, in order.
    asked_input: Vec<i64>,
    /// `moveForward` reads 1 only for this entity (-1: for everyone).
    forward_for: i64,
    /// What a trace's `entity` says (-1: nothing with a script).
    traces_hit: i64,
}

fn s(p: *const c_char) -> String {
    unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
}

extern "C" fn pressing(user: *mut c_void, entity: i64, _system: *const c_char, input: *const c_char, kind: i32) -> bool {
    let r = unsafe { &*(user as *const Recorded) };
    let input = s(input);
    if input == "jump" && kind == 2 && r.forward_for != -1 {
        return entity == r.forward_for; // only that entity's own mapping has jump going down
    }
    if input == "crouch" && r.forward_for != -1 {
        return (kind == 0) == (entity == r.forward_for); // and only its crouch is held
    }
    match kind {
        0 => r.held.contains(&input),
        1 => !r.held.contains(&input),
        2 => r.started.contains(&input),
        _ => false,
    }
}
extern "C" fn axis(user: *mut c_void, entity: i64, _system: *const c_char, input: *const c_char) -> f32 {
    let r = unsafe { &mut *(user as *mut Recorded) };
    r.asked_input.push(entity);
    if s(input) == "moveForward" && (r.forward_for == -1 || r.forward_for == entity) {
        1.0
    } else {
        0.0
    }
}
extern "C" fn call(user: *mut c_void, system: *const c_char, action: *const c_char, args: *const c_char) {
    let r = unsafe { &mut *(user as *mut Recorded) };
    r.calls.push(format!("{}.{} {}", s(system), s(action), s(args)));
}
extern "C" fn print(user: *mut c_void, text: *const c_char) {
    let r = unsafe { &mut *(user as *mut Recorded) };
    r.printed.push(s(text));
}
extern "C" fn level_name(_user: *mut c_void) -> *const c_char {
    c"forest".as_ptr()
}
extern "C" fn end_frame(user: *mut c_void) {
    let r = unsafe { &mut *(user as *mut Recorded) };
    r.ended += 1;
    r.started.clear();
}

/// An engine with a floor at z = -3 that has no script.
extern "C" fn trace(user: *mut c_void, me: i64, from: *const f32, to: *const f32, _radius: f32, out: *mut PropulsionTraceHit) -> bool {
    let r = unsafe { &mut *(user as *mut Recorded) };
    r.traced_by.push(me);
    let (from, to) = unsafe { (std::slice::from_raw_parts(from, 3), std::slice::from_raw_parts(to, 3)) };
    let length = ((to[0] - from[0]).powi(2) + (to[1] - from[1]).powi(2) + (to[2] - from[2]).powi(2)).sqrt();
    let hit = unsafe { &mut *out };
    if from[2] > -3.0 && to[2] <= -3.0 {
        let distance = (from[2] + 3.0) / (from[2] - to[2]) * length;
        *hit = PropulsionTraceHit { blocked: true, entity: r.traces_hit, location: [from[0], from[1], -3.0], normal: [0.0, 0.0, 1.0], distance };
    } else {
        *hit = PropulsionTraceHit { blocked: false, entity: -1, location: [to[0], to[1], to[2]], normal: [0.0; 3], distance: length };
    }
    true
}

fn host(rec: &mut Recorded) -> PropulsionHost {
    PropulsionHost {
        user: rec as *mut Recorded as *mut c_void,
        pressing: Some(pressing),
        axis: Some(axis),
        call: Some(call),
        print: Some(print),
        end_frame: Some(end_frame),
        level_name: Some(level_name),
        trace: Some(trace),
    }
}

fn script(folder: &str, name: &str) -> CString {
    let p: PathBuf = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..").join(folder).join(name);
    CString::new(p.to_string_lossy().as_bytes()).unwrap()
}

fn behaviors(name: &str) -> CString {
    script("behaviors", name)
}


fn open(rec: &mut Recorded, paths: &[&CString]) -> *mut PropulsionSession {
    let ptrs: Vec<*const c_char> = paths.iter().map(|p| p.as_ptr()).collect();
    let mut errors: *mut c_char = ptr::null_mut();
    let session = unsafe { propulsion_session_new(host(rec), ptrs.as_ptr(), ptrs.len(), &mut errors) };
    assert!(!session.is_null(), "{}", if errors.is_null() { String::new() } else { s(errors) });
    session
}

#[test]
fn a_trace_is_answered_by_the_engine() {
    let mut rec = Recorded { traces_hit: -1, forward_for: -1, ..Default::default() };
    let tracer = script("propulsion-ffi/tests", "tracer.prop");
    let session = open(&mut rec, &[&tracer]);
    let id = unsafe { propulsion_spawn(session, tracer.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    assert_eq!(id, 0);
    assert_eq!(rec.traced_by, vec![0], "the engine is told which entity is looking");
    // The floor the engine knows about has no script, so it blocks the line but there is nobody to name.
    assert_eq!(rec.printed, vec!["blocked true", "far 4", "nobody"]);
    unsafe { propulsion_session_free(session) };

    // When the engine says it hit a scripted entity (here, entity 0 itself, which is only a number to it), the script has someone.
    let mut rec = Recorded { traces_hit: 0, forward_for: -1, ..Default::default() };
    let session = open(&mut rec, &[&tracer]);
    unsafe { propulsion_spawn(session, tracer.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    assert_eq!(rec.printed, vec!["blocked true", "far 4", "somebody"]);
    unsafe { propulsion_session_free(session) };
}

#[test]
fn an_input_read_says_which_entity_is_asking() {
    // Two walkers in one world; the engine says only entity 1 is holding forward (its own input mapping, say).
    let mut rec = Recorded { forward_for: 1, ..Default::default() };
    let walker = script("propulsion-ffi/tests", "walker.prop");
    let session = open(&mut rec, &[&walker]);
    let first = unsafe { propulsion_spawn(session, walker.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    let second = unsafe { propulsion_spawn(session, walker.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    assert_eq!((first, second), (0, 1));
    for _ in 0..4 {
        unsafe { propulsion_step(session, 0.25) };
    }
    let (mut a, mut b) = ([0f32; 3], [0f32; 3]);
    assert!(unsafe { propulsion_get_transform(session, first, a.as_mut_ptr(), ptr::null_mut(), ptr::null_mut()) });
    assert!(unsafe { propulsion_get_transform(session, second, b.as_mut_ptr(), ptr::null_mut(), ptr::null_mut()) });
    assert!(a[1].abs() < 0.001, "entity 0 holds nothing, got {a:?}");
    assert!((b[1] - 5.0).abs() < 0.01, "entity 1 holds forward at 5 a second for a second, got {b:?}");
    assert!(a[2].abs() < 0.001 && (b[2] - 40.0).abs() < 0.01, "`starts pressing jump` is asked per entity too: {a:?} {b:?}");
    assert!(a[0].abs() < 0.001 && (b[0] - 4.0).abs() < 0.01, "and so is `if Input is pressing crouch`: {a:?} {b:?}");
    assert!(rec.asked_input.contains(&0) && rec.asked_input.contains(&1), "each read names who asked: {:?}", rec.asked_input);
    unsafe { propulsion_session_free(session) };
}

#[test]
fn scripts_are_checked_against_the_inputs_the_game_defines() {
    let dasher = script("propulsion-ffi/tests", "dasher.prop");
    let check = |expect_ok: bool| {
        let mut errors: *mut c_char = ptr::null_mut();
        let ok = unsafe { propulsion_check_file(dasher.as_ptr(), &mut errors) };
        let message = if errors.is_null() { String::new() } else { s(errors) };
        assert_eq!(ok, expect_ok, "{message}");
        message
    };
    // Built in: no `dash` or `steer`.
    let message = check(false);
    assert!(message.contains("dash") || message.contains("steer"), "{message}");

    // The game defines them (and keeps the built-in names, since other tests share this process).
    let buttons = CString::new("moveForward
moveBack
moveLeft
moveRight
jump
crouch
interact
dash
").unwrap();
    let axes = CString::new("moveForward
moveRight
lookX
lookY
steer").unwrap();
    unsafe { propulsion_set_input_actions(buttons.as_ptr(), axes.as_ptr()) };
    check(true);

    // Back to the built-in names.
    unsafe { propulsion_set_input_actions(ptr::null(), ptr::null()) };
    check(false);
}

#[test]
fn a_rotator_spins_and_its_transform_comes_back_out() {
    let mut rec = Recorded { forward_for: -1, ..Default::default() };
    let rotator = behaviors("rotator.prop");
    let session = open(&mut rec, &[&rotator]);

    let loc = [1.0f32, 2.0, 3.0];
    let id = unsafe { propulsion_spawn(session, rotator.as_ptr(), loc.as_ptr(), ptr::null(), ptr::null()) };
    assert_eq!(id, 0);

    for _ in 0..8 {
        unsafe { propulsion_step(session, 0.25) };
    }
    let (mut l, mut r, mut sc) = ([0f32; 3], [0f32; 3], [0f32; 3]);
    assert!(unsafe { propulsion_get_transform(session, id, l.as_mut_ptr(), r.as_mut_ptr(), sc.as_mut_ptr()) });
    assert_eq!(l, loc, "position is kept");
    assert!((r[2] - 90.0).abs() < 0.01, "45 degrees a second for 2 seconds, got {r:?}");
    assert_eq!(sc, [1.0; 3]);
    assert_eq!(rec.ended, 8, "end_frame is called once per step");
    assert_eq!(rec.printed, vec!["Rotation is 90".to_string()], "the 2-second print fired once");
    unsafe { propulsion_session_free(session) };
}

#[test]
fn an_object_placed_with_a_rotation_starts_from_it() {
    let mut rec = Recorded { forward_for: -1, ..Default::default() };
    let rotator = behaviors("rotator.prop");
    let session = open(&mut rec, &[&rotator]);
    let rot = [0.0f32, 0.0, 30.0];
    let scale = [2.0f32, 2.0, 2.0];
    let id = unsafe { propulsion_spawn(session, rotator.as_ptr(), ptr::null(), rot.as_ptr(), scale.as_ptr()) };
    unsafe { propulsion_step(session, 1.0) };
    let (mut r, mut sc) = ([0f32; 3], [0f32; 3]);
    assert!(unsafe { propulsion_get_transform(session, id, ptr::null_mut(), r.as_mut_ptr(), sc.as_mut_ptr()) });
    assert!((r[2] - 75.0).abs() < 0.01, "30 + 45, got {r:?}");
    assert_eq!(sc, scale);
    unsafe { propulsion_session_free(session) };
}

#[test]
fn the_engine_can_move_an_entity_between_steps() {
    let mut rec = Recorded { forward_for: -1, ..Default::default() };
    let rotator = behaviors("rotator.prop");
    let session = open(&mut rec, &[&rotator]);
    let id = unsafe { propulsion_spawn(session, rotator.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    let moved = [5.0f32, 0.0, 0.0];
    assert!(unsafe { propulsion_set_transform(session, id, moved.as_ptr(), ptr::null(), ptr::null()) });
    unsafe { propulsion_step(session, 0.25) };
    let mut l = [0f32; 3];
    assert!(unsafe { propulsion_get_transform(session, id, l.as_mut_ptr(), ptr::null_mut(), ptr::null_mut()) });
    assert_eq!(l, moved);
    assert!(!unsafe { propulsion_set_transform(session, 99, moved.as_ptr(), ptr::null(), ptr::null()) }, "no such entity");
    unsafe { propulsion_session_free(session) };
}

#[test]
fn input_and_system_calls_go_through_the_hosts_callbacks() {
    let mut rec = Recorded { forward_for: -1, ..Default::default() };
    rec.held.push("jump".to_string());
    rec.started.push("jump".to_string());
    let player = behaviors("player.prop");
    let session = open(&mut rec, &[&player]);
    let id = unsafe { propulsion_spawn(session, player.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    unsafe { propulsion_step(session, 0.125) };
    let mut l = [0f32; 3];
    assert!(unsafe { propulsion_get_transform(session, id, l.as_mut_ptr(), ptr::null_mut(), ptr::null_mut()) });
    assert!(l[1] > 0.0, "the player jumped up, got {l:?}");
    assert!(rec.calls.iter().any(|c| c == "Audio.Play sound=\"jump.wav\""), "calls: {:?}", rec.calls);
    unsafe { propulsion_session_free(session) };
}

#[test]
fn a_script_with_a_mistake_is_refused_with_the_line_to_fix() {
    let dir = std::env::temp_dir().join(format!("propulsion_ffi_bad_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let bad = dir.join("bad.prop");
    std::fs::write(&bad, "header:\n\tneeds component:Transform\n\nimplement:\n\tevery frame:\n\t\tadd Nothing to Transform.Rot.Y\n").unwrap();
    let bad = CString::new(bad.to_string_lossy().as_bytes()).unwrap();

    let mut rec = Recorded { forward_for: -1, ..Default::default() };
    let ptrs = [bad.as_ptr()];
    let mut errors: *mut c_char = ptr::null_mut();
    let session = unsafe { propulsion_session_new(host(&mut rec), ptrs.as_ptr(), 1, &mut errors) };
    assert!(session.is_null());
    assert!(!errors.is_null());
    let text = s(errors);
    assert!(text.contains("Nothing") && text.contains("bad.prop"), "{text}");
    unsafe { propulsion_string_free(errors) };

    let mut again: *mut c_char = ptr::null_mut();
    assert!(!unsafe { propulsion_check_file(bad.as_ptr(), &mut again) });
    assert!(s(again).contains("Nothing"));
    unsafe { propulsion_string_free(again) };
    assert!(unsafe { propulsion_check_file(behaviors("rotator.prop").as_ptr(), ptr::null_mut()) });

    let missing = CString::new("no/such/file.prop").unwrap();
    let ptrs = [missing.as_ptr()];
    let mut errors: *mut c_char = ptr::null_mut();
    assert!(unsafe { propulsion_session_new(host(&mut rec), ptrs.as_ptr(), 1, &mut errors) }.is_null());
    assert!(s(errors).contains("no/such/file.prop"));
    unsafe { propulsion_string_free(errors) };
}

#[test]
fn spawning_a_script_that_was_not_loaded_fails_cleanly() {
    let mut rec = Recorded { forward_for: -1, ..Default::default() };
    let rotator = behaviors("rotator.prop");
    let session = open(&mut rec, &[&rotator]);
    let other = behaviors("door.prop");
    assert_eq!(unsafe { propulsion_spawn(session, other.as_ptr(), ptr::null(), ptr::null(), ptr::null()) }, -1);
    assert_eq!(unsafe { propulsion_spawn(ptr::null_mut(), rotator.as_ptr(), ptr::null(), ptr::null(), ptr::null()) }, -1);
    unsafe { propulsion_session_free(session) };
}

#[test]
fn several_scripts_share_one_world_and_can_touch() {
    let mut rec = Recorded { forward_for: -1, ..Default::default() };
    let door = behaviors("door.prop");
    let player = behaviors("player.prop");
    let session = open(&mut rec, &[&door, &player]);
    let d = unsafe { propulsion_spawn(session, door.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    let p = unsafe { propulsion_spawn(session, player.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    unsafe { propulsion_touch(session, d, p, true) };
    for _ in 0..8 {
        unsafe { propulsion_step(session, 0.125) };
    }
    let mut l = [0f32; 3];
    assert!(unsafe { propulsion_get_transform(session, d, l.as_mut_ptr(), ptr::null_mut(), ptr::null_mut()) });
    assert!((l[2] - 3.0).abs() < 0.01, "the door opened when touched, got {l:?}");
    assert!(rec.calls.iter().any(|c| c.starts_with("Audio.Play")), "{:?}", rec.calls);
    unsafe { propulsion_session_free(session) };
}

#[test]
fn version_is_available() {
    assert_eq!(s(propulsion_version()), env!("CARGO_PKG_VERSION"));
}

#[test]
fn the_graph_can_be_loaded_and_edited_through_the_c_interface() {
    let dir = std::env::temp_dir().join(format!("propulsion_ffi_graph_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let file = dir.join("spin.prop");
    std::fs::write(&file, "header:\n\tneeds component:Transform\n\nimplement:\n\tevery frame:\n\t\tadd 45.0 per second to Transform.Rot.Y\n").unwrap();
    let path = CString::new(file.to_string_lossy().as_bytes()).unwrap();
    let mut error: *mut c_char = ptr::null_mut();

    let json = unsafe { propulsion_graph_load(path.as_ptr(), &mut error) };
    assert!(!json.is_null(), "{}", if error.is_null() { String::new() } else { s(error) });
    let text = s(json);
    assert!(text.contains("\"name\":\"On Tick\"") && text.contains("\"name\":\"Rotate Object\"") && text.contains("\"name\":\"Per Second\""), "{text}");
    unsafe { propulsion_string_free(json) };

    let (op, id, pin, value) = (CString::new("set_pin").unwrap(), CString::new("h0.0.0").unwrap(), CString::new("Target").unwrap(), CString::new("Transform.Rot.X").unwrap());
    let edited = unsafe { propulsion_graph_edit(path.as_ptr(), op.as_ptr(), id.as_ptr(), pin.as_ptr(), value.as_ptr(), ptr::null(), &mut error) };
    assert!(!edited.is_null(), "{}", if error.is_null() { String::new() } else { s(error) });
    assert!(s(edited).contains("Transform.Rot.X"));
    unsafe { propulsion_string_free(edited) };
    assert!(std::fs::read_to_string(&file).unwrap().contains("to Transform.Rot.X"));

    // A refused edit returns null, says why, and leaves the file alone.
    let before = std::fs::read_to_string(&file).unwrap();
    let bad = CString::new("two words").unwrap();
    let mut error: *mut c_char = ptr::null_mut();
    let refused = unsafe { propulsion_graph_edit(path.as_ptr(), op.as_ptr(), id.as_ptr(), pin.as_ptr(), bad.as_ptr(), ptr::null(), &mut error) };
    assert!(refused.is_null());
    assert!(!s(error).is_empty());
    unsafe { propulsion_string_free(error) };
    assert_eq!(std::fs::read_to_string(&file).unwrap(), before);

    let catalog = unsafe { propulsion_graph_catalog() };
    assert!(s(catalog).contains("\"name\":\"Branch\""));
    unsafe { propulsion_string_free(catalog) };
}

#[test]
fn the_whole_text_can_be_checked_and_replaced_through_the_c_interface() {
    let dir = std::env::temp_dir().join(format!("propulsion_ffi_source_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let file = dir.join("s.prop");
    let original = "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tfinish\n";
    std::fs::write(&file, original).unwrap();
    let path = CString::new(file.to_string_lossy().as_bytes()).unwrap();

    // A text that parses is fine; the outputs may be null.
    let good = CString::new(original).unwrap();
    assert!(unsafe { propulsion_graph_check_source(good.as_ptr(), ptr::null_mut(), ptr::null_mut(), ptr::null_mut()) });

    // One that does not says where and why.
    let bad = CString::new("header:\n\tneeds component:Transform\n\nimplement:\n\twhen created\n\t\tfinish\n").unwrap();
    let (mut line, mut col, mut message): (i32, i32, *mut c_char) = (0, 0, ptr::null_mut());
    assert!(!unsafe { propulsion_graph_check_source(bad.as_ptr(), &mut line, &mut col, &mut message) });
    assert_eq!(line, 5);
    assert!(col >= 1);
    assert!(!s(message).is_empty());
    unsafe { propulsion_string_free(message) };

    // set_source writes a text that parses, as typed, and refuses one that does not.
    let (op, typed) = (CString::new("set_source").unwrap(), CString::new(original.replace("finish", "finish   ")).unwrap());
    let mut error: *mut c_char = ptr::null_mut();
    let json = unsafe { propulsion_graph_edit(path.as_ptr(), op.as_ptr(), typed.as_ptr(), ptr::null(), ptr::null(), ptr::null(), &mut error) };
    assert!(!json.is_null(), "{}", if error.is_null() { String::new() } else { s(error) });
    unsafe { propulsion_string_free(json) };
    assert_eq!(std::fs::read_to_string(&file).unwrap(), original.replace("finish", "finish   "));

    let before = std::fs::read_to_string(&file).unwrap();
    let mut error: *mut c_char = ptr::null_mut();
    let refused = unsafe { propulsion_graph_edit(path.as_ptr(), op.as_ptr(), bad.as_ptr(), ptr::null(), ptr::null(), ptr::null(), &mut error) };
    assert!(refused.is_null());
    assert!(!s(error).is_empty());
    unsafe { propulsion_string_free(error) };
    assert_eq!(std::fs::read_to_string(&file).unwrap(), before);
}

#[test]
fn hits_damage_and_flags_reach_the_scripts_and_come_back() {
    let mut rec = Recorded { forward_for: -1, ..Default::default() };
    let world = behaviors("world_kit.prop");
    let math = behaviors("math_kit.prop");
    let session = open(&mut rec, &[&world, &math]);
    let a = unsafe { propulsion_spawn(session, world.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    let m = unsafe { propulsion_spawn(session, math.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    assert!(a >= 0 && m >= 0);

    // The engine says two bodies collided, and that something hurt an entity: the handlers run (their effects stay
    // inside the scripts, so what comes back out is only that nothing went wrong).
    unsafe { propulsion_hit(session, a, m) };
    unsafe { propulsion_damage(session, a, -1, 30.0) };

    let (mut shown, mut collides) = (false, false);
    assert!(unsafe { propulsion_get_flags(session, a, &mut shown, &mut collides) });
    assert!(shown && collides, "an entity starts shown and solid");
    assert!(!unsafe { propulsion_get_flags(session, 99, &mut shown, &mut collides) }, "an entity that doesn't exist has no flags");
    assert!(unsafe { propulsion_take_warnings(session) }.is_null(), "no warnings");
    unsafe { propulsion_session_free(session) };
}
