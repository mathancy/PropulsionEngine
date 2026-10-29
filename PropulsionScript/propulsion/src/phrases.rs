//! Phrase expressions that are one table entry each.
//!
//! A phrase is a fixed sentence shape with single terms in it (SPEC 4.1): `clamp Health between 0 and 100`,
//! `random integer from 1 to 6`, `normalized Offset`. Everything about one lives in one place here: how it is
//! read and written, what types it takes, what it does in the interpreter, and the C++ it becomes. The parser,
//! the formatter, the type checker, the interpreter, the C++ generator, the node table and the node editor all
//! go through this table, so adding a phrase is adding an entry (plus its node in `nodes.rs`).
//!
//! Phrases are recognized by their leading words, and only when the whole shape matches, so a variable that
//! happens to be called `length` or `map` still works as an ordinary name.

use crate::runtime::Val;
use crate::ty::Type;
use crate::typecheck::Typed;

/// One piece of a phrase's sentence.
#[derive(Debug, Clone, Copy)]
pub enum Piece {
    /// A fixed word.
    Word(&'static str),
    /// A single term (a pin of the node). The text is the pin's name.
    Arg(&'static str),
    /// A fixed word followed by a colon, like `pitch:`. The term after it is the next `Arg`.
    Key(&'static str),
}

use Piece::{Arg, Key, Word};

/// What a phrase needs from the world it runs in.
pub trait PhraseEnv {
    /// The length of this frame in seconds.
    fn dt(&self) -> f32;
    /// The next 64 random bits.
    fn random(&mut self) -> u64;
    /// An entity's rotation (Euler degrees). `None`, with a warning, if it is missing.
    fn rotation_of(&mut self, entity: &Val) -> Option<[f32; 3]>;
    /// A position: a vector, or an entity's location.
    fn position_of(&mut self, value: &Val) -> [f32; 3];
    /// The name of the level being played.
    fn level_name(&self) -> String;
    /// What a line from one place to another (this thick) hits first, as a `hit`.
    fn trace(&mut self, from: [f32; 3], to: [f32; 3], radius: f32) -> Val;
}

pub struct Phrase {
    /// The node this phrase is (see `nodes.rs`).
    pub node: &'static str,
    pub pieces: &'static [Piece],
    /// The type of the result for these argument types, or the reason they don't fit.
    pub typing: fn(&[Typed]) -> Result<Type, String>,
    pub eval: fn(&mut dyn PhraseEnv, &[Val]) -> Val,
    /// The C++ expression, with `{0}`, `{1}`... for the arguments.
    pub cpp: &'static str,
}

impl Phrase {
    /// The pins, in the order they appear in the sentence.
    pub fn pins(&self) -> Vec<&'static str> {
        self.pieces.iter().filter_map(|p| if let Arg(n) = p { Some(*n) } else { None }).collect()
    }

    /// The sentence with the pins as `<Name>`, like the node table shows it.
    pub fn pattern(&self) -> String {
        self.render(&self.pins().iter().map(|p| format!("<{p}>")).collect::<Vec<_>>())
    }

    /// The sentence with each pin's text put in.
    pub fn render(&self, args: &[String]) -> String {
        let mut out = String::new();
        let mut next = 0;
        let mut glue = false;
        for p in self.pieces {
            match p {
                Word(w) => {
                    if !out.is_empty() {
                        out.push(' ');
                    }
                    out.push_str(w);
                }
                Key(k) => {
                    if !out.is_empty() {
                        out.push(' ');
                    }
                    out.push_str(k);
                    out.push(':');
                    glue = true;
                }
                Arg(_) => {
                    if !out.is_empty() && !glue {
                        out.push(' ');
                    }
                    glue = false;
                    out.push_str(args.get(next).map(String::as_str).unwrap_or(""));
                    next += 1;
                }
            }
        }
        out
    }
}

pub fn find(node: &str) -> Option<&'static Phrase> {
    PHRASES.iter().find(|p| p.node == node)
}

/// Phrases that start with this word.
pub fn starting_with(word: &str) -> impl Iterator<Item = &'static Phrase> + '_ {
    PHRASES.iter().filter(move |p| matches!(p.pieces.first(), Some(Word(w)) if *w == word))
}

// ---------------------------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------------------------

fn is_decimal_like(t: &Typed) -> bool {
    t.ty == Type::Decimal || (t.ty == Type::Integer && t.int_lit)
}

fn decimal_args(args: &[Typed], names: &[&str], phrase: &str) -> Result<(), String> {
    for (t, name) in args.iter().zip(names) {
        if !is_decimal_like(t) {
            let hint = if t.ty == Type::Integer { " (use `decimal of ...` to convert a whole number)" } else { "" };
            return Err(format!("`{phrase}` needs {name} to be a `decimal`, but this is a `{}`{hint}", t.ty));
        }
    }
    Ok(())
}

/// Numbers that all have to be the same kind: whole-number literals fit a decimal, nothing else mixes.
fn same_number(args: &[Typed], names: &[&str], phrase: &str) -> Result<Type, String> {
    for (t, name) in args.iter().zip(names) {
        if !t.ty.is_numeric() {
            return Err(format!("`{phrase}` needs numbers, but {name} is a `{}`", t.ty));
        }
    }
    let base = args
        .iter()
        .find(|t| !(t.ty == Type::Integer && t.int_lit))
        .map(|t| t.ty.clone())
        .unwrap_or(Type::Integer);
    for (t, name) in args.iter().zip(names) {
        let fits = t.ty == base || (base == Type::Decimal && t.ty == Type::Integer && t.int_lit);
        if !fits {
            return Err(format!(
                "`{phrase}` needs numbers of the same kind, but {name} is a `{}` and the others are `{base}` (convert one with `decimal of ...` or `integer of ...`)",
                t.ty
            ));
        }
    }
    Ok(base)
}

fn clamp_type(a: &[Typed]) -> Result<Type, String> {
    same_number(a, &["the value", "the lowest", "the highest"], "clamp")
}

fn map_range_type(a: &[Typed]) -> Result<Type, String> {
    decimal_args(a, &["the value", "where it starts", "where it ends", "the new start", "the new end"], "map")?;
    Ok(Type::Decimal)
}

fn random_decimal_type(a: &[Typed]) -> Result<Type, String> {
    decimal_args(a, &["the lowest", "the highest"], "random decimal")?;
    Ok(Type::Decimal)
}

fn random_integer_type(a: &[Typed]) -> Result<Type, String> {
    for (t, name) in a.iter().zip(["the lowest", "the highest"]) {
        if t.ty != Type::Integer {
            return Err(format!("`random integer` needs {name} to be an `integer`, but this is a `{}` (use `integer of ...` to convert a decimal)", t.ty));
        }
    }
    Ok(Type::Integer)
}

fn vector_type(a: &[Typed]) -> Result<Type, String> {
    match a[0].ty {
        Type::Vector2 | Type::Vector3 => Ok(a[0].ty.clone()),
        ref other => Err(format!("`normalized` needs a vector, but this is a `{other}`")),
    }
}

fn length_type(a: &[Typed]) -> Result<Type, String> {
    match a[0].ty {
        Type::Vector2 | Type::Vector3 => Ok(Type::Decimal),
        ref other => Err(format!("`length of` needs a vector, but this is a `{other}`")),
    }
}

fn dot_type(a: &[Typed]) -> Result<Type, String> {
    if a[0].ty == a[1].ty && matches!(a[0].ty, Type::Vector2 | Type::Vector3) {
        Ok(Type::Decimal)
    } else {
        Err(format!("`dot product of ... and ...` needs two vectors of the same kind, but this is a `{}` and a `{}`", a[0].ty, a[1].ty))
    }
}

fn rotator_type(a: &[Typed]) -> Result<Type, String> {
    decimal_args(a, &["`pitch:`", "`yaw:`", "`roll:`"], "rotation")?;
    Ok(Type::Vector3)
}

fn look_at_type(a: &[Typed]) -> Result<Type, String> {
    if a[0].ty == a[1].ty && matches!(a[0].ty, Type::Vector3 | Type::Entity) {
        Ok(Type::Vector3)
    } else {
        Err(format!("`rotation looking from ... to ...` needs two positions or two entities, but this is a `{}` and a `{}`", a[0].ty, a[1].ty))
    }
}

fn direction_type(a: &[Typed]) -> Result<Type, String> {
    if a[0].ty == Type::Entity {
        Ok(Type::Vector3)
    } else {
        Err(format!("this needs an entity (which way does a `{}` face?)", a[0].ty))
    }
}

fn not_type(a: &[Typed]) -> Result<Type, String> {
    if a[0].ty == Type::Boolean {
        Ok(Type::Boolean)
    } else {
        Err(format!("`not` needs a `boolean`, but this is a `{}`", a[0].ty))
    }
}

fn find_type(a: &[Typed]) -> Result<Type, String> {
    match &a[1].ty {
        Type::List(el) => {
            if **el == a[0].ty || (**el == Type::Decimal && a[0].ty == Type::Integer && a[0].int_lit) {
                Ok(Type::Integer)
            } else {
                Err(format!("`position of ... in ...` looks for a `{el}` in this list, but the item is a `{}`", a[0].ty))
            }
        }
        other => Err(format!("`position of ... in ...` needs a list, but this is a `{other}`")),
    }
}

fn ease_type(a: &[Typed]) -> Result<Type, String> {
    if !is_decimal_like(&a[2]) {
        return Err(format!("`ease ... toward ... at ...` needs a `decimal` speed, but this is a `{}`", a[2].ty));
    }
    match (&a[0].ty, &a[1].ty) {
        (Type::Vector2, Type::Vector2) | (Type::Vector3, Type::Vector3) => Ok(a[0].ty.clone()),
        _ if is_decimal_like(&a[0]) && is_decimal_like(&a[1]) => Ok(Type::Decimal),
        (x, y) => Err(format!("`ease ... toward ...` moves a decimal or a vector toward one of the same kind, but this is a `{x}` and a `{y}`")),
    }
}

fn turn_type(a: &[Typed]) -> Result<Type, String> {
    if !is_decimal_like(&a[2]) {
        return Err(format!("`turn ... toward ... at ...` needs a `decimal` speed, but this is a `{}`", a[2].ty));
    }
    if a[0].ty == Type::Vector3 && a[1].ty == Type::Vector3 {
        Ok(Type::Vector3)
    } else {
        Err(format!("`turn ... toward ...` turns a rotation (`vector3`) toward another, but this is a `{}` and a `{}`", a[0].ty, a[1].ty))
    }
}

fn text_type(_: &[Typed]) -> Result<Type, String> {
    Ok(Type::Text)
}

fn trace_type(a: &[Typed]) -> Result<Type, String> {
    if !(a[0].ty == a[1].ty && matches!(a[0].ty, Type::Vector3 | Type::Entity)) {
        return Err(format!("a trace goes from one position to another (or from one entity to another), but this is a `{}` and a `{}`", a[0].ty, a[1].ty));
    }
    if let Some(r) = a.get(2) {
        if !is_decimal_like(r) {
            return Err(format!("`radius` must be a `decimal`, but this is a `{}`", r.ty));
        }
    }
    Ok(Type::Struct("hit".to_string()))
}

// ---------------------------------------------------------------------------------------------
// What they do
// ---------------------------------------------------------------------------------------------

pub const DEGREES: f32 = 57.295_779_513_082_32;

fn num(v: &Val) -> f32 {
    v.num().unwrap_or(0.0) as f32
}

fn vec3(v: &Val) -> [f32; 3] {
    match v {
        Val::Vec3(v) => *v,
        Val::Vec2(v) => [v[0], v[1], 0.0],
        _ => [0.0; 3],
    }
}

/// Rotate `v` by Euler angles in degrees the way the engine does (`glm::quat(radians(rot)) * v`).
pub(crate) fn rotate_by_euler(rot_degrees: [f32; 3], v: [f32; 3]) -> [f32; 3] {
    rotate_by_quat(euler_quat(rot_degrees), v)
}

/// The same turned the other way.
pub(crate) fn rotate_by_euler_inverse(rot_degrees: [f32; 3], v: [f32; 3]) -> [f32; 3] {
    let q = euler_quat(rot_degrees);
    rotate_by_quat([-q[0], -q[1], -q[2], q[3]], v)
}

/// (x, y, z, w) of `glm::quat(radians(rot))`.
fn euler_quat(rot_degrees: [f32; 3]) -> [f32; 4] {
    let h = |d: f32| d.to_radians() * 0.5;
    let (cx, cy, cz) = (h(rot_degrees[0]).cos(), h(rot_degrees[1]).cos(), h(rot_degrees[2]).cos());
    let (sx, sy, sz) = (h(rot_degrees[0]).sin(), h(rot_degrees[1]).sin(), h(rot_degrees[2]).sin());
    [sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz]
}

fn rotate_by_quat(q: [f32; 4], v: [f32; 3]) -> [f32; 3] {
    let w = q[3];
    let qv = [q[0], q[1], q[2]];
    let cross = |a: [f32; 3], b: [f32; 3]| [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
    let uv = cross(qv, v);
    let uuv = cross(qv, uv);
    [v[0] + (uv[0] * w + uuv[0]) * 2.0, v[1] + (uv[1] * w + uuv[1]) * 2.0, v[2] + (uv[2] * w + uuv[2]) * 2.0]
}

/// `-0` and `0` are the same number but print differently; scripts only ever see `0`.
fn tidy(x: f32) -> f32 {
    x + 0.0
}

fn clamp(_: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    match (&a[0], &a[1], &a[2]) {
        (Val::Int(v), Val::Int(lo), Val::Int(hi)) => Val::Int(if v < lo {
            *lo
        } else if v > hi {
            *hi
        } else {
            *v
        }),
        _ => {
            let (v, lo, hi) = (num(&a[0]), num(&a[1]), num(&a[2]));
            Val::Dec(if v < lo {
                lo
            } else if v > hi {
                hi
            } else {
                v
            })
        }
    }
}

fn map_range(_: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    let (v, from_min, from_max, to_min, to_max) = (num(&a[0]), num(&a[1]), num(&a[2]), num(&a[3]), num(&a[4]));
    let t = if from_max == from_min { 0.0 } else { (v - from_min) / (from_max - from_min) };
    let t = if t < 0.0 {
        0.0
    } else if t > 1.0 {
        1.0
    } else {
        t
    };
    Val::Dec(to_min + (to_max - to_min) * t)
}

fn random_decimal(env: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    let (lo, hi) = (num(&a[0]), num(&a[1]));
    let unit = (env.random() >> 40) as f32 / 16_777_216.0;
    Val::Dec(lo + (hi - lo) * unit)
}

fn random_integer(env: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    let (lo, hi) = (num(&a[0]) as i64, num(&a[1]) as i64);
    if hi < lo {
        return Val::Int(lo as i32);
    }
    let span = (hi - lo + 1) as u64;
    Val::Int((lo + (env.random() % span) as i64) as i32)
}

fn length_of(v: &Val) -> f32 {
    match v {
        Val::Vec2(v) => (v[0] * v[0] + v[1] * v[1]).sqrt(),
        other => {
            let v = vec3(other);
            (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt()
        }
    }
}

fn normalized(_: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    let len = length_of(&a[0]);
    match &a[0] {
        Val::Vec2(v) => Val::Vec2(if len == 0.0 { [0.0; 2] } else { [v[0] / len, v[1] / len] }),
        other => {
            let v = vec3(other);
            Val::Vec3(if len == 0.0 { [0.0; 3] } else { [v[0] / len, v[1] / len, v[2] / len] })
        }
    }
}

fn vector_length(_: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    Val::Dec(length_of(&a[0]))
}

fn dot_product(_: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    match (&a[0], &a[1]) {
        (Val::Vec2(p), Val::Vec2(q)) => Val::Dec(p[0] * q[0] + p[1] * q[1]),
        (p, q) => {
            let (p, q) = (vec3(p), vec3(q));
            Val::Dec(p[0] * q[0] + p[1] * q[1] + p[2] * q[2])
        }
    }
}

fn make_rotator(_: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    // A rotation is stored as (pitch, roll, yaw), the turns about X, Y and Z, but written pitch, yaw, roll.
    Val::Vec3([num(&a[0]), num(&a[2]), num(&a[1])])
}

fn look_at(env: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    let from = env.position_of(&a[0]);
    let to = env.position_of(&a[1]);
    let d = [to[0] - from[0], to[1] - from[1], to[2] - from[2]];
    let len = (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]).sqrt();
    if len == 0.0 {
        return Val::Vec3([0.0; 3]);
    }
    // Forward is +Y and up is +Z. The engine turns by pitch (about X) and then yaw (about Z), so forward ends
    // up at (-cos(pitch) * sin(yaw), cos(pitch) * cos(yaw), sin(pitch)).
    let up = d[2] / len;
    let up = if up > 1.0 {
        1.0
    } else if up < -1.0 {
        -1.0
    } else {
        up
    };
    let pitch = up.asin() * DEGREES;
    let yaw = (-d[0]).atan2(d[1]) * DEGREES;
    Val::Vec3([tidy(pitch), 0.0, tidy(yaw)])
}

fn direction(env: &mut dyn PhraseEnv, entity: &Val, axis: [f32; 3]) -> Val {
    match env.rotation_of(entity) {
        Some(rot) => Val::Vec3(rotate_by_euler(rot, axis)),
        None => Val::Vec3(axis),
    }
}

fn forward_of(env: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    direction(env, &a[0], [0.0, 1.0, 0.0])
}

fn right_of(env: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    direction(env, &a[0], [1.0, 0.0, 0.0])
}

fn up_of(env: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    direction(env, &a[0], [0.0, 0.0, 1.0])
}

fn not(_: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    Val::Bool(!matches!(a[0], Val::Bool(true)))
}

/// Do two values count as the same thing in a list? (A whole number and a decimal can.)
pub(crate) fn same_value(a: &Val, b: &Val) -> bool {
    match (a.num(), b.num()) {
        (Some(x), Some(y)) => x == y,
        _ => a == b,
    }
}

fn position_of_item(_: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    match &a[1] {
        Val::List(_, items) => Val::Int(items.iter().position(|x| same_value(x, &a[0])).map(|i| i as i32).unwrap_or(-1)),
        _ => Val::Int(-1),
    }
}

fn step_fraction(env: &dyn PhraseEnv, speed: f32) -> f32 {
    let f = env.dt() * speed;
    if f < 0.0 {
        0.0
    } else if f > 1.0 {
        1.0
    } else {
        f
    }
}

fn wrap_half_turn(d: f32) -> f32 {
    let mut d = d % 360.0;
    if d > 180.0 {
        d -= 360.0;
    } else if d < -180.0 {
        d += 360.0;
    }
    d
}

fn ease_scalar(env: &dyn PhraseEnv, current: f32, target: f32, speed: f32) -> f32 {
    if speed <= 0.0 {
        return target;
    }
    let dist = target - current;
    if dist * dist < 1e-8 {
        return target;
    }
    current + dist * step_fraction(env, speed)
}

fn ease(env: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    let speed = num(&a[2]);
    match (&a[0], &a[1]) {
        (Val::Vec3(c), Val::Vec3(t)) => {
            if speed <= 0.0 {
                return Val::Vec3(*t);
            }
            let d = [t[0] - c[0], t[1] - c[1], t[2] - c[2]];
            if d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < 1e-8 {
                return Val::Vec3(*t);
            }
            let f = step_fraction(env, speed);
            Val::Vec3([c[0] + d[0] * f, c[1] + d[1] * f, c[2] + d[2] * f])
        }
        (Val::Vec2(c), Val::Vec2(t)) => {
            if speed <= 0.0 {
                return Val::Vec2(*t);
            }
            let d = [t[0] - c[0], t[1] - c[1]];
            if d[0] * d[0] + d[1] * d[1] < 1e-8 {
                return Val::Vec2(*t);
            }
            let f = step_fraction(env, speed);
            Val::Vec2([c[0] + d[0] * f, c[1] + d[1] * f])
        }
        (c, t) => Val::Dec(ease_scalar(env, num(c), num(t), speed)),
    }
}

fn turn(env: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    let (c, t, speed) = (vec3(&a[0]), vec3(&a[1]), num(&a[2]));
    if speed <= 0.0 {
        return Val::Vec3(t);
    }
    let d = [wrap_half_turn(t[0] - c[0]), wrap_half_turn(t[1] - c[1]), wrap_half_turn(t[2] - c[2])];
    if d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < 1e-8 {
        return Val::Vec3(t);
    }
    let f = step_fraction(env, speed);
    Val::Vec3([c[0] + d[0] * f, c[1] + d[1] * f, c[2] + d[2] * f])
}

fn line_trace(env: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    let (from, to) = (env.position_of(&a[0]), env.position_of(&a[1]));
    env.trace(from, to, 0.0)
}

fn sweep_trace(env: &mut dyn PhraseEnv, a: &[Val]) -> Val {
    let (from, to) = (env.position_of(&a[0]), env.position_of(&a[1]));
    env.trace(from, to, num(&a[2]))
}

fn level_name(env: &mut dyn PhraseEnv, _: &[Val]) -> Val {
    Val::Text(env.level_name())
}

// ---------------------------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------------------------

pub static PHRASES: &[Phrase] = &[
    Phrase {
        node: "Clamp",
        pieces: &[Word("clamp"), Arg("Value"), Word("between"), Arg("Min"), Word("and"), Arg("Max")],
        typing: clamp_type,
        eval: clamp,
        cpp: "Clamp({0}, {1}, {2})",
    },
    Phrase {
        node: "Map Range",
        pieces: &[
            Word("map"),
            Arg("Value"),
            Word("from"),
            Arg("FromMin"),
            Word("to"),
            Arg("FromMax"),
            Word("into"),
            Arg("ToMin"),
            Word("to"),
            Arg("ToMax"),
        ],
        typing: map_range_type,
        eval: map_range,
        cpp: "MapRange({0}, {1}, {2}, {3}, {4})",
    },
    Phrase {
        node: "Random Decimal",
        pieces: &[Word("random"), Word("decimal"), Word("from"), Arg("Min"), Word("to"), Arg("Max")],
        typing: random_decimal_type,
        eval: random_decimal,
        cpp: "world->RandomDecimal({0}, {1})",
    },
    Phrase {
        node: "Random Integer",
        pieces: &[Word("random"), Word("integer"), Word("from"), Arg("Min"), Word("to"), Arg("Max")],
        typing: random_integer_type,
        eval: random_integer,
        cpp: "world->RandomInteger({0}, {1})",
    },
    Phrase { node: "Normalize", pieces: &[Word("normalized"), Arg("Vector")], typing: vector_type, eval: normalized, cpp: "Normalized({0})" },
    Phrase { node: "Vector Length", pieces: &[Word("length"), Word("of"), Arg("Vector")], typing: length_type, eval: vector_length, cpp: "VectorLength({0})" },
    Phrase {
        node: "Dot Product",
        pieces: &[Word("dot"), Word("product"), Word("of"), Arg("A"), Word("and"), Arg("B")],
        typing: dot_type,
        eval: dot_product,
        cpp: "DotProduct({0}, {1})",
    },
    Phrase {
        node: "Make Rotator",
        pieces: &[Word("rotation"), Key("pitch"), Arg("Pitch"), Key("yaw"), Arg("Yaw"), Key("roll"), Arg("Roll")],
        typing: rotator_type,
        eval: make_rotator,
        cpp: "glm::vec3(static_cast<float>({0}), static_cast<float>({2}), static_cast<float>({1}))",
    },
    Phrase {
        node: "Look At Rotation",
        pieces: &[Word("rotation"), Word("looking"), Word("from"), Arg("From"), Word("to"), Arg("To")],
        typing: look_at_type,
        eval: look_at,
        cpp: "world->LookAt({0}, {1})",
    },
    Phrase {
        node: "Forward Vector",
        pieces: &[Word("forward"), Word("of"), Arg("Entity")],
        typing: direction_type,
        eval: forward_of,
        cpp: "world->Direction({0}, glm::vec3(0.0f, 1.0f, 0.0f))",
    },
    Phrase {
        node: "Right Vector",
        pieces: &[Word("right"), Word("of"), Arg("Entity")],
        typing: direction_type,
        eval: right_of,
        cpp: "world->Direction({0}, glm::vec3(1.0f, 0.0f, 0.0f))",
    },
    Phrase {
        node: "Up Vector",
        pieces: &[Word("up"), Word("of"), Arg("Entity")],
        typing: direction_type,
        eval: up_of,
        cpp: "world->Direction({0}, glm::vec3(0.0f, 0.0f, 1.0f))",
    },
    Phrase { node: "Not", pieces: &[Word("not"), Arg("Value")], typing: not_type, eval: not, cpp: "(!{0})" },
    Phrase {
        node: "Find In List",
        pieces: &[Word("position"), Word("of"), Arg("Item"), Word("in"), Arg("List")],
        typing: find_type,
        eval: position_of_item,
        cpp: "IndexOf({1}, {0})",
    },
    Phrase {
        node: "Ease",
        pieces: &[Word("ease"), Arg("Current"), Word("toward"), Arg("Target"), Word("at"), Arg("Speed")],
        typing: ease_type,
        eval: ease,
        cpp: "world->Ease({0}, {1}, {2})",
    },
    Phrase {
        node: "Turn",
        pieces: &[Word("turn"), Arg("Current"), Word("toward"), Arg("Target"), Word("at"), Arg("Speed")],
        typing: turn_type,
        eval: turn,
        cpp: "world->Turn({0}, {1}, {2})",
    },
    Phrase {
        node: "Line Trace",
        pieces: &[Word("trace"), Word("from"), Arg("From"), Word("to"), Arg("To")],
        typing: trace_type,
        eval: line_trace,
        cpp: "world->Trace(self, {0}, {1}, 0.0f)",
    },
    Phrase {
        node: "Sphere Trace",
        pieces: &[Word("sweep"), Word("from"), Arg("From"), Word("to"), Arg("To"), Word("radius"), Arg("Radius")],
        typing: trace_type,
        eval: sweep_trace,
        cpp: "world->Trace(self, {0}, {1}, static_cast<float>({2}))",
    },
    Phrase { node: "Level Name", pieces: &[Word("level"), Word("name")], typing: text_type, eval: level_name, cpp: "world->host().LevelName()" },
];
