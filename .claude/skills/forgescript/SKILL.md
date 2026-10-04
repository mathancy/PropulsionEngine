---
name: forgescript
description: Write, edit, review, explain and debug ForgeScript (.forge) files, the controlled-English scripting language for the ForgeEngine game engine in this repo (header:/implement: sections, `when created:`, `every frame:`, `make Health 100`, `needs component:Transform`, `do Heal on Player`). Use whenever the user wants a game behavior or script (enemy, pickup, door, spawner, HUD, trap, state machine, ...), wants a Blueprint/node idea turned into ForgeScript text, wants a .forge file fixed or explained, or pastes a forgec / editor compile error, even if they only say "script" or "behavior" and never name the language. ForgeScript reads like English but is NOT a general-purpose language (no parentheses, no `=`, no braces, no `+=`), so guessing the syntax produces files that don't compile. This skill has the real rules and the check-with-forgec loop. Not for changing the compiler itself (parser, typechecker, codegen, graph).
---

# Writing ForgeScript

ForgeScript is a *controlled natural language*: every line matches one of a fixed list of sentence patterns, and anything else is a compile error. Your instincts from C-like languages, Python or Lua will mislead you, because the language was designed so non-programmers can read it (`make Health Health minus 5`, not `Health -= 5`). So write from the patterns below and from existing files, and let the compiler tell you when a line is wrong. Don't invent plausible-looking phrasings.

The repo root is `G:\ForgeScript`. The sources of truth, in order of how often you'll need them:

- `behaviors/*.forge` and `examples/*.forge`: working scripts. Find the one closest to the request and copy its idioms. `behaviors/*_kit.forge` have one small action per language construct (lists, math, flow, world, types), which makes them a good phrasebook.
- `SPEC.md`: the full language. Grep it for the exact pattern instead of reading all 750 lines. Sections: 3 header, 3.1 types, 3.2 enums/structs/tables, 4.1 statements and phrase expressions, 4.1b field paths, 4.1c numbers/vectors/traces, 4.2 conditions, 4.3 loops, 4.3a flow (`only once`, `choose`, `animate`...), 4.4 actions and `tell`, 4.5 handlers, 4.6 runtime errors, 5 systems, 6 ambiguity rules.
- `forge/tests/fail/*.forge`: one file per rejected mistake, each with the expected message on its first line. Useful when an error confuses you.

## The loop

1. **Find the closest existing script** and read it. If the new script talks to another file (`needs file`, `do X on Player`), read that file too: you need its real action names and parameter types.
2. **Write the file with the Write tool**, using real tab characters for indentation. (Shell heredocs mangle apostrophes and `\n` in this environment, and spaces for indentation are a compile error.)
3. **Check it** from the repo root:
   ```
   target/release/forgec.exe path/to/script.forge
   ```
   It checks the file and everything it `needs`. Success prints `ok: ...`; failures print the message, the line, and often the closest valid patterns (`I expected one of: ...`). If the binary is missing or older than the sources, run `cargo build --release -p forgec` first.
4. **Fix from the message**, don't guess variations. If the message doesn't settle it, grep SPEC.md or the kit files for the pattern. Repeat until it says `ok`.
5. **Format**: `forgec fmt <file> --check` should print nothing and exit 0; `forgec fmt <file> --write` fixes spacing. It never changes meaning or drops comments.
6. **For anything with logic, run it**: `forgec run <file> --frames 60 --press jump`. It simulates the script against a stub host and prints entity state and system calls (`Audio.Play`, `UI.SetText`, ...), which is the quickest way to confirm `after`, timers, `when ... changes` and health math really do what you intended. Compiling only proves the sentences are legal.

Only say a script is done once step 3 printed `ok`. Report any warnings the compiler gave.

## File shape

```
// pickup.forge: heals the player once, then hides and comes back after a while.

header:
	needs component:Transform
	needs component:Visibility
	needs system:Audio
	needs file:"player.forge" as Player when:used

	constant name:Cooldown type:decimal value:10.0
	variable name:Amount type:integer value:25 exposed:true
	variable name:Ready type:boolean value:true

implement:
	every frame:
		add 90 per second to Transform.Rot.Y

	when Self touches Player as Visitor:
		if Ready is false, then finish
		make Ready false
		tell Visitor to Heal
		do Play on Audio sound:"pickup.wav"
		make Visibility.Shown false
		after Cooldown seconds:
			make Visibility.Shown true
			make Ready true
```

