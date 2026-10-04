# ForgeScript Language Spec (draft v0.2)

ForgeScript is a scripting language for a custom Vulkan game engine. Source files use the `.forge` extension. The compiler and runtime are written in Rust.

## 1. Goals

1. **Readable by non-programmers.** Statements read like short sentences. No braces, semicolons, parentheses, or operator soup.
2. **Predictable.** It's a *controlled* natural language: a fixed list of sentence patterns, not free-form English. Every line parses exactly one way. Many patterns are one-offs, and that's fine.
3. **Fast.** Static types, no garbage collector, and dependencies declared up front so the compiler knows everything a file touches.
4. **Game-shaped.** Entities, components, events, and time are part of the language, not a library.

## 2. File structure

A `.forge` file has exactly two sections, in this order. Both end with a colon, and their contents are indented one tab.

```
header:
	...declarations...

implement:
	...behavior...
```

- `//` starts a comment, either on its own line or at the end of a line.
- Blocks are made with indentation (**tabs only**; spaces in indentation are a compile error).
- One statement per line. A line that ends with `:` opens an indented block.
- A block that holds nothing is written as the single line `do nothing`, and so is an `implement:` section with no handlers. It is not a statement: the block is simply empty (it is what the node editor writes when the last node of a block is deleted). A block with nothing under its `:` at all is still an error, so a forgotten indent is caught.
- Names are case-sensitive. User-defined names are `PascalCase` or `camelCase`, and they can't contain spaces (see 8, decision 1).
- **There are no parentheses in the language.** What other languages group with them is handled by fixed-shape expressions (4.1) and `remember`.
- **Dots are only used in field paths** (4.1b), like `Transform.Loc.X`. Files, actions, events, and systems are never reached with a dot.

## 3. Header

The header is the *only* place a file declares what it uses. Anything not declared here can't be referenced in `implement`. This is the main source of both clarity and speed (see 7).

Every declaration is a keyword followed by `key:value` pairs. The pairs are always named, so a line stays readable without knowing the language.

| Declaration | Example | Meaning |
|---|---|---|
| `variable` | `variable name:Health type:integer value:100` | Mutable state owned by this file |
| `constant` | `constant name:MaxHealth type:integer value:100` | Fixed at compile time |
| `needs component` | `needs component:Transform` | Engine component on the entity this file is attached to |
| `needs system` | `needs system:Input` | Engine-wide subsystem (`Input`, `Audio`, `UI`, ...) |
| `needs entity` | `needs entity:Player file:"player.forge" tag:"player"` | The one entity with this tag, whose behavior file is known (see 8, decision 8) |
| `needs entities` | `needs entities:Foes tag:"enemy"` | A live `list of entity` of everything with the tag. `file:` is optional. |
| `needs file` | `needs file:"coin.forge" as Coin when:used` | Another `.forge` file this one talks to |
| `event` | `event name:Died` | An event this file can send |
| `tag` | `tag name:"enemy"` | Every entity running this file carries this tag, so other files can find it with `needs entity` / `needs entities` |
| `enum` | `enum name:Mode values:Idle Chasing Attacking` | A value that is one of a few named things (3.2) |
| `struct` | `struct name:Item` with indented `field name:Cost type:integer` lines | Several values kept together (3.2) |
| `table` | `table name:Items type:Item` with indented `row key:"Sword" Cost:5` lines | A fixed set of structs to look up by key (3.2) |

`value:` is optional. Without it, the type's default is used: `0`, `0.0`, `false`, `""`.

**Exposed variables.** Add `exposed:true` to a `variable` to let each placed instance override its starting value, like a public field: `variable name:Speed type:decimal value:5.0 exposed:true`. Constants and timers can't be exposed, and an instance can only set variables that are. Setting anything else is skipped with a warning.

A `needs file` path (and the `file:` in `needs entity`) is relative to the file that mentions it, so `needs file:"ui/hud.forge"` in `examples/player.forge` means `examples/ui/hud.forge`. A path that doesn't exist is a compile error. `needs file` requires `as <Name>`, the name the rest of the file uses.

`needs file` takes an optional `when:` that controls loading:
- `when:used` (default): the file is loaded the first time it's referenced at runtime.
- `when:startup`: the file is loaded when this one is.

**Calling an action on a file name** (`do ShowCoins on Hud`) runs it on *the one entity running that file*. If there isn't one yet, it is created: at startup for `when:startup`, otherwise at the first call. This is how shared things like a HUD work. To get a separate copy each time, use `spawn`.

### 3.1 Types

| Type | Notes |
|---|---|
| `integer` | 32-bit signed |
| `decimal` | 32-bit float (matches GPU data) |
| `boolean` | `true` / `false` |
| `text` | Immutable, reference-counted |
| `vector2`, `vector3`, `color` | Value types. Declared with named parts: `type:vector3 x:0 y:1 z:0`, `type:color r:1 g:0 b:0 a:1` |
| `entity` | Handle to a game entity (never a raw pointer) |
| `list of <type>` | Ordered collection. A list in the header gets its starting items as indented `entry` lines (see below) |
| `timer` | Counts up in seconds from 0 once the file is created. Compare it to a duration (`SinceDamage is above 10 seconds`) and restart it with `reset`. |
| `gate` | Open or closed (`variable name:Door type:gate value:closed`; open if `value:` is left out). Change it with `open`, `close`, `toggle` and use it with `through` (4.3a). It can't be read as a value or set with `make`. |
| an enum or struct | The names from `enum` and `struct` declarations above it (3.2), and `hit`, what a trace gives (4.1c). |

Durations are written `<number> seconds` (or `1 second`) and only compare against a `timer`.

There is **no single `number` type**. `integer` and `decimal` are separate, and there are no implicit conversions. To convert, write `decimal of Health` or `integer of Speed` (phrase expressions, 4.1).

**One exception: whole-number literals.** Writing `0` or `5` where a `decimal` is expected is fine (`make Speed 2`, `x:0 y:1 z:0`). That's a literal, so there's no surprise. A *variable* that holds an integer is never converted automatically.

A list's starting items are indented under its declaration, one per line. `entry` is used here so it can't be confused with the `item N of` phrase:

```
variable name:SpawnPoints type:list of vector3
	entry x:0 y:0 z:0
	entry x:10 y:0 z:5
```

In a statement, a vector is written with the `vector` phrase: `make Target vector x:0 y:1 z:0`.

### 3.2 Enums, structs and tables

```
enum name:Mode values:Idle Chasing Attacking

struct name:Item
	field name:Name type:text
	field name:Cost type:integer
	field name:Stance type:Mode

table name:Items type:Item
	row key:"Sword" Name:"Sword" Cost:5 Stance:Attacking
	row key:"Shield" Name:"Shield" Cost:8
```

- **Enums.** `variable name:State type:Mode value:Chasing` starts as the first value when `value:` is left out. A value is written as its bare name (`make State Chasing`, `if State is Idle`), so enum values share the file's one list of names (6a): `Idle` can't also be a variable. Enums compare with `is` / `is not` (not `above`), print as the name of the value (`text of State`), and `choose` takes them (4.3a). They can't be exposed.
- **Structs.** A struct is declared above where it is used. Its fields can be any value type (not a timer or gate), including enums, vectors, lists and other structs declared earlier. A field can't be named `type`, `key` or `as`, or like a part of a vector (`X Y Z R G B A Pitch Yaw Roll`). A struct starts with every field at its default. Make one with `new Item Name:"Lance" Cost:5` (fields left out are the defaults). Read and change a field with a path, `Held.Cost`, `make Held.Cost 7`, `subtract 3 from Held.Cost`; a vector field goes one deeper (`Held.Spot.X`). Structs compare with `is`. A struct belongs to the script that declares it: another script can't call an action that takes or gives one, and `tell` can't carry one.
- **Tables.** `row "Sword" of Items` is the row with that key, as a struct: the fields a row gives, and the defaults for the rest. A key with no row gives the defaults and a warning. A row sets numbers, text, booleans and enum values.

## 4. Implement

### 4.1 Statements and expressions

