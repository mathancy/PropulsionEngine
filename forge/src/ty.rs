//! The types a value can have once a file has been checked.

use crate::ast::TypeExpr;
use std::fmt;

#[derive(Debug, Clone, PartialEq)]
pub enum Type {
    Integer,
    Decimal,
    Boolean,
    Text,
    Vector2,
    Vector3,
    Color,
    Entity,
    Timer,
    /// Open or closed: `open Door`, `through Door:`.
    Gate,
    Enum(String),
    Struct(String),
    /// `10 seconds`. Only valid next to a timer.
    Duration,
    List(Box<Type>),
}

/// Which part of a vector or color a name picks out: `X` (or `Pitch`), `Y` (or `Yaw`), `Z` (or `Roll`) of a
/// vector; `R`, `G`, `B`, `A` of a color. Pitch, yaw and roll are the same three numbers as X, Y and Z, named
/// the way a rotation reads them.
pub fn part_index(ty: &Type, name: &str) -> Option<usize> {
    match (ty, name) {
        (Type::Vector2 | Type::Vector3, "X") | (Type::Vector3, "Pitch") => Some(0),
        (Type::Vector2 | Type::Vector3, "Y") | (Type::Vector3, "Yaw") => Some(1),
        (Type::Vector3, "Z" | "Roll") => Some(2),
        (Type::Color, "R") => Some(0),
        (Type::Color, "G") => Some(1),
        (Type::Color, "B") => Some(2),
        (Type::Color, "A") => Some(3),
        _ => None,
    }
}

/// The names of a type's parts, for messages.
pub fn part_names(ty: &Type) -> &'static str {
    match ty {
        Type::Vector2 => "`X`, `Y`",
        Type::Vector3 => "`X`, `Y`, `Z` (or `Pitch`, `Yaw`, `Roll`)",
        Type::Color => "`R`, `G`, `B`, `A`",
        _ => "",
    }
}

impl Type {
    pub fn from_expr(t: &TypeExpr) -> Type {
        match t {
            TypeExpr::Integer => Type::Integer,
            TypeExpr::Decimal => Type::Decimal,
            TypeExpr::Boolean => Type::Boolean,
            TypeExpr::Text => Type::Text,
            TypeExpr::Vector2 => Type::Vector2,
            TypeExpr::Vector3 => Type::Vector3,
            TypeExpr::Color => Type::Color,
            TypeExpr::Entity => Type::Entity,
            TypeExpr::Timer => Type::Timer,
            TypeExpr::Gate => Type::Gate,
            TypeExpr::Enum(n) => Type::Enum(n.clone()),
            TypeExpr::Struct(n) => Type::Struct(n.clone()),
            TypeExpr::List(inner) => Type::List(Box::new(Type::from_expr(inner))),
        }
    }

    pub fn is_numeric(&self) -> bool {
        matches!(self, Type::Integer | Type::Decimal)
    }

    /// Can this be turned into text by `joined with` / `text of`?
    pub fn is_printable(&self) -> bool {
        matches!(
            self,
            Type::Integer | Type::Decimal | Type::Boolean | Type::Text | Type::Vector2 | Type::Vector3 | Type::Color | Type::Enum(_)
        )
    }
}

impl fmt::Display for Type {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Type::Integer => write!(f, "integer"),
            Type::Decimal => write!(f, "decimal"),
            Type::Boolean => write!(f, "boolean"),
            Type::Text => write!(f, "text"),
            Type::Vector2 => write!(f, "vector2"),
            Type::Vector3 => write!(f, "vector3"),
            Type::Color => write!(f, "color"),
            Type::Entity => write!(f, "entity"),
            Type::Timer => write!(f, "timer"),
            Type::Gate => write!(f, "gate"),
            Type::Enum(n) | Type::Struct(n) => write!(f, "{n}"),
            Type::Duration => write!(f, "duration"),
            Type::List(inner) => write!(f, "list of {inner}"),
        }
    }
}