(This compiles when saved next to `examples/player.forge`. `tell Visitor to Heal` works because that script has a `Heal` action; check the real action names of whatever file you talk to.)

- Exactly two sections: `header:` then `implement:`. Contents indented one tab. Statements may **not** sit directly under `implement:`: every line of code lives in a handler (`when created:`, `every frame:`, `every 2 seconds:`, `when ...:`) or an `action`.
- `//` comments. Start the file with a one-line comment saying what the script does.
- One statement per line; a line ending in `:` opens a block. An empty block is the single line `do nothing`.

## Rules that cause most compile errors

Each of these is a deliberate language decision, so there's no way round them. Restructure the code instead.

- **No parentheses anywhere.** A value is one of: a single term (literal, name, field path), a fixed phrase, *one* operator (`A plus B`, `minus`, `times`, `divided by`, `remainder after dividing by`), or one comparison. Anything bigger is split across lines with `remember Name <value>` (a temporary that lasts to the end of the block).
- **Never mix `and` with `or`** in one condition. Name part of it with `remember`.
- **Booleans are explicit**: `if Ready is true`, never `if Ready:`. Missing entities: `is present` / `is missing` (`is not missing` is an error).
- **Two number types, no implicit conversion**: `integer` and `decimal`. `decimal of Count` / `integer of Speed` to convert. Only whole-number *literals* may stand in for a decimal (`make Speed 2`). `timer` values compare to durations: `SinceHit is above 10 seconds`.
- **Everything used must be declared in the header**: components (`needs component:Transform`, also `Visibility`, `Collision`), systems (`needs system:Input|Audio|UI|Animation|Game`), other scripts (`needs file:"x.forge" as Name`, path relative to this file), entities by tag (`needs entity:Player tag:"player" file:"player.forge"`, `needs entities:Foes tag:"enemy"`). Using `Transform.Loc` without `needs component:Transform` is an error, and so is `move Self ...`.
- **Names**: PascalCase or camelCase, no spaces, unique across the whole file (variables, constants, actions, events, enum values, `remember` names, `as` names all share one list), and not a reserved word (`Self`, `unset`, `make`, `if`, `is`, `every`, ...). An enum value like `Idle` therefore can't also be a variable.
- **No filler words**: `make X be 5` is an error. **No aliases**: there is exactly one way to write each thing, so if a phrasing "should" work and doesn't, look it up rather than trying synonyms.
- **Dots only in field paths**: `Transform.Loc.X`, `Velocity.Y`, `Held.Cost`, and `of <entity>` for another entity's field (`Transform.Loc of Player`). Calls never use dots: `do TakeDamage on Player amount:10`.
- **Call arguments are named, never positional**: `do Hurt amount:5`.
- **Tabs only** for indentation.

## Pattern cheat-sheet

This is a working subset. Anything not here is in SPEC.md (grep the quoted phrase).

Header declarations: `variable name:N type:T [value:V] [exposed:true]`, `constant name:N type:T value:V`, `event name:E`, `tag name:"enemy"`, `enum name:Mode values:Idle Chasing`, `struct`/`table` (SPEC 3.2). Types: `integer decimal boolean text vector2 vector3 color entity timer gate`, `list of T`, enum/struct names. Vectors: `type:vector3 x:0 y:1 z:0`. List start items are indented `entry x:0 y:0 z:0` lines under the variable.

Statements:
```
make Health 100                       add 1 to Coins
subtract Amount from Health, floored at 0
add Heal to Health, capped at MaxHealth
multiply Speed by 2                   divide Speed by 2
remember Gap distance from Self to Player
do TakeDamage amount:10               do ShowCoins on Hud count:Coins
tell Foe to TakeDamage amount:3       // any entity; skipped quietly if it has no such action
send Died                             reset SinceHit
spawn Coin at:vector x:0 y:1 z:0 as Reward
destroy Self                          damage Foe by 5.0
move Self forward by Speed per second  // forward back left right up down
move Self toward Target by Step
print "Health is " joined with Health  attach A to B / detach A
after 2 seconds:                      repeat 3 times:
for Index from 0 to 4:                for each Foe in Foes:
while Health is above 0:              stop loop / skip / finish
give back Value                       // inside an action that says `gives <type>`
add Item to List / add Item to List, if new / remove Item from List / clear List
only once: / first 3 times: / alternate: ... otherwise: / through Door: / open Door / close Door / toggle Door
choose State:  (case Idle: ... case 1 or 2: ... otherwise: ...)
animate Lift from 0.0 to 4.0 over 2 seconds:
```

