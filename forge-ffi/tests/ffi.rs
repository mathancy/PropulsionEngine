//! The C interface, called the way the engine calls it.

use forge_ffi::*;
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
}

fn s(p: *const c_char) -> String {
    unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
}

extern "C" fn pressing(user: *mut c_void, _system: *const c_char, input: *const c_char, kind: i32) -> bool {
    let r = unsafe { &*(user as *const Recorded) };
    let input = s(input);
    match kind {
        0 => r.held.contains(&input),
        1 => !r.held.contains(&input),
        2 => r.started.contains(&input),
        _ => false,
    }
}
extern "C" fn axis(_user: *mut c_void, _system: *const c_char, input: *const c_char) -> f32 {
    if s(input) == "moveForward" {
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

fn host(rec: &mut Recorded) -> ForgeHost {
    ForgeHost {
        user: rec as *mut Recorded as *mut c_void,
        pressing: Some(pressing),
        axis: Some(axis),
        call: Some(call),
        print: Some(print),
        end_frame: Some(end_frame),
        level_name: Some(level_name),
    }
}

fn script(folder: &str, name: &str) -> CString {
    let p: PathBuf = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..").join(folder).join(name);
    CString::new(p.to_string_lossy().as_bytes()).unwrap()
}

fn behaviors(name: &str) -> CString {
    script("behaviors", name)
}


fn open(rec: &mut Recorded, paths: &[&CString]) -> *mut ForgeSession {
    let ptrs: Vec<*const c_char> = paths.iter().map(|p| p.as_ptr()).collect();
    let mut errors: *mut c_char = ptr::null_mut();
    let session = unsafe { forge_session_new(host(rec), ptrs.as_ptr(), ptrs.len(), &mut errors) };
    assert!(!session.is_null(), "{}", if errors.is_null() { String::new() } else { s(errors) });
    session
}

#[test]
fn a_rotator_spins_and_its_transform_comes_back_out() {
    let mut rec = Recorded::default();
    let rotator = behaviors("rotator.forge");
    let session = open(&mut rec, &[&rotator]);

    let loc = [1.0f32, 2.0, 3.0];
    let id = unsafe { forge_spawn(session, rotator.as_ptr(), loc.as_ptr(), ptr::null(), ptr::null()) };
    assert_eq!(id, 0);

    for _ in 0..8 {
        unsafe { forge_step(session, 0.25) };
    }
    let (mut l, mut r, mut sc) = ([0f32; 3], [0f32; 3], [0f32; 3]);
    assert!(unsafe { forge_get_transform(session, id, l.as_mut_ptr(), r.as_mut_ptr(), sc.as_mut_ptr()) });
    assert_eq!(l, loc, "position is kept");
    assert!((r[1] - 90.0).abs() < 0.01, "45 degrees a second for 2 seconds, got {r:?}");
    assert_eq!(sc, [1.0; 3]);
    assert_eq!(rec.ended, 8, "end_frame is called once per step");
    assert_eq!(rec.printed, vec!["Rotation is 90".to_string()], "the 2-second print fired once");
    unsafe { forge_session_free(session) };
}

#[test]
fn an_object_placed_with_a_rotation_starts_from_it() {
    let mut rec = Recorded::default();
    let rotator = behaviors("rotator.forge");
    let session = open(&mut rec, &[&rotator]);
    let rot = [0.0f32, 30.0, 0.0];
    let scale = [2.0f32, 2.0, 2.0];
    let id = unsafe { forge_spawn(session, rotator.as_ptr(), ptr::null(), rot.as_ptr(), scale.as_ptr()) };
    unsafe { forge_step(session, 1.0) };
    let (mut r, mut sc) = ([0f32; 3], [0f32; 3]);
    assert!(unsafe { forge_get_transform(session, id, ptr::null_mut(), r.as_mut_ptr(), sc.as_mut_ptr()) });
    assert!((r[1] - 75.0).abs() < 0.01, "30 + 45, got {r:?}");
    assert_eq!(sc, scale);
    unsafe { forge_session_free(session) };
}

#[test]
fn the_engine_can_move_an_entity_between_steps() {
    let mut rec = Recorded::default();
    let rotator = behaviors("rotator.forge");
    let session = open(&mut rec, &[&rotator]);
    let id = unsafe { forge_spawn(session, rotator.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    let moved = [5.0f32, 0.0, 0.0];
    assert!(unsafe { forge_set_transform(session, id, moved.as_ptr(), ptr::null(), ptr::null()) });
    unsafe { forge_step(session, 0.25) };
    let mut l = [0f32; 3];
    assert!(unsafe { forge_get_transform(session, id, l.as_mut_ptr(), ptr::null_mut(), ptr::null_mut()) });
    assert_eq!(l, moved);
    assert!(!unsafe { forge_set_transform(session, 99, moved.as_ptr(), ptr::null(), ptr::null()) }, "no such entity");
    unsafe { forge_session_free(session) };
}

#[test]
fn input_and_system_calls_go_through_the_hosts_callbacks() {
    let mut rec = Recorded::default();
    rec.held.push("jump".to_string());
    rec.started.push("jump".to_string());
    let player = behaviors("player.forge");
    let session = open(&mut rec, &[&player]);
    let id = unsafe { forge_spawn(session, player.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    unsafe { forge_step(session, 0.125) };
    let mut l = [0f32; 3];
    assert!(unsafe { forge_get_transform(session, id, l.as_mut_ptr(), ptr::null_mut(), ptr::null_mut()) });
    assert!(l[1] > 0.0, "the player jumped up, got {l:?}");
    assert!(rec.calls.iter().any(|c| c == "Audio.Play sound=\"jump.wav\""), "calls: {:?}", rec.calls);
    unsafe { forge_session_free(session) };
}

#[test]
fn a_script_with_a_mistake_is_refused_with_the_line_to_fix() {
    let dir = std::env::temp_dir().join(format!("forge_ffi_bad_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let bad = dir.join("bad.forge");
    std::fs::write(&bad, "header:\n\tneeds component:Transform\n\nimplement:\n\tevery frame:\n\t\tadd Nothing to Transform.Rot.Y\n").unwrap();
    let bad = CString::new(bad.to_string_lossy().as_bytes()).unwrap();

    let mut rec = Recorded::default();
    let ptrs = [bad.as_ptr()];
    let mut errors: *mut c_char = ptr::null_mut();
    let session = unsafe { forge_session_new(host(&mut rec), ptrs.as_ptr(), 1, &mut errors) };
    assert!(session.is_null());
    assert!(!errors.is_null());
    let text = s(errors);
    assert!(text.contains("Nothing") && text.contains("bad.forge"), "{text}");
    unsafe { forge_string_free(errors) };

    let mut again: *mut c_char = ptr::null_mut();
    assert!(!unsafe { forge_check_file(bad.as_ptr(), &mut again) });
    assert!(s(again).contains("Nothing"));
    unsafe { forge_string_free(again) };
    assert!(unsafe { forge_check_file(behaviors("rotator.forge").as_ptr(), ptr::null_mut()) });

    let missing = CString::new("no/such/file.forge").unwrap();
    let ptrs = [missing.as_ptr()];
    let mut errors: *mut c_char = ptr::null_mut();
    assert!(unsafe { forge_session_new(host(&mut rec), ptrs.as_ptr(), 1, &mut errors) }.is_null());
    assert!(s(errors).contains("no/such/file.forge"));
    unsafe { forge_string_free(errors) };
}

#[test]
fn spawning_a_script_that_was_not_loaded_fails_cleanly() {
    let mut rec = Recorded::default();
    let rotator = behaviors("rotator.forge");
    let session = open(&mut rec, &[&rotator]);
    let other = behaviors("door.forge");
    assert_eq!(unsafe { forge_spawn(session, other.as_ptr(), ptr::null(), ptr::null(), ptr::null()) }, -1);
    assert_eq!(unsafe { forge_spawn(ptr::null_mut(), rotator.as_ptr(), ptr::null(), ptr::null(), ptr::null()) }, -1);
    unsafe { forge_session_free(session) };
}

#[test]
fn several_scripts_share_one_world_and_can_touch() {
    let mut rec = Recorded::default();
    let door = behaviors("door.forge");
    let player = behaviors("player.forge");
    let session = open(&mut rec, &[&door, &player]);
    let d = unsafe { forge_spawn(session, door.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    let p = unsafe { forge_spawn(session, player.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    unsafe { forge_touch(session, d, p, true) };
    for _ in 0..8 {
        unsafe { forge_step(session, 0.125) };
    }
    let mut l = [0f32; 3];
    assert!(unsafe { forge_get_transform(session, d, l.as_mut_ptr(), ptr::null_mut(), ptr::null_mut()) });
    assert!((l[1] - 3.0).abs() < 0.01, "the door opened when touched, got {l:?}");
    assert!(rec.calls.iter().any(|c| c.starts_with("Audio.Play")), "{:?}", rec.calls);
    unsafe { forge_session_free(session) };
}

#[test]
fn version_is_available() {
    assert_eq!(s(forge_version()), env!("CARGO_PKG_VERSION"));
}

#[test]
fn the_graph_can_be_loaded_and_edited_through_the_c_interface() {
    let dir = std::env::temp_dir().join(format!("forge_ffi_graph_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let file = dir.join("spin.forge");
    std::fs::write(&file, "header:\n\tneeds component:Transform\n\nimplement:\n\tevery frame:\n\t\tadd 45.0 per second to Transform.Rot.Y\n").unwrap();
    let path = CString::new(file.to_string_lossy().as_bytes()).unwrap();
    let mut error: *mut c_char = ptr::null_mut();

    let json = unsafe { forge_graph_load(path.as_ptr(), &mut error) };
    assert!(!json.is_null(), "{}", if error.is_null() { String::new() } else { s(error) });
    let text = s(json);
    assert!(text.contains("\"name\":\"On Tick\"") && text.contains("\"name\":\"Rotate Object\"") && text.contains("\"name\":\"Per Second\""), "{text}");
    unsafe { forge_string_free(json) };

    let (op, id, pin, value) = (CString::new("set_pin").unwrap(), CString::new("h0.0.0").unwrap(), CString::new("Target").unwrap(), CString::new("Transform.Rot.X").unwrap());
    let edited = unsafe { forge_graph_edit(path.as_ptr(), op.as_ptr(), id.as_ptr(), pin.as_ptr(), value.as_ptr(), ptr::null(), &mut error) };
    assert!(!edited.is_null(), "{}", if error.is_null() { String::new() } else { s(error) });
    assert!(s(edited).contains("Transform.Rot.X"));
    unsafe { forge_string_free(edited) };
    assert!(std::fs::read_to_string(&file).unwrap().contains("to Transform.Rot.X"));

    // A refused edit returns null, says why, and leaves the file alone.
    let before = std::fs::read_to_string(&file).unwrap();
    let bad = CString::new("two words").unwrap();
    let mut error: *mut c_char = ptr::null_mut();
    let refused = unsafe { forge_graph_edit(path.as_ptr(), op.as_ptr(), id.as_ptr(), pin.as_ptr(), bad.as_ptr(), ptr::null(), &mut error) };
    assert!(refused.is_null());
    assert!(!s(error).is_empty());
    unsafe { forge_string_free(error) };
    assert_eq!(std::fs::read_to_string(&file).unwrap(), before);

    let catalog = unsafe { forge_graph_catalog() };
    assert!(s(catalog).contains("\"name\":\"Branch\""));
    unsafe { forge_string_free(catalog) };
}

#[test]
fn the_whole_text_can_be_checked_and_replaced_through_the_c_interface() {
    let dir = std::env::temp_dir().join(format!("forge_ffi_source_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let file = dir.join("s.forge");
    let original = "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tfinish\n";
    std::fs::write(&file, original).unwrap();
    let path = CString::new(file.to_string_lossy().as_bytes()).unwrap();

    // A text that parses is fine; the outputs may be null.
    let good = CString::new(original).unwrap();
    assert!(unsafe { forge_graph_check_source(good.as_ptr(), ptr::null_mut(), ptr::null_mut(), ptr::null_mut()) });

    // One that does not says where and why.
    let bad = CString::new("header:\n\tneeds component:Transform\n\nimplement:\n\twhen created\n\t\tfinish\n").unwrap();
    let (mut line, mut col, mut message): (i32, i32, *mut c_char) = (0, 0, ptr::null_mut());
    assert!(!unsafe { forge_graph_check_source(bad.as_ptr(), &mut line, &mut col, &mut message) });
    assert_eq!(line, 5);
    assert!(col >= 1);
    assert!(!s(message).is_empty());
    unsafe { forge_string_free(message) };

    // set_source writes a text that parses, as typed, and refuses one that does not.
    let (op, typed) = (CString::new("set_source").unwrap(), CString::new(original.replace("finish", "finish   ")).unwrap());
    let mut error: *mut c_char = ptr::null_mut();
    let json = unsafe { forge_graph_edit(path.as_ptr(), op.as_ptr(), typed.as_ptr(), ptr::null(), ptr::null(), ptr::null(), &mut error) };
    assert!(!json.is_null(), "{}", if error.is_null() { String::new() } else { s(error) });
    unsafe { forge_string_free(json) };
    assert_eq!(std::fs::read_to_string(&file).unwrap(), original.replace("finish", "finish   "));

    let before = std::fs::read_to_string(&file).unwrap();
    let mut error: *mut c_char = ptr::null_mut();
    let refused = unsafe { forge_graph_edit(path.as_ptr(), op.as_ptr(), bad.as_ptr(), ptr::null(), ptr::null(), ptr::null(), &mut error) };
    assert!(refused.is_null());
    assert!(!s(error).is_empty());
    unsafe { forge_string_free(error) };
    assert_eq!(std::fs::read_to_string(&file).unwrap(), before);
}

#[test]
fn hits_damage_and_flags_reach_the_scripts_and_come_back() {
    let mut rec = Recorded::default();
    let world = behaviors("world_kit.forge");
    let math = behaviors("math_kit.forge");
    let session = open(&mut rec, &[&world, &math]);
    let a = unsafe { forge_spawn(session, world.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    let m = unsafe { forge_spawn(session, math.as_ptr(), ptr::null(), ptr::null(), ptr::null()) };
    assert!(a >= 0 && m >= 0);

    // The engine says two bodies collided, and that something hurt an entity: the handlers run (their effects stay
    // inside the scripts, so what comes back out is only that nothing went wrong).
    unsafe { forge_hit(session, a, m) };
    unsafe { forge_damage(session, a, -1, 30.0) };

    let (mut shown, mut collides) = (false, false);
    assert!(unsafe { forge_get_flags(session, a, &mut shown, &mut collides) });
    assert!(shown && collides, "an entity starts shown and solid");
    assert!(!unsafe { forge_get_flags(session, 99, &mut shown, &mut collides) }, "an entity that doesn't exist has no flags");
    assert!(unsafe { forge_take_warnings(session) }.is_null(), "no warnings");
    unsafe { forge_session_free(session) };
}