| Pattern | Meaning |
|---|---|
| `make <target> <value>` | Assign |
| `add <value> to <target>` | `+=` (or list append when the target is a list) |
| `subtract <value> from <target>` | `-=` |
| `multiply <target> by <value>` | `*=` |
| `divide <target> by <value>` | `/=` |
| `remove <value> from <list>` | Remove from a list |
| `do <Action> key:value ...` | Call an action in this file |
| `do <Action> on <Thing> key:value ...` | Call an action on another file, system, or entity (`do ShowCoins on Hud count:Coins`) |
| `send <Event>` | Fire an event declared in `header` |
| `remember <Name> <value>` | Create a temporary value that lasts until the end of the current block (action, handler, or `if`/loop body). Its type comes from the value. Use `make` to change it afterwards. |
| `reset <timer>` | Set a `timer` back to 0 |
| `spawn <File> at:<value> as <Name>` | Create an entity with that file attached (**draft**) |
| `move <target> toward <value> by <value>` | Step a position toward a point (**draft**) |
| `move <entity> <direction> by <value>` | Move an entity relative to where it faces. Directions: `forward`, `back`, `left`, `right`, `up`, `down`. A negative amount goes the other way. |
| `print <value>` | Show a debug message (text, number, boolean, vector, or color). Join pieces with `joined with`. |
| `after <N> seconds:` | Run the indented block once, later (a delay). Local names are copied when it's scheduled, so the block still knows them. It can't `give back`. |
| `for <Name> from <A> to <B>:` | Count from A to B, including both ends. `Name` is an `integer` inside the loop. |
| `destroy <entity>` | Remove an entity |
| `give back <value>` | Return a value from an action |
| `finish` | Stop the current action or handler early |
| `stop loop` / `skip` | Leave a loop / go to the next iteration |
| `clear <list>` | Empty a list |
| `add <value> to <list>, if new` | Append only if the list doesn't hold it yet |
| `remove item <N> from <list>` | Remove by position (past the end: nothing, with a warning) |
| `damage <entity> by <value>` | Run the entity's `when damaged` handlers (4.5) |
| `attach <entity> to <entity>` / `detach <entity>` | The first follows the second (4.5a) |
| `tell <entity> to <Action> key:value ...` | Ask any entity to run an action by name; skipped quietly if it has none (4.4) |
| `only once:` / `first <N> times:` / `alternate:` / `through <Gate>:` / `choose ...:` / `animate ...:` and `open`, `close`, `toggle` | Flow with memory, switches and timelines (4.3a) |

**Value rule:** anywhere a `<value>` is expected, it is one of these fixed shapes. The parser always knows where a value ends, and it never needs parentheses.

1. **A single term:** a literal, a name, or a field path (`Transform.Loc.X`).
2. **A phrase expression:** a fixed phrase with a fixed number of single-term operands (table below).
3. **One operator:** `<term> plus <term>`, `minus`, `times`, `divided by`, `remainder after dividing by`. Only one operator per expression.
4. **One comparison:** `<term> is above <term>`, etc. (4.2), when a boolean value is wanted.

Anything bigger is split across lines with `remember`. A line should do one thing.

```
make Switch false                              // single term
make Health Health minus 5                     // one operator
add Speed per second to Transform.Loc.Z        // phrase expression
remember DistanceToPlayer distance from Self to Player
if WaypointIndex is count of Waypoints, then make WaypointIndex 0
```

#### Phrase expressions

| Phrase | Result |
|---|---|
| `time since start` | `decimal`. Seconds since the game started. |
| `<T> of the way from <A> to <B>` | Blend between two values: `0` gives A, `1` gives B, `0.5` the middle. A and B are both `decimal` or both vectors; T is a `decimal`. |
| `axis <name> of <System>` | `decimal` between -1 and 1: an analog input, like `axis moveForward of Input`. |
| `<value> per second` | The value scaled by this frame's time step, so movement is frame-rate independent. A number gives a `decimal`; a vector gives a vector (`add Velocity per second to Transform.Loc`). |
| `distance from <A> to <B>` | `decimal`. Operands are both `vector3` or both `entity` (distance between their `Transform.Loc`; compile error if either lacks a `Transform`). |
| `count of <list>` | `integer` |
| `item <N> of <list>` | the list's element type |
| `<text> joined with <value>` | `text`. Numbers and booleans are converted automatically. |
| `text of <value>` | `text` |
| `decimal of <integer>` / `integer of <decimal>` | explicit conversion |
| `absolute value of <value>` | same type |
| `result of <Action> key:value ...` | The returned value of an action that has `gives <type>`. Must be the last thing on its line. |
| `vector x:<n> y:<n> z:<n>` | `vector3` |
| `<A> if <condition> otherwise <B>` | One of two values of the same type, by a condition (Select). |
| `not <boolean>` | The other boolean |
| `format "Score: {Score} of {Best}"` | `text`, with each name or field in braces filled in (as in `joined with`) |
| `cast <entity> to <File>` | The entity if it runs that file, otherwise a missing entity (Cast To) |
| `new <Struct> key:value ...`, `row <key> of <Table>` | A struct, a row of a table (3.2) |
| `position of <item> in <list>` | `integer`: where it is (from 0), or -1 |
| `level name` | `text`: the level being played |

The number and vector phrases (4.1b below) are the same kind of thing.

**Limits:** `add`, `subtract`, and `make` take an optional suffix that keeps the result in range:

```
add HealingOrbAmount to Health, capped at MaxHealth     // result is at most MaxHealth
subtract amount from Health, floored at 0               // result is at least 0
```

`capped at` and `floored at` are fixed phrases after a comma, and each takes a single term.

### 4.1b Field paths

Engine components expose fields through a dotted path. Dots appear **only** here.

| Path | Meaning |
|---|---|
| `Transform.Loc` | Location (`vector3`) |
| `Transform.Rot` | Rotation (`vector3`, degrees) |
| `Transform.Scale` | Scale (`vector3`) |
| `Transform.Loc.X` | One axis. Axes are `X`, `Y`, `Z` |

- **Parts of a vector or color held by a name** use the same dot: `Velocity.X`, `Tint.A`, `make Velocity.Y 2.0`, `subtract 1 from Velocity.Z`. A vector has `X`, `Y`, `Z` (a 2D one `X`, `Y`); a rotation's three can also be called `Pitch`, `Yaw` and `Roll` (`Facing.Yaw` is `Facing.Y`; `Transform.Rot.Yaw` works too); a color has `R`, `G`, `B`, `A`. A struct's field reads the same way (3.2).
- A bare path always refers to `Self`: `add Speed per second to Transform.Loc.Z`.
- For another entity, add `of <entity>`: `make Target Transform.Loc of Player`.
- The component must be declared in the header (`needs component:Transform`).
- Besides `Transform`, `Visibility` has `Shown` (`make Visibility.Shown false` hides the entity) and `Collision` has `Enabled` (`make Collision.Enabled false` makes it non-solid, and a trace ignores it). Both are booleans and start true, and both reach another entity with `of`: `make Visibility.Shown of Door false`.

Because the component name is part of the path, two components may use the same field name without clashing.

### 4.1c Numbers, vectors and traces

Each of these is a fixed phrase with single terms in it, like the phrases in 4.1. A name that is also a phrase's first word (`length`, `map`) still works as a name.

| Phrase | Result |
|---|---|
| `clamp <V> between <Min> and <Max>` | The number, kept in range. All three are `integer`, or all `decimal` (whole-number literals fit a decimal) |
| `map <V> from <A> to <B> into <C> to <D>` | `decimal`: where V sits between A and B, as a place between C and D. Never outside C to D |
| `random decimal from <Min> to <Max>` / `random integer from <Min> to <Max>` | A random number (`integer` includes both ends). A world rolls the same numbers each run until it is seeded; the editor seeds from the clock |
| `normalized <V>` | The vector with length 1 (a zero vector stays zero) |
| `length of <V>` | `decimal` |
| `dot product of <A> and <B>` | `decimal` |
| `rotation pitch:<P> yaw:<Y> roll:<R>` | `vector3` in degrees (X, Y, Z) |
| `rotation looking from <A> to <B>` | The pitch and yaw that make forward (-Z) point from A at B (positions, or entities) |
| `forward of <entity>` / `right of <entity>` / `up of <entity>` | `vector3`: that direction, turned by where the entity faces |
| `ease <Current> toward <Target> at <Speed>` | Part of the way to the target this frame (a `decimal`, `vector2` or `vector3`): `Speed` times the frame's time, capped at all the way. Use it as `make X ease X toward Goal at 5.0` |
| `turn <Current> toward <Target> at <Speed>` | The same for a rotation (`vector3` degrees), taking the short way round |
| `trace from <A> to <B>` | A `hit`: what a line from A to B meets first (positions or entities). `sweep from <A> to <B> radius <R>` is the same with a thick line |