Conditions: `is`, `is not`, `is above`, `is below`, `is at least`, `is at most`, `X is present|missing`, `Input is pressing jump`, `Input starts pressing jump`, `Input stops pressing jump`, `List contains X`. One-line `if C, then <statement>`, or block `if C:` / `otherwise if C:` / `otherwise:`.

Phrases (single-term operands): `X per second`, `distance from A to B`, `count of L`, `item N of L` (from 0), `position of X in L`, `Text joined with V`, `text of V`, `format "Score: {Score}"`, `decimal of N`, `integer of D`, `absolute value of V`, `not B`, `A if C otherwise B`, `T of the way from A to B`, `clamp V between A and B`, `map V from A to B into C to D`, `random decimal|integer from A to B`, `normalized V`, `length of V`, `dot product of A and B`, `vector x:1 y:0 z:0`, `rotation pitch:P yaw:Y roll:R`, `ease Cur toward Goal at Speed`, `forward of Self`, `trace from A to B`, `cast E to File`, `new Item Cost:5`, `row "Sword" of Items`, `result of Action key:value`, `time since start`, `level name`, `axis moveForward of Input`.

Handlers: `when created:`, `when destroyed:`, `every frame:`, `every 2 seconds:`, `when Self touches File as Name:`, `when Self stops touching File as Name:`, `when Self hits File as Name:`, `when Var changes:`, `when Input starts pressing jump:`, `when MyEvent:`, `when File sends Event as Name:`, `when damaged as Amount from Source:`.

Actions: `action Name p:type q:type [gives type]:` with an indented body. Called with `do Name p:1 q:2`, or on another file/system/entity name with `on`.

Engine names the compiler accepts today: components `Transform` (`Loc Rot Scale`; axes `X Y Z`, rotation also `Pitch Yaw Roll`), `Visibility.Shown`, `Collision.Enabled`; inputs `moveForward moveBack moveLeft moveRight jump crouch interact`; axes `moveForward moveRight lookX lookY`; `Audio` (`Play sound: [volume: at: attachedTo: loop:]`, `Stop`), `UI` (`SetText element: text:`, `Show`, `Hide`, `Create widget:`, `Remove`, `Play animation:`), `Animation` (`Play`, `Stop`, `PlayMontage`), `Game` (`OpenLevel level:`, `SetPaused paused:`). An unknown one is a compile error, so don't invent more.

## Habits that keep scripts correct

- **Handlers say when things happen, so use the right one.** One-time setup goes in `when created:`; putting `make Health 100` in `every frame:` resets it 60 times a second. For movement, use `per second` so speed doesn't depend on frame rate.
- **Guard against missing entities**: `if Player is missing, then finish` before using `Player`'s fields. Scripts never crash, but they silently read defaults.
- **Make the state explicit** with `enum` + `choose` for state machines, `gate` for open/closed, `timer` for "time since", rather than loose booleans and counters.
- **Use `exposed:true`** for values a level designer would tune per instance (speeds, amounts, distances). Not for constants or timers.
- **Keep scripts focused**: one entity's behavior per file; cross-script talk goes through `needs file` + `do ... on`, `send`/`when File sends`, or `tell`.

## When the user wants node-editor-compatible text

Only if they ask. Each node's sentence gets a marker comment immediately above it: `// node 6: Play Sound` (id, then the name from `forgec nodes --table`), one marker per node a sentence implements, ids unique within the file. `forgec nodes <file>` checks that markers still match their sentences (exit 1 if one was edited by hand). `behaviors/door.forge` is the reference. Unmarked sentences are fine; they're just "custom code".

## Explaining or reviewing a script

Read it, run `forgec` on it, and explain in terms of when each handler fires and what state it changes. Point out runtime-only hazards the compiler can't see: a division whose divisor is only known at runtime gives 0, an out-of-range `item N of` gives the default, actions on a missing entity are skipped with a warning in dev builds.
