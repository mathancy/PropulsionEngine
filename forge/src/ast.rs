//! The syntax tree for a `.forge` file.

/// The comments and spacing that belong to one line of code. The language ignores them; the
/// formatter and the node editor keep them (SPEC 10).
#[derive(Debug, Clone, PartialEq, Default)]
pub struct Notes {
    /// Comment lines directly above, without the `//`.
    pub leading: Vec<String>,
    /// A comment at the end of the line.
    pub trailing: Option<String>,
    /// There was a blank line above (and above the leading comments).
    pub blank_before: bool,
    /// Blank lines inside the leading comments (see `lexer::Comments::gaps`).
    pub gaps: Vec<usize>,
    /// Where the node editor shows this line's node (grid units). Not part of the script: it is kept
    /// in a layout file next to it, and travels with the line through edits (see `graph`).
    pub pos: Option<(i32, i32)>,
    /// The same for the value nodes shown on this line's pins, by pin name.
    pub value_pos: Vec<(String, (i32, i32))>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct File {
    pub header: Vec<Decl>,
    pub implement: Vec<Item>,
    pub header_notes: Notes,
    pub implement_notes: Notes,
    /// Comments after the last line of code.
    pub footer: Vec<String>,
    /// Lines of comments the parser couldn't attach to anything (for example, above `otherwise:`
    /// or an `entry` line). The formatter refuses files that have any, rather than drop them.
    pub unattached: Vec<usize>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum TypeExpr {
    Integer,
    Decimal,
    Boolean,
    Text,
    Vector2,
    Vector3,
    Color,
    Entity,
    Timer,
    /// `variable name:Door type:gate`: open or closed (`open Door`, `close Door`, `through Door:`).
    Gate,
    List(Box<TypeExpr>),
    /// An enum declared in the header (`enum name:Mode values:Idle Chasing`).
    Enum(String),
    /// A struct declared in the header (`struct name:Item`), or the built-in `hit`.
    Struct(String),
}

/// Named parts of a vector or color, e.g. `x:0 y:1 z:0`.
pub type Parts = Vec<(String, Term)>;

#[derive(Debug, Clone, PartialEq)]
pub struct Decl {
    pub kind: DeclKind,
    pub line: usize,
    pub notes: Notes,
}

#[derive(Debug, Clone, PartialEq)]
pub enum DeclKind {
    /// `exposed` means a placed instance may override the starting value (SPEC 3).
    Variable { name: String, ty: TypeExpr, value: Option<Term>, parts: Parts, entries: Vec<Parts>, exposed: bool },
    Constant { name: String, ty: TypeExpr, value: Option<Term>, parts: Parts },
    NeedsComponent(String),
    NeedsSystem(String),
    NeedsEntity { name: String, file: String, tag: String },
    /// `needs entities:Enemies tag:"enemy"`: a live list of every entity with the tag.
    NeedsEntities { name: String, tag: String, file: Option<String> },
    NeedsFile { file: String, alias: Option<String>, when: Option<String> },
    /// `tag name:"enemy"`: every entity running this file carries the tag.
    Tag(String),
    Event(String),
    /// `enum name:Mode values:Idle Chasing Attacking`: a value that is one of a few named things.
    Enum { name: String, values: Vec<String> },
    /// `struct name:Item` with indented `field name:Cost type:integer` lines: several values kept together.
    Struct { name: String, fields: Vec<(String, TypeExpr)> },
    /// `table name:Items type:Item` with indented `row key:"Sword" Cost:5` lines: a fixed set of structs to look up.
    Table { name: String, ty: String, rows: Vec<TableRow> },
}

/// One `row key:"Sword" Cost:5` of a table: its key, and the fields it gives (the rest are the defaults).
#[derive(Debug, Clone, PartialEq)]
pub struct TableRow {
    pub key: String,
    pub values: Vec<(String, Term)>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Item {
    pub kind: ItemKind,
    pub line: usize,
    pub notes: Notes,
}

#[derive(Debug, Clone, PartialEq)]
pub enum ItemKind {
    Handler { trigger: Trigger, body: Vec<Stmt> },
    Action { name: String, params: Vec<Param>, gives: Option<TypeExpr>, body: Vec<Stmt> },
}

#[derive(Debug, Clone, PartialEq)]
pub enum Trigger {
    Created,
    Destroyed,
    EveryFrame,
    EverySeconds(f64),
    /// `when Self touches <File> as <Name>:`
    Touches { file: String, name: String },
    /// `when Self stops touching <File> as <Name>:`
    StopsTouching { file: String, name: String },
    /// `when <Variable> changes:`
    Changes(String),
    /// `when Input starts pressing jump:` (kind is `Started` or `Stopped`)
    Input { system: String, input: String, kind: PressKind },
    /// `when <File> sends <Event> as <Name>:`
    Sends { file: String, event: String, name: String },
    /// `when <Event>:`
    Event(String),
    /// `when damaged as Amount from Attacker:` runs when something does `damage Self by ...`. `Amount` is a
    /// `decimal`; `from` (optional) names the entity that did it.
    Damaged { amount: String, source: Option<String> },
    /// `when Self hits <File> as <Name>:`: the engine reports a blocking collision with an entity running that file.
    Hits { file: String, name: String },
}

#[derive(Debug, Clone, PartialEq)]
pub struct Param {
    pub name: String,
    pub ty: TypeExpr,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Stmt {
    pub kind: StmtKind,
    pub line: usize,
    pub notes: Notes,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct Limit {
    pub capped: Option<Term>,
    pub floored: Option<Term>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Arg {
    pub name: String,
    pub value: Value,
}

#[derive(Debug, Clone, PartialEq)]
pub enum StmtKind {
    Make { target: Term, value: Value, limit: Limit },
    Add { value: Value, target: Term, limit: Limit },
    Subtract { value: Value, target: Term, limit: Limit },
    Multiply { target: Term, value: Value },
    Divide { target: Term, value: Value },
    Remove { value: Value, list: Term },
    /// `remove item 2 from Alive`: by position.
    RemoveAt { index: Term, list: Term },
    /// `add Fresh to Alive, if new`: only when it isn't in the list yet.
    AddUnique { value: Value, list: Term },
    /// `clear Alive`
    Clear(Term),
    Do { action: String, on: Option<String>, args: Vec<Arg> },
    Send(String),
    Remember { name: String, value: Value },
    Reset(String),
    Spawn { file: String, at: Value, name: String },
    Move { target: Term, toward: Value, by: Value },
    /// `move Self forward by Speed per second`: relative to the entity's rotation.
    MoveDir { target: Term, dir: Direction, by: Value },
    /// `print "Health is " joined with Health`
    Print(Value),
    /// `after 2 seconds:` runs the block once, later. Local names are copied when it is scheduled.
    After { seconds: Term, body: Vec<Stmt> },
    /// `for Index from 0 to 4:` counts up, including both ends.
    ForRange { var: String, from: Term, to: Term, body: Vec<Stmt> },
    Destroy(Term),
    GiveBack(Value),
    Finish,
    StopLoop,
    Skip,
    /// `inline` is the one-line form, `if X, then ...`. It means the same as a block with one
    /// statement; it is kept so the formatter doesn't change the author's style.
    If { branches: Vec<(Condition, Vec<Stmt>)>, otherwise: Option<Vec<Stmt>>, inline: bool },
    Repeat { times: Term, body: Vec<Stmt> },
    ForEach { var: String, list: Term, body: Vec<Stmt> },
    While { cond: Condition, body: Vec<Stmt> },
    /// `only once:` runs its block the first time it is reached; `reset <Name>` lets it run again.
    OnlyOnce { name: Option<String>, body: Vec<Stmt> },
    /// `first 3 times:` runs its block the first N times it is reached.
    FirstTimes { count: Term, name: Option<String>, body: Vec<Stmt> },
    /// `alternate:` runs the first block, then the second, then the first again, and so on.
    Alternate { name: Option<String>, first: Vec<Stmt>, second: Vec<Stmt> },
    /// `open Door` / `close Door` / `toggle Door`
    GateChange { gate: String, change: GateChange },
    /// `through Door:` runs its block only while the gate is open.
    Through { gate: String, body: Vec<Stmt> },
    /// `choose Lives:` with a `case` per value and an optional `otherwise:`.
    Choose { value: Term, cases: Vec<Case>, otherwise: Option<Vec<Stmt>> },
    /// `animate Lift from 0.0 to 5.0 over 2 seconds:` runs its block every frame for that long, with `Lift` moving
    /// from the first value to the second.
    Animate { var: String, from: Term, to: Term, seconds: Term, body: Vec<Stmt> },
    /// `damage Foe by 5.0`: runs the target's `when damaged` handlers.
    Damage { target: Term, amount: Value },
    /// `attach Hat to Player`: the entity follows the other until it is detached.
    Attach { child: Term, parent: Term },
    /// `detach Hat`
    Detach(Term),
    /// `tell Foe to TakeDamage amount:5`: ask any entity to run an action by name. It is skipped quietly when the
    /// entity has no such action, which is what makes it work across scripts that don't know each other.
    Tell { target: Term, action: String, args: Vec<Arg> },
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum GateChange {
    Open,
    Close,
    Toggle,
}

/// One `case 1 or 2:` of a `choose`.
#[derive(Debug, Clone, PartialEq)]
pub struct Case {
    pub values: Vec<Term>,
    pub body: Vec<Stmt>,
}

/// A single term: the only thing that can be an operand.
#[derive(Debug, Clone, PartialEq)]
pub enum Term {
    Int(i64),
    Decimal(f64),
    Text(String),
    Bool(bool),
    Name(String),
    /// `Transform.Loc.X`, optionally `... of Player`. A bare path means `Self`.
    Path { segments: Vec<String>, owner: Option<String> },
    /// `10 seconds`
    Duration(f64),
}

/// Directions for `move ... <direction> by ...`, relative to where the entity faces.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Direction {
    Forward,
    Back,
    Left,
    Right,
    Up,
    Down,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ArithOp {
    Plus,
    Minus,
    Times,
    DividedBy,
    Remainder,
}

/// Every value has one of a few fixed shapes (SPEC 4.1), so no parentheses are needed.
#[derive(Debug, Clone, PartialEq)]
pub enum Value {
    Term(Term),
    PerSecond(Term),
    Distance { from: Term, to: Term },
    CountOf(Term),
    ItemOf { index: Term, list: Term },
    JoinedWith { left: Term, right: Term },
    TextOf(Term),
    DecimalOf(Term),
    IntegerOf(Term),
    AbsoluteValueOf(Term),
    ResultOf { action: String, args: Vec<Arg> },
    Vector { x: Term, y: Term, z: Term },
    /// `time since start`: seconds since the game started.
    TimeSinceStart,
    /// `0.25 of the way from A to B`
    Lerp { t: Term, from: Term, to: Term },
    /// `axis moveForward of Input`: an analog input between -1 and 1.
    Axis { system: String, input: String },
    Arith { left: Term, op: ArithOp, right: Term },
    Condition(Box<Condition>),
    /// A phrase from the table in `phrases.rs`: `clamp Health between 0 and 100`, `random integer from 1 to 6`, ...
    Phrase { node: &'static str, args: Vec<Term> },
    /// `"Open" if Door is open otherwise "Closed"`: one of two values, by a condition.
    Pick { yes: Term, cond: Box<Condition>, no: Term },
    /// `cast Foe to Goblin`: the entity if it runs that file, otherwise a missing entity.
    CastTo { value: Term, file: String },
    /// `format "Score: {Score} of {Best}"`: text with values filled in. `parts` alternate literal text and values.
    Format { parts: Vec<FormatPart> },
    /// `new Item Name:"Sword" Cost:5`: a struct, with the fields it is given (the rest are the defaults).
    NewStruct { ty: String, args: Vec<Arg> },
    /// `row "Sword" of Items`: one row of a table, as a struct.
    RowOf { key: Term, table: String },
}

#[derive(Debug, Clone, PartialEq)]
pub enum FormatPart {
    Text(String),
    Value(Term),
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Joiner {
    And,
    Or,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum CmpOp {
    Eq,
    NotEq,
    Above,
    Below,
    AtLeast,
    AtMost,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Comparison {
    Compare { left: Value, op: CmpOp, right: Value },
    /// `Input is pressing moveForward`, `Input starts pressing jump`, ...
    Pressing { system: String, input: String, kind: PressKind },
    /// `Player is missing` / `Player is present`
    Presence { value: Value, missing: bool },
    /// `Alive contains Fresh` / `Alive does not contain Fresh`
    Contains { list: Value, item: Value, negated: bool },
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PressKind {
    /// `is pressing`: held down this frame.
    Held,
    /// `is not pressing`
    NotHeld,
    /// `starts pressing`: went down this frame.
    Started,
    /// `stops pressing`: came up this frame.
    Stopped,
}

/// One or more comparisons joined by the same word (`and` or `or`, never both).
#[derive(Debug, Clone, PartialEq)]
pub struct Condition {
    pub first: Comparison,
    pub rest: Vec<Comparison>,
    pub joiner: Option<Joiner>,
}
