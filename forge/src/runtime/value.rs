use crate::ty::Type;
use std::fmt;

/// A value at runtime. Integers are 32-bit and decimals are 32-bit floats (SPEC 3.1).
#[derive(Debug, Clone, PartialEq)]
pub enum Val {
    Int(i32),
    Dec(f32),
    Bool(bool),
    Text(String),
    Vec2([f32; 2]),
    Vec3([f32; 3]),
    Color([f32; 4]),
    /// `None` is a missing entity.
    Entity(Option<usize>),
    /// The time the timer was last reset. Its value is "now minus this".
    Timer(f64),
    /// `10 seconds`, only ever compared with a timer.
    Duration(f64),
    /// A list remembers its element type so an empty list can still hand out defaults.
    List(Type, Vec<Val>),
    /// One value of an enum: the enum's name, and which value.
    Enum(String, String),
    /// A struct: its name, and its fields in the order the struct declares them.
    Struct(String, Vec<(String, Val)>),
}

impl Val {
    pub fn default_for(ty: &Type, now: f64) -> Val {
        match ty {
            Type::Integer => Val::Int(0),
            Type::Decimal => Val::Dec(0.0),
            Type::Boolean => Val::Bool(false),
            Type::Text => Val::Text(String::new()),
            Type::Vector2 => Val::Vec2([0.0; 2]),
            Type::Vector3 => Val::Vec3([0.0; 3]),
            Type::Color => Val::Color([0.0; 4]),
            Type::Entity => Val::Entity(None),
            Type::Timer => Val::Timer(now),
            // A gate starts open (a variable can start it closed: `value:closed`).
            Type::Gate => Val::Bool(true),
            Type::Duration => Val::Duration(0.0),
            Type::List(el) => Val::List((**el).clone(), Vec::new()),
            // The world knows an enum's first value and a struct's fields; `World::default_val` fills these in.
            Type::Enum(n) => Val::Enum(n.clone(), String::new()),
            Type::Struct(n) => Val::Struct(n.clone(), Vec::new()),
        }
    }

    pub fn num(&self) -> Option<f64> {
        match self {
            Val::Int(i) => Some(*i as f64),
            Val::Dec(d) => Some(*d as f64),
            _ => None,
        }
    }

    /// A whole-number literal used where a decimal is expected becomes a decimal.
    pub fn coerce(self, ty: &Type) -> Val {
        match (&self, ty) {
            (Val::Int(i), Type::Decimal) => Val::Dec(*i as f32),
            _ => self,
        }
    }

    /// Same, when the target already holds a value to match.
    pub fn coerce_like(self, existing: &Val) -> Val {
        match (&self, existing) {
            (Val::Int(i), Val::Dec(_)) => Val::Dec(*i as f32),
            _ => self,
        }
    }

    /// A blend between two values of the same kind, `t` of the way from `a` to `b` (`animate` and `ease` use it).
    pub fn mix(a: &Val, b: &Val, t: f32) -> Val {
        let m = |x: f32, y: f32| x + (y - x) * t;
        match (a, b) {
            (Val::Vec3(p), Val::Vec3(q)) => Val::Vec3([m(p[0], q[0]), m(p[1], q[1]), m(p[2], q[2])]),
            (Val::Vec2(p), Val::Vec2(q)) => Val::Vec2([m(p[0], q[0]), m(p[1], q[1])]),
            _ => Val::Dec(m(a.num().unwrap_or(0.0) as f32, b.num().unwrap_or(0.0) as f32)),
        }
    }

    /// Like `Display`, but text is quoted. Used when logging calls.
    pub fn literal(&self) -> String {
        match self {
            Val::Text(s) => format!("\"{s}\""),
            other => other.to_string(),
        }
    }
}

impl fmt::Display for Val {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Val::Int(i) => write!(f, "{i}"),
            Val::Dec(d) => write!(f, "{d}"),
            Val::Bool(b) => write!(f, "{b}"),
            Val::Text(s) => write!(f, "{s}"),
            Val::Vec2(v) => write!(f, "({}, {})", v[0], v[1]),
            Val::Vec3(v) => write!(f, "({}, {}, {})", v[0], v[1], v[2]),
            Val::Color(c) => write!(f, "color({}, {}, {}, {})", c[0], c[1], c[2], c[3]),
            Val::Entity(Some(id)) => write!(f, "entity #{id}"),
            Val::Entity(None) => write!(f, "missing entity"),
            Val::Timer(t) => write!(f, "timer(since {t})"),
            Val::Duration(d) => write!(f, "{d} seconds"),
            Val::Enum(_, value) => write!(f, "{value}"),
            Val::Struct(name, fields) => {
                write!(f, "{name}(")?;
                for (i, (n, v)) in fields.iter().enumerate() {
                    if i > 0 {
                        write!(f, ", ")?;
                    }
                    write!(f, "{n}={v}")?;
                }
                write!(f, ")")
            }
            Val::List(_, items) => {
                write!(f, "[")?;
                for (i, v) in items.iter().enumerate() {
                    if i > 0 {
                        write!(f, ", ")?;
                    }
                    write!(f, "{v}")?;
                }
                write!(f, "]")
            }
        }
    }
}
