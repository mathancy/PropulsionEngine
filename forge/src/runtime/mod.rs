//! A tree-walking interpreter for checked `.forge` files.
//!
//! The interpreter talks to the outside world through the [`Host`] trait: system actions
//! (`do Play on Audio ...`) and input (`Input is pressing jump`) go to the host, and everything
//! else (entities, variables, timers, events) lives in the [`World`]. [`StubHost`] just records
//! what scripts ask for, so scripts can run and be tested without an engine.

mod exec;
pub mod host;
pub mod value;
pub mod world;

pub use host::{Host, StubHost};
pub use value::Val;
pub use world::{Entity, World};