A `hit` has the fields `Blocked` (boolean), `Entity` (the one hit, or missing), `Location`, `Normal` and `Distance`; with no hit, `Location` is B and `Distance` the whole length. Until the engine reports real shapes, every entity that has collision on counts as a ball half as wide as its largest scale; the one that traces is skipped.

### 4.2 Conditions

One-line form:

```
if Switch is true, then make Switch false
```

Block form:

```
if Health is at most 0:
	send Died
otherwise if Health is below 20:
	do ShowWarning on Hud
otherwise:
	finish
```

Comparison phrases (fixed list):

| Phrase | Meaning |
|---|---|
| `is` / `is not` | equal / not equal |
| `is above` / `is below` | `>` / `<` |
| `is at least` / `is at most` | `>=` / `<=` |
| `<entity> is missing` / `is present` | The entity doesn't exist (never found, or destroyed) / exists. `is not missing` is an error: write `is present`. |
| `<System> is pressing <input>` / `is not pressing` | Held down this frame: `Input is pressing moveForward` |
| `<System> starts pressing <input>` / `stops pressing` | Went down / came up this frame: `Input starts pressing jump` |

| `<list> contains <value>` / `<list> does not contain <value>` | The list holds it (or doesn't) |

For booleans, `is true` and `is false` are the idiomatic forms. `if Switch:` is **not** allowed. Being explicit costs 8 characters and is worth it.

**Combining conditions:** `and` and `or` can be used in one condition, but **not together**. To mix them, name one part with `remember`.

```
if A is true and B is true, then ...           // ok
if A is true or B is true or C is true, ...    // ok
if A is true and B is true or C is true, ...   // compile error: ambiguous

remember Ready A is true and B is true          // fix: split the line
if Ready is true or C is true, then ...
```

### 4.3 Loops

```
repeat 3 times:
	do Flash on Hud

for each Foe in Alive:
	do TakeDamage on Foe amount:1

while Health is above 0:
	...

for Index from 0 to 4:
	remember Spacing decimal of Index
	...
```

`for ... from ... to ...` counts up by one and includes both ends, so `from 0 to 4` runs five times. Use `repeat` when you don't need the number.

Loop bounds are known from the header or from a literal wherever possible, so the compiler can unroll and check them.

### 4.3a Flow with memory, switches and timelines

```
only once:                       // runs the first time it is reached
	...
first 3 times as Warmup:         // runs the first three times
	...
alternate:                       // runs the first block, then the second, then the first...
	...
otherwise:
	...
through Door:                    // runs only while the gate is open
	...
choose Lives:                    // one block, by value (a whole number, text, boolean or enum)
	case 0:
		...
	case 1 or 2:
		...
	otherwise:
		...
animate Lift from 0.0 to 4.0 over 2 seconds:
	make Transform.Loc.Y Lift
```

- `only once`, `first N times` and `alternate` remember per entity and per statement. `as Name` (optional) names one, and `reset Name` (like `reset` of a timer) starts it over. Names are file-wide and share the one list of names (6a).
- A gate is a variable of type `gate`: `open Door`, `close Door`, `toggle Door`, and `through Door:` for its block. It starts open unless declared `value:closed`.
- `choose` runs the first case that matches, or `otherwise`, or nothing. Case values are fixed (numbers, text, true/false, enum values) and can't repeat. Several go in one case with `or`.
- `animate` runs its block every frame for that time, starting with the next frame: the first frame has the value `Progress/Duration` of the way along, and the last frame is with the value at the end, then it stops. The name holds a `decimal` (or a vector when both ends are). Like `after`, it runs on its own: it copies the names it can see, can't `give back`, and is dropped if the entity is destroyed first.

### 4.4 Actions (functions)

```
action TakeDamage amount:integer:
	subtract amount from Health

action IsAlive gives boolean:
	give back Health is above 0
```

- Parameters use the same `name:type` shape as the header. Multiple parameters are space-separated.
- Calls use `key:value`, and parameters can't be positional: `do TakeDamage amount:10`.
- An action with `gives <type>` can be used as a value: `make Alive result of IsAlive`.
- `tell <entity> to <Action> key:value ...` finds the action by name on whatever script the entity runs, with no need to know which: if it has none, nothing happens (not even a warning), which is what makes it work like an interface between scripts that don't know each other. Its arguments are numbers, text, booleans, vectors, colors or entities; they are matched to the action's parameters by name, and ones left out are defaults.
- Actions elsewhere are called with `on`: `do TakeDamage on Player amount:10`. The name after `on` is the `as` name from a `needs file`, a `needs system`, or a `needs entity`.

### 4.5 Events and time

Handlers sit directly in `implement`. They are what the engine calls; nothing else runs. **Statements can't sit directly under `implement:`**: every line of code has to say when it runs, and the compiler tells you so (see 8, decision 13).

| Handler | Fires |
|---|---|
| `when created:` | Once, when the entity is spawned |
| `when destroyed:` | Once, when the entity is removed |
| `every frame:` | Every frame |
| `every 2 seconds:` | On a timer |
| `when Self touches <File> as <Name>:` | Overlap begins with an entity that has that file attached |
| `when Self stops touching <File> as <Name>:` | That overlap ends |
| `when <Variable> changes:` | Any time the variable's value is different from the last time this handler looked (see 4.5a) |
| `when <System> starts pressing <input>:` / `stops pressing` | Once, in the frame an input goes down / comes up (`when Input starts pressing jump:`) |
| `when <Event>:` | An event declared in this file's header |
| `when <File> sends <Event> as <Name>:` | An event sent by an entity running another file |
| `when damaged as <Amount> from <Source>:` | `damage Self by ...` was done. `Amount` is a `decimal`; `from <Source>` (optional) names the entity that did it |
| `when Self hits <File> as <Name>:` | The engine reports a blocking collision with an entity that runs that file |

### 4.5a How code runs

- **Each frame**, blocks whose `after` delay is up run first, in the order they were due. Then entities run in the order they were created. For each entity, its input handlers and `every frame:` handlers run, and then any `every N seconds:` handler that is due, in the order they appear in the file. An `every N seconds:` handler runs at most once per frame, and its first run is N seconds after the entity is created.
- **`after N seconds:`** blocks are tied to the entity that scheduled them: if it is destroyed first, the block is dropped. The block sees a *copy* of the local names from when it was scheduled, so changing them inside doesn't affect the code that scheduled it.
- **`when <Variable> changes:`** is checked after each handler, action call, or event delivery finishes (not in the middle of one), so a handler that sets a variable three times runs the watcher once, with the final value. A change made by the watcher itself can trigger it again; this is cut off after 64 rounds.
- **Directions** follow the engine: `forward` is -Z, `right` is +X, `up` is +Y, turned by the entity's rotation (`Transform.Rot` as Euler degrees, applied like `glm::quat(radians(Rot))`).
- **Events** are queued by `send` and delivered after the current handler finishes, first-in first-out. A delivered event runs the sender's own `when <Event>:` handlers (if the sender still exists) and every `when <File> sends <Event> as <Name>:` handler on other entities. The sender being destroyed in the meantime doesn't stop delivery. Events that keep triggering each other are cut off after 10,000 deliveries.
- **`spawn`** creates the entity and runs its `when created:` handlers before the next statement. **`destroy`** runs its `when destroyed:` handlers, then removes it. The rest of the current handler still runs.
- **Collisions** (`when Self touches ...`, `when Self hits ...`) are reported by the engine. Both entities' handlers run.
- **`damage`** runs the target's `when damaged` handlers right away, like an action call, and the rest of the handler that did it carries on.
- **`attach A to B`** keeps A where it is now, measured from B (turned with B), and from then on A follows B: after everything else in a frame has run, A's location is B's plus that offset turned by B's rotation, and its rotation is B's plus the difference it had. So moving A by hand while it is attached has no lasting effect: `detach` it first. Detaching, or B being destroyed, leaves A where it is. Two entities can't follow each other.
- **`animate`** blocks run after the `after` blocks that are due, and before the entities' handlers.
- **Random numbers** come from one generator per world (xorshift64*), the same in the interpreter and the generated C++.
- **Timers** start at 0 when the entity is created. A timer compared with `above 10 seconds` is true only *after* 10 seconds have passed, not at exactly 10.
- **Safety limits:** a loop stops after 100,000 turns, and actions calling actions stop at 64 levels deep. Both log a warning (4.6).

### 4.6 When things go wrong

Scripts never throw and never crash the game. Mistakes that can be found early are errors; the rest are skipped and reported.

**Caught before the game runs (compile errors):** unknown names, type mismatches, a `needs file` that doesn't exist, dividing by a literal `0`, and a `needs component` or `needs system` the engine doesn't offer.

**At runtime, the script skips and says so:**

| Situation | What happens |
|---|---|
| An action, field read, or `destroy` on a **missing** entity | Skipped. Reads give the type's default value (`0`, `false`, ...). |
| Dividing by zero (value only known at runtime) | The result is `0`. |
| `item N of` a list when `N` is out of range | The type's default value. |

In development builds each of these logs a warning naming the file and line. In shipped builds they stay silent. Scripts guard against missing entities with `if Player is missing, then finish`.

## 5. Built-ins

Always available, no header entry needed: `Self`.

**`Self`** always refers to the file the code is running in, i.e. the entity this file is attached to (`destroy Self`, `distance from Self to Player`, `when Self touches Coin as Pickup:`). It is a reserved name: it can't be declared as a variable, constant, action, event, timer, or `as` name, and it means the same thing everywhere it appears.

Engine subsystems, such as `Input`, `Audio`, `UI`, `Animation` and `Game`, must be declared with `needs system:` so they appear in the header.

A system action may take **optional arguments**, which a call can leave out:

| System | Actions |
|---|---|
| `Audio` | `Play sound:` and optionally `volume:`, `at:` (a place), `attachedTo:` (an entity), `loop:`; `Stop sound:` |
| `UI` | `SetText element: text:`, `Show element:`, `Hide element:`, `Remove element:`, `Create widget:`, `Play animation:` |
| `Animation` | `Play animation:` (optionally `target:`, `loop:`, `rate:`), `Stop animation:` (optionally `target:`), `PlayMontage montage:` (optionally `section:`, `rate:`, `target:`) |
| `Game` | `OpenLevel level:`, `SetPaused paused:` |

The editor connects `Game.SetPaused` (the scripts stop advancing); the rest are written to the console until the engine has those systems.

## 6. Ambiguity rules

Predictability is a design goal, so these rules are part of the spec:

1. **Fixed sentence patterns.** Anything that doesn't match a pattern is an error. Errors list the closest patterns, e.g. `I expected one of: make <target> <value> / add <value> to <target>`.
2. **Values have fixed shapes** (4.1), with no parentheses.
3. **No mixing `and`/`or`** in one condition (4.2).
4. **No implicit type conversion** (3.1), except `joined with`, which is explicit by its name.
5. **No optional filler words.** A word is always required or never allowed, so `make X be 5` is an error, not an alias.
6. **Names can't collide with keywords** and can't be a reserved word (`make`, `if`, `is`, `then`, `every`, `missing`, `present`, `Self`, ...).
6a. **Names are unique within a file** across variables, constants, timers, gates, events, actions, remembered values, the names given to `only once` and friends, enum names and their values, struct names and table names. An action and a constant can't share a name, and an enum value can't also be a variable.
7. **One way to do each thing.** Aliases are added only when real use shows a need.
8. **Dots only in field paths** (2, 4.1b).

## 7. Performance design

| Idea | Effect |
|---|---|
| Header declares all dependencies | The compiler builds the full dependency graph without running code. Files load lazily (`when:used`). |
| Static types, no `number` | Values are fixed-size and unboxed. |
| Name resolution at compile time | Variables become slot indices. There are no string lookups at runtime. |
| Every access goes through the header | The compiler knows which components each handler reads and writes, so independent handlers can be **scheduled in parallel** (ECS-style) with no locks. |
| No garbage collector | Value types, entity handles, and ref-counted text. No pauses. |
| `every frame` allocation warning | The compiler warns when a per-frame handler creates `text` or other heap values. |

Execution plan (each stage is optional until the previous one works):
1. Tree-walking interpreter, to settle the language.
2. Typed IR, then **bytecode VM** with hot reload (the dev workflow).
3. **AOT to Rust or native** for shipped builds, if the VM becomes a bottleneck.

## 8. Design decisions

Questions raised while drafting, and how each was settled. The tiebreaker was always the goals in section 1: plain English first, and one predictable reading per line.

1. **Multi-word names: no.** Names are `PascalCase` or `camelCase` with no spaces. A name with spaces would make every sentence ambiguous (is `Player Health` one name or two words?). The `of` and `on` phrases already make lines read well (`Health of Player`-style readability comes from the phrases, not from spaces in names).
2. **Collections.** A list is `list of <type>`. Operations: `count of X`, `item N of X` (from 0), `add V to X` (append), `remove V from X`, and `for each Name in X:`. `add` is shared with arithmetic on purpose: it reads correctly both ways, and the type of the target decides. The compiler's error names which one it expected. Lists are ordered and may hold duplicates.
3. **Engine interface.** The engine registers its components, systems, fields, actions, and input names through Rust traits. A build step writes them to a manifest file (`engine.forgeapi`). The compiler, the checker, and editor autocomplete read the manifest, so none of them need to link the engine. `needs component:` / `needs system:` are checked against it, which is why an unknown one is a compile error.
4. **Errors.** Scripts never throw: see 4.6. Early errors where possible, skip and warn at runtime.
5. **Tooling.** The sentence-pattern table is the single source of truth for the parser, the autocomplete/LSP, and the docs. Today the parser has its pattern list inline; moving it into one shared table is part of the formatter/LSP work.
6. **Remembered names may not shadow header names.** Strict is more readable: a name means one thing in a file (rule 6a).
7. **No `min` / `max`.** `capped at` and `floored at` cover clamping, and `remember` plus an `if` covers the rest. Revisit only if real code keeps needing it.
8. **Entity references.** An entity handle comes from one of four places, and every one is visible in the file:
   - `needs entity:Name tag:"..."`: the one entity with that tag. If there isn't one, it's *missing*. If there are several, it's a development-build error (use `needs entities`).
   - `needs entities:Name tag:"..."`: a live list of everything with the tag.
   - `spawn ... as Name`: the entity you just created.
   - `when ... as Name:`: the entity involved in an event.
   Looking entities up by scene name is not supported: names change, tags are deliberate.
9. **Rotation.** `Transform.Rot` is three axes in degrees (`Transform.Rot.Y`). The engine stores a quaternion internally and converts, so scripts never see gimbal-lock problems. Setting one axis at a time is fine for simple spinning and turning; richer rotation will get its own statements (like `face <target>`) rather than exposing the quaternion.
10. **Total time.** `time since start` (seconds). Frame time is never needed directly: `per second` covers it.
11. **Language statements vs system actions.** A few things every game needs are language statements: `spawn`, `destroy`, `move` (toward a point, or in a direction), `print`, and `send`. Everything else is a system action called with `do ... on <System>` (`SetText`, `Play`, ...). The test: does it create, remove, or move entities? Then it's a statement. Otherwise it belongs to a system.
12. **Input.** Systems may add comparison phrases through the manifest. `Input` provides `is pressing` (held), `starts pressing` (went down this frame), and `stops pressing` (came up this frame), plus `is not pressing`.
13. **Bare statements under `implement:` are not allowed.** The tempting reading, "a bare line runs every frame", is a trap for newcomers: `make Health 100` would quietly reset health sixty times a second. Instead the compiler says "code directly under `implement:` has to say when it runs" and names the handlers. `every frame:` is already plain English.

### Still open

- **Fixed-step updates:** `every physics step:` as a handler, for physics-driven code.
- **Manifest format:** the exact layout of `engine.forgeapi`.
- **Text formatting:** decimals and padding in `joined with`.
- **Ordering:** which handlers run before others when two touch the same data (the compiler can see it, but the rule isn't written down).
- **Optional parameters** on system actions, for things like `Play Sound` volume and 3D position.
- **Spawning from an object asset** (`.fobj`) instead of a script file (see 10).
- **Physics:** forces and raycasts wait for the engine's physics module.

## 9. Implementation roadmap

Status: steps 1 and 2 are done. The hand-written lexer and parser, the name checks, and the type checker all live in `forge/` with no dependencies. `forgec file.forge` loads the file and everything it needs (`forge/src/project.rs`), then checks all of it: syntax, names, and types, including calls across files. The engine is described by `Manifest::builtin()` until `engine.forgeapi` exists. `cargo test` covers every example plus the files in `forge/tests/fail/` that must be rejected, each with a specific message (89 of them now). Step 3 is done too: a tree-walking interpreter in `forge/src/runtime/` runs checked files against a `Host` (the seam to the real engine; `StubHost` records calls and takes input by hand). `forgec run file.forge --frames N --press <input>` runs a file and prints what it did. `forge/tests/runtime.rs` runs the example files through 13 scenarios (movement, coins, damage and respawn, regeneration timing, enemy chase and attack, the spawner). `behaviors/` holds one file per node or example behavior from the engine's design doc, and `forge/tests/behaviors.rs` runs them through 15 scenarios (see 10). The node kits (`behaviors/*_kit.forge`, one action per node taken from `Documentation/NODELIST.md`) are run by `forge/tests/nodelist.rs` and `nodelist_types.rs`, and by `forge/tests/graph_nodes.rs` through the graph (see 10). `forge/src/format.rs`, `forge/src/nodes.rs`, and the `fmt` and `nodes` commands are covered in section 10.

1. Lexer (indentation-aware) and parser from the pattern table. Rust, hand-written, so error messages stay under our control. **Done.**
2. Type checker and header resolution (the dependency graph). **Done** for every construct in this spec. Not yet checked: that every path through an action ends in `give back`, and unused declarations.
3. Tree-walking interpreter and a test suite built from the `examples/` files. **Done.**
4. **C++ generator.** ForgeEngine is C++ and its design doc plans readable generated classes (`FG_Player.generated.cpp` with `OnBeginPlay` / `OnTick`). ForgeScript compiles to that style. The Rust interpreter stays as the reference for what each construct means. **Done** (see 11): every example and behavior generates, compiles against the engine's real headers, and agrees with the interpreter. The engine side (play mode, a `Host`, a script registry) is not built.
5. Comment-preserving parse, formatter, and the node ↔ sentence table (see 10). **Done.** Then the language server and VS Code extension. **Done** (see 13).
6. Optional: bytecode VM and hot reload for the editor.

## 10. Nodes and text

ForgeEngine's visual scripting is meant to be editable from both sides: the graph produces text, a programmer edits the text, and the graph follows (engine design doc, section 6.10). ForgeScript is that text. Four rules make it work:

1. **One node, one sentence.** A node produces exactly one statement, one handler, or one phrase. Nodes that follow a `Delay` nest inside its `after` block.
2. **A marker comment names the node.** A line like `// node 6: Move Object` goes right before the text that node produced: `node`, an id, a colon, and the node's name from the table below, optionally followed by `[a detail for people]`. A sentence that implements several nodes (a `Lerp` feeding a `Set Location`) gets one marker line per node. The language ignores markers; the tools read them.
3. **A marker is checked against its sentence.** If the sentence below a marker no longer implements that node, the node was edited by hand, and the tool says so (the engine design doc's "Modified" badge, 6.10.2). Changing what a sentence *says* without changing which node it is, like `Transform.Rot.Y` to `Transform.Rot.X`, is not a modification: the node just has a new pin value.
4. **Hand-written text is still checked.** A sentence with no marker is custom code: an ordinary statement, shown as such. Name and type checks apply to all text equally.

### 10.1 The formatter

`forgec fmt` prints a file in one canonical style: tabs, one space between words, `// ` after comment slashes, trailing comments kept on their line, at most one blank line in a row. It never changes what a file means and never loses a comment: the output parses to the same tree, and formatting twice gives the same text (both are tested over every example and behavior). A comment in a place the formatter can't keep (above `otherwise:` or above an `entry` line) makes it refuse the file, pointing at the line, instead of dropping it. This is what lets the editor rewrite a file after a graph change without disturbing a programmer's own comments.

### 10.2 The node table and checks

`forge/src/nodes.rs` holds the table: 117 nodes, each with a name, a category, its sentence pattern, and its input pins. Every entry carries sample sentences, and a test parses each one and checks that it classifies as that node with those pins, so the table, the parser, and the classifier can't drift apart.

- **From text to nodes:** `analyze` reads every marker and says whether it still matches (`Matches`, `Modified`, `UnknownNode`, or `DuplicateId`), with each matching node's pins as ForgeScript text. Sentences with no marker are listed as custom code with the nodes they would become.
- **From nodes to text** is the editor's side. It fills a node's pattern with its pins and writes the marker above it. `forgec nodes --table --json` gives it the table.
- `behaviors/door.forge` is the reference for editor output: every sentence is marked, and a test keeps it that way.

### 10.3 Commands

```
forgec fmt <file>... [--check | --write]   # print, verify, or rewrite in the canonical style
forgec nodes <file> [--json]               # markers vs. text; exits 1 if any node is modified
forgec nodes --table [--json]              # the node table
forgec cpp <file> --out <dir>              # generate C++ (see 11)
```

### Coverage of the engine's planned nodes (design doc 6.4)

Every node in the list has a sentence, tested by a file in `behaviors/`, except the gaps below.

| Node | ForgeScript |
|---|---|
| On Begin Play | `when created:` |
| On Tick (and interval) | `every frame:` / `every N seconds:` |
| When Key / Action Pressed | `when Input starts pressing jump:` |
| On Overlap Begin / End | `when Self touches <File> as <Name>:` / `when Self stops touching <File> as <Name>:` |
| On Variable Changed | `when Score changes:` |
| Move Object | `move Self forward by Speed per second` |
| Rotate Object | `add TurnSpeed per second to Transform.Rot.Y` |
| Set Location / Rotation / Scale | `make Transform.Loc ...` |
| Spawn Object | `spawn Coin at:vector x:0 y:1 z:0 as Reward` |
| Destroy Object | `destroy Self` |
| Play Sound | `do Play on Audio sound:"jump.wav"` |
| Set / Get Variable | `make Health 100` / the name |
| Branch | `if ...:` and `otherwise:` |
| Sequence | statements, top to bottom |
| Loop | `for Index from 0 to 4:`, `repeat 3 times:`, `for each Foe in Foes:` |
| Math, Clamp | arithmetic, `capped at`, `floored at` |
| Lerp | `Progress of the way from A to B` |
| Vector add / subtract / scale | `A plus B`, `A times 2.0` |
| Print Debug Message | `print "Health is " joined with Health` |
| Delay | `after 2 seconds:` |
| Get Axis | `axis moveForward of Input` |
| Exposed variables | `exposed:true` |

**Gaps:**
- **Spawn Object** takes a *script file* today. The engine spawns `.fobj` object assets that have a script attached, so spawning needs a way to name an asset.
- **Print**'s duration and screen position.
- **Apply Force** waits for the engine's physics module. Traces, damage and hits exist in the language, but the engine doesn't report real shapes or collisions yet (see the table below).

### Coverage of `Documentation/NODELIST.md`

The list of Unreal Engine nodes the engine wants to take inspiration from, and what each is here (names follow ForgeScript's own words). `behaviors/*_kit.forge` has one action for each new node, and `forge/tests/nodelist*.rs` and `cpp/tests/differential_kits.cpp` run them against the interpreter and the generated C++.

| NODELIST | ForgeScript |
|---|---|
| Event BeginPlay, Event Tick, Custom Event | `when created:`, `every frame:`, `event` / `send` / `when <Event>:` (and `action`) |
| Branch, Sequence | `if` / `otherwise`; statements run top to bottom |
| Do Once, Do N | `only once:`, `first N times:` (`as Name` and `reset Name` to run again) |
| Gate | a `gate` variable, `open` / `close` / `toggle`, `through Door:` |
| FlipFlop | `alternate:` / `otherwise:` |
| Delay, Set Timer by Event | `after N seconds:`; `every N seconds:` (a repeating timer that can be stopped is a boolean checked inside it) |
| For Loop, For Each, While, Break | `for X from A to B:`, `for each`, `while`, `stop loop` (and `skip`) |
| AND, OR, NOT, ==, !=, >, < | `and`, `or`, `not <boolean>`, `is`, `is not`, `is above` / `below` / `at least` / `at most` |
| Select | `<A> if <condition> otherwise <B>` |
| Switch on Int, Switch on Enum | `choose <value>:` with `case` and `otherwise` (whole numbers, text, booleans, enums) |
| Get / Set Variable, Increment / Decrement Int | the name, `make`, `add 1 to`, `subtract 1 from` |
| Clamp, Map Range Clamped, Lerp, Random Float / Int | `clamp`, `map`, `<T> of the way from A to B`, `random decimal` / `random integer` |
| Make / Break Vector, Vector math, Normalize, Length, Distance, Dot | `vector x: y: z:`, `Velocity.X`, `plus` / `minus` / `times`, `normalized`, `length of`, `distance from`, `dot product of` |
| Make Rotator, Find Look at Rotation | `rotation pitch: yaw: roll:`, `rotation looking from A to B` (and `Facing.Yaw` to break one) |
| Get / Set Actor Location, Rotation, Transform | `Transform.Loc`, `Transform.Rot`, `Transform.Scale` (each, or an axis); `of <entity>` for another entity |
| Add Actor World / Local Offset, Add Actor World Rotation | `add V to Transform.Loc`, `move Self forward by X`, `add V to Transform.Rot` |
| Get Forward / Right Vector | `forward of Self`, `right of Self` (and `up of`) |
| Spawn Actor from Class, Destroy Actor | `spawn`, `destroy` |
| Get Actor of Class, Get All Actors of Class, Get Player Character / Controller | `needs entity` / `needs entities` with a tag |
| Cast To, Is Valid | `cast <entity> to <File>`, `is present` / `is missing` |
| Get Component by Class | `needs component:` (a component is declared, not searched for) |
| Attach Actor to Component, Detach from Actor | `attach A to B`, `detach A` |
| Set Visibility, Set Hidden in Game, Set Actor Enable Collision | `make Visibility.Shown false`, `make Collision.Enabled false` |
| On Component Begin / End Overlap, Event Hit | `when Self touches ...`, `when Self stops touching ...`, `when Self hits ...` |
| Line Trace, Sphere Trace, Break Hit Result | `trace from A to B`, `sweep from A to B radius R`, and the fields of the `hit` (`Last.Entity`) |
| Apply Damage, Event AnyDamage | `damage Foe by 5.0`, `when damaged as Amount from Attacker:` |
| Play Sound at Location, Spawn Sound Attached | `do Play on Audio sound:"x.wav" at:Place` / `attachedTo:Self` |
| Play Animation, Play Montage | `do Play on Animation animation:"walk"`, `do PlayMontage on Animation montage:"attack"` |
| Create Widget, Add to Viewport, Remove from Parent, Set Text | `do Create on UI widget:`, `do Show on UI element:`, `do Remove on UI element:`, `do SetText on UI element: text:` |
| Print String, Format Text, To String | `print`, `format "Score: {Score}"`, `text of` |
| Array Add, Remove, Get, Length, Contains, Find, Clear, Add Unique | `add X to L`, `remove X from L` / `remove item N from L`, `item N of L`, `count of L`, `L contains X`, `position of X in L`, `clear L`, `add X to L, if new` |
| Get Data Table Row, Make / Break Struct, Set Members in Struct | `table` + `row "Key" of Items`; `struct` + `new Item Cost:5`; `Held.Cost`; `make Held.Cost 7` |
| Bind Event to Dispatcher, Call Event Dispatcher | `event` / `send`, and `when <File> sends <Event> as <Name>:` (bound for as long as the script runs) |
| Blueprint Interface Message | `tell <entity> to <Action> key:value` |
| Get Game Mode / State / Instance | a script with `when:startup` that every other script reaches with `needs file` (one entity, created when the first one starts) |
| Open Level, Get Current Level Name, Set Game Paused | `do OpenLevel on Game level:`, `level name`, `do SetPaused on Game paused:true` |
| Timeline | `animate Lift from 0.0 to 4.0 over 2 seconds:` (a straight line; `ease` gives a curve when you want one) |
| FInterp To, VInterp To, RInterp To | `ease X toward Goal at Speed` (decimal or vector), `turn X toward Goal at Speed` (rotation) |

**What isn't connected to the engine yet.** The editor runs all of this through the interpreter, but it has no physics or audio/UI/animation systems and no levels, so: `damage` and `tell` work between scripts; `trace` uses the ball approximation above; `when Self hits` is only run when the engine calls `forge_hit` (it doesn't yet, like overlaps); `Visibility.Shown` hides the object in the viewport and `Collision.Enabled` is kept for traces and a future collision system; `Game.SetPaused` pauses the scripts; the other system calls are written to the console (`Audio`, `UI`, `Animation`, `Game.OpenLevel`).

## 11. The C++ generator

ForgeEngine is C++, and its design doc plans scripts that compile to readable generated classes. `forgec cpp <file.forge> --out <dir>` checks a file and everything it needs, then writes one class per file:

```
FG_Door.generated.h     the class: variables, actions, and the entry points the World calls
FG_Door.generated.cpp   the handler bodies
ForgeScriptRuntime.h    the small runtime generated code stands on (cpp/ForgeScriptRuntime.h)
```

A class is named from its file (`game_state.forge` becomes `FG_GameState`). Two files that would get the same class name are refused. Node markers (section 10) are kept as the engine doc's `// ─── Node: "Lerp" (ID: 13) ───` comments, so the planned code view can jump between code and nodes.

| ForgeScript | Generated C++ |
|---|---|
| `variable` / `constant` | a member / `static constexpr`. `exposed:true` adds a case to `SetExposed` |
| `tag name:"player"` | `Tags()` |
| `needs entity` / `needs entities` | an accessor, `Object* Player()`, that looks the entity up by tag |
| `when created:` / `destroyed:` | `OnBeginPlay` / `OnDestroy` |
| `every frame:`, `every N seconds:`, `when Input starts pressing ...:` | `OnTick`, in file order |
| `when Self touches File as Name:` | `OnOverlapBegin`, checked with `world->As<FG_File>(other)` |
| `when File sends Event as Name:` | `OnEventFrom`, checked with `dynamic_cast` |
| `when Variable changes:` | `FlushChanges`, which compares with a saved `Last_Variable` |
| `action` | a public method |
| `do Action on Other` | `if (auto* callee = world->As<FG_Other>(x)) callee->Action(...); else world->Warn(...)` |
| `do Action on System` | `world->host().Call("System", "Action", {...})` |
| `after N seconds:` | `world->After(this, N, [=, this]() mutable { ... })`, which copies the locals |
| `Transform.Loc` / `Rot` / `Scale` | `self->getTransform()->position` / `rotation` / `scale` |
| `move Self forward by X` | `world->MoveRelative(self, glm::vec3(0, 0, -1), X)` |
| `only once` / `first N times` / `alternate` | a counter member per statement (`Latch_<line>_`); `reset Name` clears it |
| `gate` variable, `open` / `close` / `toggle` / `through` | a `bool` member, set and tested directly |
| `choose` | an `if` / `else if` chain on a copy of the value |
| `animate` | `world->Animate(this, from, to, seconds, [=, this](auto Name) mutable { ... })`, run each frame by `World::Step` |
| `damage`, `when damaged`, `tell` | `world->Damage`, `OnDamaged(amount, source)`, `world->Tell` finding the action in a generated `Message(name, args)` |
| `attach` / `detach`, `Visibility.Shown`, `Collision.Enabled` | `world->Attach` / `Detach` (followed at the end of `Step`), `SetShown`, `SetCollision` |
| `clamp`, `map`, `normalized`, ... | the C++ template of its entry in `forge/src/phrases.rs` (`Clamp`, `MapRange`, `world->RandomInteger`, `world->Ease`, ...) |
| `enum` / `struct` / `table` | a nested `enum class`, a nested `struct` with `==`, and a `Row_<Table>(key)` member; the text of an enum is a `ToText` overload beside the class |
| `trace` / `sweep` | `world->Trace(self, from, to, radius)`, giving a `HitResult` |

Writing `Transform.Rot` is always followed by `updateQuaternion()`, because the engine's `TransformComponent` keeps a separate quaternion that `translateLocal` and `getForward` use. Any engine code that changes `rotation` directly must do the same.

### The runtime

`cpp/ForgeScriptRuntime.h` is header-only and needs only the engine's `Object`/`Component` headers and glm. It has four parts:

- **`Host`**, which the engine implements: `Pressing`, `Started`, `Stopped`, `Axis`, `Call` (for `do ... on Audio`/`UI`), and `Print`.
- **`World`**, which runs the scripts: `Spawn<T>`, `Destroy`, `Step(dt)`, `Touch`/`Untouch` (the engine reports overlaps), and the queues for events and `after` blocks. Its rules are exactly section 4.5a.
- **`ScriptBehavior`**, the base class of every generated script.
- **`ScriptComponent`**, which attaches a behavior to an engine `Object` (`ComponentType::Script`).

### How we know it's right

The Rust interpreter is the reference for what every construct means. `cpp/tests/` holds three C++ programs that run the same scenarios as `forge/tests/behaviors.rs`, `runtime.rs` and `nodelist*.rs` against the generated classes: 55, 35 and 101 checks (the last one includes the random numbers: a seed gives the same rolls in both). `cargo test` generates the code, compiles it with MSVC against **the real ForgeEngine headers**, and runs it. It skips quietly if Visual Studio or the engine's headers can't be found (`VCVARS64` and `FORGE_ENGINE_DIR` override the search). Only MSVC has been tried.

### Not done yet

- **Most of the engine side.** The editor runs scripts with the interpreter through `forge-ffi` (see `cpp/README.md`): a Play/Stop menu item, keyboard input, scripts picked per Object asset from the project's `Scripts/` folder, and no rebuild when a script changes. Still missing: audio, UI and animation systems (their calls are logged; `Game.SetPaused` and `Visibility.Shown` are connected), physics (`forge_touch` and `forge_hit` exist but the engine never calls them, and a trace uses balls instead of real shapes), levels, mouse look, and objects that scripts spawn showing up in the viewport. (Scripts can be edited as a graph or as text; see section 12.)
- **Generated C++ in a packaged game.** `forgec cpp` and the registry exist and are tested against the interpreter, but nothing packages a game with them yet.
- **Number formatting** in `joined with` uses the shortest exact form (`std::to_chars`), like the interpreter. They agree on the tested values, and very large or very small numbers may print differently.

## 12. The script graph

A script can be shown, and edited, as a node graph (`forge/src/graph.rs`, used by the editor through `forge-ffi`). **The text is always the source of truth.** The graph is built from the parsed file, and every edit made in it is turned back into ForgeScript, checked by the real parser and checker, and only then saved. An edit that can't be turned into valid ForgeScript is refused with the reason, and the file is not touched (an edit that only leaves something undeclared or unplugged is allowed, and Compile reports it). Comments stay with their lines (the file is rewritten with the formatter), and editing the file in a text editor updates the graph.

### What the nodes are

- **Events** (`On Tick`, `On Overlap Begin`, ...) are handlers. They have one execution output, `Run`.
- **Actions** are statements. Each has an execution input, a `Next` output, and a text field per input pin.
- **Blocks** (`Branch`, `Loop`, `Repeat`, `For Each`, `While`, `Delay`, `Do Once`, `Do N`, `Flip Flop`, `Gate`, `Switch`, `Timeline`) are statements with an output for each body (`Then`, `Else if`, `Otherwise`, `Body`, `Later`, `Do`, `A` / `B`, `Open`, `Case 1` ..., `Update`) and a `Next`.
- **Value nodes** (`Per Second`, `Distance`, `Compare`, ...) appear when a pin's text is a phrase and not just a name or number. They are wired into the pin. The pin's text stays the single source; the value node is a view of it.

A pin that holds a choice (`Operation`, `Direction`, `Operator`, `State`, `Check`, `Match`) is a list. Changing `Operation` from `add` to `subtract` changes the sentence. (The arguments of a call, and the fields of a `Make Struct`, are named by the action or struct, so they are always plain values even when they are called `State` or `Check`.)

### Edits

Each operation rebuilds the affected sentence from the pins, parses it, and puts it back: `set_pin`, `add` (a node from the table, with a default that is valid in the script, or the reason it can't be: "declare a timer variable first"), `delete`, `move` (reorder, or move into a block), `plug` (compute a pin with a value node) and `unplug`, and `add_branch` / `remove_branch` for `otherwise if` and `otherwise` (on a Switch node: `case` and `otherwise`). Adding a node that needs `needs system:Audio` adds it to the header. Anything can be deleted: a block left empty is written `do nothing` (and a script with no handlers likewise, under `implement:`). `set_positions` records where nodes are (see below) and doesn't touch the script. `set_source` replaces the whole text (what typing in the editor's code pane does): it is written as given, not re-formatted, and must parse, but it may replace a script that doesn't parse, which is how a broken file is fixed from the pane. `forge_graph_check_source` asks the same question of a text without touching any file, and says where it stopped (line, column, message); the checker's problems (an unplugged pin, an undeclared name) are not errors there, since the graph shows them on their nodes.

### Placing nodes freely, and Compile

Like Blueprints, **placing and editing nodes is never blocked by what is missing**. A node that needs a variable (`For Each`, `Reset Timer`, `Set Variable`, `Change Variable`, `Remove From List`, `Count Of`, `Item Of`, `On Variable Changed`) is added with that pin **unplugged**: the script holds the reserved name `unset` there (`for each Item in unset:`), the editor draws it as an empty red field ("drop a variable here"), and nothing is filled in for you, even if the script has a list that would fit. `unset` can't be used as a variable's name. Nodes that name an action, event, file or entity still take the first one that exists, or a placeholder name if there is none.

Every edit is accepted as long as the script still parses; a field whose text can't be turned into a sentence is refused with the reason (`Not changed: ...`). What the checker finds wrong is shown on the node, and listed by **Compile**. (Only a rename is held to a stricter rule: it may not make anything worse.)

**Compile** checks the script exactly as Play does and lists every problem that would make it fail, in the editor's words, each tied to the node it is on (click one to go to it): `For Each: List isn't plugged in yet`, ``Set Variable: `Count` isn't declared: declare a variable called Count, or plug in one that is``. Warnings don't fail it: a variable nothing uses (one marked `exposed` is left alone). The toolbar shows the state: Not compiled, Changed since compiling, Failed: N errors, Compiled. Compile writes nothing, and Play still refuses a script with errors. `forge_graph_load` returns the same list as `problems` (severity, line, message, node, variable).

### Variables, the variables panel, and the editor's layout

Variables are edited through the graph library too, so each change is undoable and written to the header: `add_variable` (a declaration such as `variable name:Speed type:decimal value:45.0 exposed:true`, placed after the other variables), `set_variable` (replace a variable's declaration: type, value, exposed; the name stays), `delete_variable` (nodes that used it show a problem when compiled), `rename_variable` (changes the declaration and every use, by the script's words, so strings, comments and fields like `Transform.Loc.X` are untouched, though a name between braces in a `format` text is renamed too, and nodes keep their positions). `forge_graph_load` lists each variable with its type, value, parts (`x:0 y:1 z:0`) and `exposed`, and the components the script needs. It also lists the `types` the script can use (its enums and structs, and the built-in `hit`) so an editor can offer them.

**Picking a variable.** A pin that names a variable is not a text field in the editor but a **dropdown** of what the script can use there, which `forge_graph_load` gives as the pin's `picks: true` and `choices`: `List` (For Each, Count Of, Item Of, Remove From List) offers the list variables and every `needs entities` (a live list of entities); `Timer` (Reset Timer) the timers; `Target` (Set Variable, Change Variable) any variable but a timer; `Variable` (On Variable Changed) any variable. Choosing one is an ordinary `set_pin`. A pin that names a declared variable or entity **and** has a place in the layout (a `value` line for that pin) is returned with a Get node, `<line>.v.<pin>`, named `Get <name>`, category `Variables`, with no pins, fed into that pin (`unplug` on it sets the pin to `unset`; a position for a pin that names nothing is dropped). Dragging a variable onto the pin and wiring in a Get node still work, and the choices follow the header as it changes. Locals made by `remember` and a loop's own name live inside a handler, so they are not offered; they can still be typed in the text. The language has no way to read another file's variables yet, so a `needs file` or `needs entity` offers no variables of the other script.

**References.** The header's `needs entity`, `needs entities` and `needs file` lines are what the script reaches for outside itself. They are edited through the library like variables: `add_reference` (a declaration such as `needs entities:Foes tag:"enemy"`, placed after the other references), `set_reference` (replace its declaration: file, tag, when it loads; the name stays), `delete_reference`, and `rename_reference` (every use is renamed, like `rename_variable`). `forge_graph_load` lists them as `references` (name, text, kind `entity`/`entities`/`file`, file, tag, when). A problem on a reference's line (a script that does not exist) is listed by Compile with the reference's name as its `variable`.

The editor has three columns, sized in font heights so they follow File > Preferences > UI Scaling, with draggable lines between them:

- **Left, top, Components:** the Object asset using the script, then its **Mesh**, and under the Mesh its **Material** and its **Transform** (where the mesh sits measured from the Object's anchor). The script's own `needs component:Transform` is not shown as a component: scripts move the whole Object, and the Mesh's offset from the anchor belongs to the Mesh. (Objects don't store an offset yet, so the Transform shows the identity.)
- **Left, middle, Variables:** the header's variables, colored by type. `+ Variable` declares one with a free name. A node's variable field is a **dropdown** of what fits it; or **drag a variable onto the field** to plug it in, or onto the canvas for a **Get** node and wire its output into a field. The wired Get node **stays**, connected to the pin: it is only a way of looking at the variable's name in the script (the script just says `for each Item in Targets:`), and its place is kept in the layout file like a value node's, so it is there the next time the script is opened. Deleting it unplugs the pin (`unset`). A Get node that is not wired yet is not saved. Right-clicking a field also offers `Use variable`.
- **Left, bottom, References:** the header's other entities and scripts. `+ Reference` offers Entity (one, found by tag), Entities (every one with a tag) and Script (another `.forge` file beside this one, picked from a list). An entity or entity list can be dragged onto a field like a variable, and a list shows up in the List dropdowns.
- **Middle:** the graph, with **Compiler Results** under it after a Compile.
- **Cutting a connection:** right-click a wire (it lights up under the mouse), or a pin that has one, and choose **Delete connection**. In a script graph this cuts a *value* wire: the pin becomes `unset`; a Get node is kept as a loose Get node where it was, and a computed value node is replaced by a plain value. An *execution* wire can't be cut (its item is greyed out) because a statement has to stay in its chain: drag its input wire elsewhere, or delete the node. In a material graph any wire can be cut, and it is one undo step. A dropdown's list takes the graph's zoom for its text, like the node it opens from.
- **Right, Details,** for whatever is selected: a variable (name, type, default value, exposed, delete), a reference (name, the script it names, its tag or when it loads, delete), a component (its asset, file, a material's base color), or a node (what it reads as, its fields, its comment, its problem).

### Where the nodes are

Positions are kept in a layout file beside the script, `door.forge.layout`, never in the script. It is plain text: a header line (`forge-layout 1`), then one line per positioned node: `node`, x, y, how many identical lines came before this one, and the node's line of script (`value` lines are the same with a pin name, for value nodes). While a script is open a position is carried on the line it belongs to, so adding, moving and deleting other nodes never disturbs it; on disk a line is found again by its text, so a script edited in a text editor keeps the positions of the lines that didn't change. A line with no position (new, or rewritten by hand) takes its automatic place, moved down if that is taken; a value node with none stays where it was relative to the node it feeds. The first time a node is moved by hand every node's position is saved; after that, a node added is pinned where it is shown. A missing or damaged layout file is ignored (the nodes are arranged automatically). "Reset layout" deletes it.

### Selecting, moving, deleting, undo

The canvas selects any number of nodes: click, Ctrl or Shift+click to add or remove one, drag a box on empty canvas (Ctrl or Shift keeps the old selection), Ctrl+A. Dragging one selected node moves them all, and they land on the grid. Delete removes every selected node as one step: nodes inside a deleted block go with it, a value node becomes a plain value, and keys are deleted last-first so none changes under another.

A script graph's undo is the script text and the layout file put back as they were: each edit, or group of edits (ten nodes deleted, a drag of many), is one step; Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z) in the tab, or the Undo and Redo buttons. A change made to the file outside the graph clears the history, so undo never overwrites it. Material graphs use the same canvas; their undo notices any change to the nodes, their values and their wires once the interaction making it ends, so a drag, a multi-drag, a multi-delete, a color pick and a texture choice are each one step.

### What it doesn't do (yet)

- Copy, paste and duplicate act on one node (the one clicked last), not on a whole selection.
- Undo history lives only while the tab is open.
- Variables, events, and actions are declared in the text. The graph shows variables but doesn't edit them, and an action's name and parameters are edited in the text.
- Value nodes show one level: a phrase inside a pin of a value node stays text in that pin.
- Adding a list-removal (`remove ... from ...`) node, a `Call Action` with arguments it can't fill in, or extra arguments to an existing call is done in the text.
- Enums, structs and tables are declared in the text (the Variables panel lists them as types to pick). A node that makes a struct or looks up a row needs one to exist (`Make Struct` is refused with the reason until it does).
- A Switch node grows with Add Case / Add Otherwise in its right-click menu, and its case values are typed as text (`1 or 2`).

## 13. Editor support

`forge-lsp/` is a language server (LSP over stdio), and `vscode-forgescript/` is the VS Code extension that colours `.forge` files and starts it. The server uses the same front end as `forgec`, so the editor shows exactly what the compiler would say.

| Feature | How |
|---|---|
| Errors as you type | The open file (unsaved text included) is parsed and checked with everything it needs; files that need it are re-checked too. |
| Colours by meaning | Semantic tokens: a variable, constant, parameter, action, event, entity, file, system, or component each get their own colour. |
| Hover | A name's kind, type, starting value, and the comments written for it. A call like `do TakeDamage on Player` shows the action from the other file; an engine call shows the manifest's signature. |
| Go to definition | A name goes to its declaration, a call to the action in the other file, and a `file:"..."` path to that file. |
| Find references, rename | Within the file, and across files for actions and events (rename edits every `do ... on ...` that reaches the action). Names that would clash, or aren't valid, are refused with the reason. Engine names can't be renamed. |
| Outline | Header declarations, then each handler and action with the names it makes. |
| Completion | Decided by the words already on the line: the names in scope, the next word of the sentence, a call's missing `key:` arguments, types after `type:`, fields after `Transform.`, and whole-line snippets. |

**The index.** Everything but the errors is built on `forge::index`: for each file, the symbols (with kind, type, and the lines where each is visible), every place a name is written, and the calls that belong to other files. It is built from the text one line at a time, not from the syntax tree, so it keeps working while a file is half-typed and `parse` would stop at the first error. This relies on one statement per line and on scope following indentation (a `remember` lasts to the end of its block). `forge/tests/index.rs` checks it against the real parser on every `.forge` file in the repository, so the two can't disagree.

**Limits.** Errors that only know their line underline the whole statement. Rename and find-references look at the `.forge` files under the open folders (not `target/`, `node_modules/`, or `build/`). A call on an entity resolves only when its `needs entity` names a `file:`. Hover and completion for engine systems read `Manifest::builtin()` until `engine.forgeapi` exists (section 8, decision 3).

**Building and installing.** `cargo build --release -p forge-lsp`; the extension finds `target/release/forge-lsp` in an open folder, or set `forgescript.serverPath`. In `vscode-forgescript/`, run `npm install`, then install it with "Developer: Install Extension from Location...".
