# CHECK-SEQUENTIAL.md — Check My Circuit for clocked circuits

Status: design, revision 1 (2026-10-06). Classroom round, Group 4, step 1.
Written for two implementations built in parallel that must give the same
answers, word for word:

- **the Mac core**: `mac/CedarCore/Check.cpp`, behind new calls in
  `CedarCore.h` (§11), shown by the Mac app's Check tab;
- **CedarLogic Online**: a new `public/assets/js/sim-check.js` (§12), run in the
  simulator's Web Worker on the WebAssembly engine, shown in the truth table
  sheet (the website has no Check My Circuit yet; it gets today's kinds too).

The tie-breaker is `tests/check-sequential/cases.json` (§13): 86 cases on 32
circuits plus 34 key texts with the kind each is read as. Both implementations
MUST reproduce every field of every case. The cases were made by
`mac/Tools/make_check_cases.cpp`, which holds a reference checker written from
this document and runs it on the real engine; every case also carries a
hand-worked answer, and the tool refuses to write the file when the engine and
the hand-worked answer disagree. When this text, the reference checker and an
implementation disagree, fix whichever is wrong until all three agree.

Words used here: a **switch** is a toggle switch (`AA_TOGGLE`); a **light** is
what the truth table calls an output (`GA_LED` and the other single LEDs); a
**clock part** is a part whose logic type is `CLOCK` (`BB_CLOCK`); a **pulse**
is one whole clock cycle, up then down (one press of Step Clock). Messages are
quoted exactly; `{x}` is filled in, `list()` is "a, b and c" as today's
Check.cpp writes it, and `plural(n, one, many)` is "1 row", "2 rows".

## 0. The whole thing on one page

Today Check My Circuit compares a page's truth table with a formula, a minterm
list or a pasted truth table, and only warns when the page has clocks or
flip-flops. This adds three kinds of answer key, typed or pasted into the same
box and told apart by their text alone:

```
0, 3, 5, 6, repeat                         a count: what the lights show, pulse by pulse
Q2 Q1 Q0: 000 011 101 110 ...              (the same, naming the lights, in binary)

Q1 Q0 X | Q1+ Q0+ | Z                      a state table: from each state, with each input,
0  0  0 |  0   0  | 0                      the next state and the outputs
0  0  1 |  0   1  | 0
...

Pulse | X | Z                              a timing table: the inputs before each pulse,
0     | 0 | 0                              the lights after it
1     | 1 | 0
2     | 1 | 1
```

and four optional lines that go with them:

```
start: Q1 Q0 = 00      the state to start from (reached with PRE'/CLR' or a reset)
reset: CLR' = 0        how to reset: these switches, one pulse, then back
set: EN = 1            switches held at these values for the whole check
clock: CLK             this switch is the clock
```

How a check runs, in short:

1. It works on **its own copy** of the circuit; the student's circuit isn't
   touched, running or not.
2. **Power-on**: the copy starts as a freshly opened circuit: every flip-flop,
   register and counter at 0, every clock part held at 0 (a running clock is
   stepped by hand for the check), every switch as it is on the page.
3. **The clock** is every clock part in the circuit, pulsed together; with no
   clock part, a switch named CLK or Clock (or the `clock:` line's switch).
4. **The start**: power-on, or the `start:` state, which the check reaches by
   itself: the `reset:` line, else turning other switches (PRE', CLR', a reset
   input...) on or off around one pulse.
5. **The run**: a count is checked over twice its length (the repeat); a
   timing table row by row; a state table by walking the circuit through every
   row, reaching each state by the rows already checked, and failing at the
   first row that's wrong.
6. **The result**: a one-line verdict in today's voice, notes, and a step
   strip: per step what was expected and what the lights showed, with the
   first wrong step marked.

| Question | Decision |
|---|---|
| Which kind a text is | From the text alone (§2): count, then timing, then state table, then truth table, then formula |
| Today's kinds | Unchanged (same matching, same messages); now detected, not chosen |
| What "settles" means | The engine's settle: steps until three in a row change nothing, at most **1000** steps |
| A pulse | Every clock to 1, settle, every clock to 0, settle (Step Clock's cycle) |
| Running clocks | Stepped by hand for the check, with a note; the `MANUAL` setting doesn't change the result |
| Several clock parts | All pulse together, with a note |
| Start state | Power-on, or `start:`, reached by `reset:` or by trying other switches with a pulse |
| How long a count runs | Twice round a repeating count; once through a count without `repeat` |
| Unknown and floating lights | Shown as X, Z, !, -; never match a 0 or 1; a warning per light, as today |
| Never settles | The check stops at that step: verdict "doesn't match", error `never_settles` |
| Which lights form a count | Named before a colon; else every light (not one named like another plus '), ordered by the number in their names (Q2 Q1 Q0), else top to bottom |
| Results | Verdict 0/1/2, a summary, notes, names, signals, steps, the first wrong step, an error code (§9) |

## 1. Scope

In: everything in this document, for the Mac core and the website. The Mac's
Check tab and the website's sheet show it (§14).

Out (not in this round): symbolic states ("A, B, C" with a state assignment);
next-state tables written as one column per input value; hex digits in a
count; reading a hex display or 7-segment display as the count; pulse
generators (`CC_PULSE`) as clocks; Windows and Linux (Group 8 ports
`Check.cpp`'s additions as it ports the rest).

## 2. Reading the key: which kind it is

The key is the text in the Check box. Detection looks at the text only (never
the circuit), so the box can say what it's reading as you type ("Read as a
count").

### 2.1 Lines

Split the text on `\n`, drop `\r`, trim spaces and tabs from each line, and
leave out empty lines. An **option line** is one whose first run of letters,
in any case, is `start`, `reset`, `set` or `clock`, followed by optional spaces
or tabs and a `:`. Every other line is a **body line**. Option lines may come
anywhere; their order doesn't matter.

### 2.2 Tokens

Two tokenizers:

- `tokens(line)`: split on spaces, tabs, `,` and `;`; a `|` is a token of its
  own (two in a row count once). This is today's Check.cpp `tokens()`
  unchanged.
- `words(text)`: split on spaces, tabs and `,` (new).

Value characters, as today: `0 1 x X - d D`; all but `0` and `1` are
don't-cares.

### 2.3 The kinds, in order

The first rule that applies wins.

1. **empty**: no body lines. (With option lines and no body, the check says
   so: §9, `bad_key`.)
2. **count**: exactly one body line, and it reads as a count (§3.2).
3. **timing table**: two or more body lines, none containing `=`, and either
   - the first token of the first line (ignoring `|`), in lower case, is one of
     `edge`, `step`, `cycle`, `pulse`, `tick`, `#`; or
   - there are three or more body lines, the first token (ignoring `|`) of
     every line after the first is all digits (6 at most), the first of them
     is 0 or 1, each next one is one more, and the last is 2 or more. (So
     `Clock | X | Z` works as a header once the table reaches pulse 2.)
4. **state table**: two or more body lines, none containing `=`; the first line
   has a token (other than `|`) with a character that isn't a value character;
   and either some token of the first line is a next-state name (§3.4), or
   the first line has a `|` with a name before it, and some name after that
   first `|` is (after `key()`, and after taking `(t)` off the end) also a name
   before it.
5. **truth table**: two or more body lines, none containing `=`, and every
   line after the first holds only value characters, `|`, spaces, tabs, `,`
   and `;`. (Today's pasted table; the first line may be names or values.)
6. **formula**: anything else (today's formulas and minterm lists, one or
   more lines).

`detect` also reports whether there were option lines. Some texts and their
kinds (all are in cases.json's `detect` list):

| Text | Kind |
|---|---|
| `S = A ^ B` · `F(A,B,C) = Σm(1,2,4,7)` · `A'B + AC` · `AB⏎C` · `1` · `5, repeat` · `0, 3, five, 6` | formula |
| `A B \| F⏎0 0 \| 0⏎…` · `0 0 0⏎0 1 1⏎…` · `CLK D \| Q⏎0 0 \| 0⏎…` · `Clock \| Q⏎0 \| 0⏎1 \| 1` | truth table |
| `0, 3, 5, 6, repeat` · `0 3 5 6 ...` · `0 3 5 6…` · `Q2 Q1 Q0: 000 011 101 110 (repeat)` · `0 -> 1 -> 2 -> 3 -> 0` · `0 → 1 → 2, and repeat` · `0 1` | count |
| `Q1 Q0 X \| Q1+ Q0+ \| Z⏎…` · `Q X \| Q \| Z⏎…` · `Q(t) T \| Q(t+1)⏎…` · `A B X \| A* B*⏎…` | state table |
| `Pulse \| X \| Z⏎1 \| 1 \| 0` · `edge X Z⏎…` · `# \| J K \| Q⏎…` · `Clock \| X \| Z⏎1 …⏎2 …⏎3 …` | timing table |
| `start: 00` (only an option line) | empty, with options |

(A one-line count needs two numbers: `5, repeat` is read as a formula and
fails as one. A single line of 0s and 1s with spaces, like `0 1`, is a count,
not a one-row truth table.)

### 2.4 Which check runs

- **formula** or **truth table** with no option lines: today's check, exactly
  as now (the app's formula reader, `cl_truth_table`, `cl_check_expected` /
  `cl_check_table`; on the website the same rules ported, §12).
- Anything else (empty, a count, a state table, a timing table, or a formula
  or truth table *with* option lines): the clocked check, which also produces
  the errors for empty keys and misplaced option lines.

## 3. The key's grammar

### 3.1 Option lines

Each may appear once (`There are two {word}: lines.` otherwise, where `{word}`
is `start`, `reset`, `set` or `clock` in lower case). Option lines with a
formula or a truth table are an error: `start:, reset:, set: and clock: lines
go with a count, a state table or a timing table.` The text after the `:`
(trimmed) is read as follows. They're read in the order they appear, before the
body.

- `clock: NAME`: one switch's name (anything without `=`). Empty or with `=`:
  `Write clock: like clock: CLK.`
- `start:`, `reset:` and `set:` are **assignments**, written one of three ways:
  - no `=`: just the bits, `start: 01` (spaces and commas between them are
    ignored). Only `start:` with a state table may do this; otherwise it's the
    "Write ... like" error below.
  - one `=`: names before it (`words()`), bits after it (only `0`, `1`, spaces
    and commas): `start: Q1 Q0 = 01`. As many bits as names, else
    `{word}: has {plural(n, "name", "names")} but {plural(m, "value", "values")}.`
  - two or more `=`: pairs split on `,` or `;`, each `NAME = 0` or `NAME = 1`:
    `reset: CLR' = 0, PRE' = 1`.
  - Anything else: `Write start: like start: Q1 Q0 = 01.`, `Write reset: like
    reset: CLR' = 0.` or `Write set: like set: EN = 1.`
- A count with a `start:` line: `A count starts at its first number, so it
  doesn't take a start: line.` (Checked as the line is reached, before its
  syntax.)

What they do: `set:` switches hold those values for the whole check (they are
the switches' power-on values, §5). `reset:` is a way to the start (§7):
those switches to those values, settle, one pulse, the switches back, settle.
`start:` is the state to start from (§7). `clock:` names the clock (§4.3).

### 3.2 A count

One body line:

1. If it has a `:`, the text before the first `:` is the **prefix**: the
   lights that form the number, most significant first (`words()`; at least
   one, or the line isn't a count). The rest follows the `:`.
2. In the rest, `->`, `→`, `=>` and `⇒` become spaces, and `…` and `...`
   become separate `...` tokens.
3. `tokens()`; a `|` means it isn't a count. A token wrapped in parentheses,
   like `(repeat)`, loses them.
4. If the last token is `...`, `repeat` or `repeats` (any case), the count
   **repeats**; drop it, and if it was a word and the token before it is
   `and` or `then`, drop that too.
5. What's left must be at least two tokens, all digits. These are the values.

Once the lights are known (§8.1, n lights): if every value has 2 or more
characters, all `0`/`1`, all the same length, they're **binary**, and their
length must be n (`Those numbers have {w} bits, but the count has
{plural(n, "light", "lights")}.`). Otherwise they're decimal. A value bigger
than 2ⁿ−1 (or longer than 9 digits): `{value as typed} doesn't fit in
{plural(n, "light", "lights")}: {it counts|they count} up to {2ⁿ−1}.` A
repeating count whose last value equals its first (and has 2 or more) drops
the last one, so `0 1 2 3 0 ...` is the cycle 0, 1, 2, 3.

### 3.3 A timing table

The first body line is the header. Its first token that isn't `|` names the
pulse column (whatever it says) and is skipped. The remaining tokens are
names; the first `|` that comes after at least one name marks where the
inputs end (a `|` after the last name doesn't count). No names: `Put the names
of the switches and lights in the top row, like Pulse | X | Z.`

Every other body line is a row; rows are numbered from 1 in the order written
(`{r}` below). More than 1000 rows: `That's {n} rows; up to 1000 can be
checked.` In each row:

- the first token that isn't `|` is the pulse number: digits, 6 at most, else
  `Row {r} needs its clock pulse number first.` Row 1's is 0 or 1; each next
  row's is one more. Otherwise `Number the rows by clock pulse: 0 for the start
  (if you like), then 1, 2, 3 and so on. Row {r} has {v}.` (For row 1, any
  number but 0 and 1 is this error.)
- the remaining tokens (without `|`) are joined into one string of cells; it
  must have one cell per name (`Row {r} has {got} values; the top row has
  {want} names.`, today's message), and each cell must be a value character
  (`Row {r} has "{c}"; use 0, 1, or X for don't care.`, today's message, for
  the first bad character).

**Inputs and outputs.** With a `|`, the names before it are inputs
(switches), the rest outputs (lights). Without one, the inputs are the
leading names that are switches (§6: by hand, then by name; the clock switch
counts here), and the rest are outputs; when every name is a switch: `Put a |
between the switches and the lights in the top row, like Pulse | X | Z.`
Zero inputs is fine (a counter's table).

An input cell that's a don't-care keeps the value from the row before (for the
first row: the switch's power-on value, §5). An output cell that's a
don't-care isn't checked. A row numbered 0 is **the start**: its inputs are
applied and its outputs checked with no pulse.

### 3.4 A state table

The header names columns; `|` tokens group them. A name is a **next-state
name** when it ends with `+` or `*` (and has a character before it) or with
`(t+1)` (any case, and has a character before it); its **base** is the name
without that ending. A name ending in `(t)` has that taken off (`Q(t)` is
`Q`). If no name is a next-state name, then (when there is a `|` with a name
before it) every name after the first `|` whose `key()` equals the `key()` of a
name before it is a next-state name of that one: `Q1 Q0 X | Q1 Q0 | Z`.

The **right side** starts at the first `|` that has a name before it, or at
the first next-state name, whichever comes first; everything before is the
**left side**. On the left:

- two names with the same `key()` (bases): `{base} is in the top row twice.`
- each next-state name must match exactly one left name by `key()` of the
  bases: `{name} has no {base} column for the present state.`, or, when two
  next-state names match the same one, `{name} is in the top row twice.`
- left names with a next-state name are the **state** (in their left-side
  order); the other left names are **inputs**. On the right, next-state names
  are reordered into the state's order; the other right names are
  **outputs**. (At least one state name: detection guarantees it.)

Rows (numbered from 1 as written, `{r}`): cells joined as in §3.3 (no pulse
column), one per name, value characters only (today's two messages). A
don't-care in a state or input cell stands for both values: the row is
**spread** into every combination, in binary counting order (the leftmost
don't-care most significant), each keeping its row number. More than 1024 rows
once spread (counted before merging; or more than 10 don't-cares in one row):
`That table stands for more than 1024 rows; up to 1024 can be checked.`

When two spread rows have the same state and inputs, they merge: a cell
specified in both with different values is `Two rows give {column} different
values for the same state and inputs (row {r}).` (`{column}` the right-side
name as written, `{r}` the later row's number); otherwise the merged row takes
every specified cell, at the first one's place. Rows whose next state and
outputs are all don't-cares are dropped: there's nothing to check. Rows whose
state or inputs are left out of the table aren't checked either (no message:
unused states are commonly left out).

## 4. The circuit

### 4.1 Switches and lights: the ports

The check reads one page: the switches and lights on it, **every one**
(unlike the truth table, the selection doesn't matter), switches first, then
lights, each group top to bottom then left to right: exactly `cl_truth_table`'s
order and names when nothing is selected. A name is the part's own name (the
website's), else the nearest unused text label of 16 characters or fewer
within 8 grid units (inputs are named before outputs, each in order), else `A`,
`B`, `C`... for switches and `Y` (one light) or `Y1`, `Y2`... for lights. A
port is **labeled** when its name isn't one of those fallbacks. The truth
table's limit of 8 switches doesn't apply. Port numbers (0, 1, 2...) index this
list; results refer to switches and lights by them.

### 4.2 Clock parts

Every part with logic type `CLOCK` on **every** page of the circuit (their
order doesn't matter), whatever its half-period or its "Only on Step Clock"
(`MANUAL`) setting. A clock part is **running** unless its `MANUAL` logic
parameter is `true`.

### 4.3 The clock

In this order:

1. A `clock:` line: the switch it names (§6, by hand then by name) is the
   clock; no such switch is `no_switch` (§9). Clock parts are then held at 0
   for the whole check.
2. Else, when the circuit has clock parts: they are the clock, all pulsing
   together.
3. Else, the first switch (port order) whose name's `key()` is `clk` or
   `clock` is the clock.
4. Else: `no_clock`.

The **clock switch** (cases 1 and 3) is not an input: a key column naming it
is `clock_in_key` (§9). In case 2, a key input whose `key()` is `clk` or
`clock` is `clock_in_key` too.

### 4.4 Inputs and control switches

- **Inputs** are the switches the key names as columns (a state table's or
  timing table's inputs), matched as in §6 among the switches other than the
  clock switch.
- `set:` and `reset:` name switches other than the clock switch (by hand,
  then by name; never by position). A `set:` switch that is also an input:
  `{switch's name} is in the table and in set:; leave it out of one.` (A
  `reset:` switch may be an input.)
- **Control switches** are the switches that are neither the clock switch, nor
  inputs, nor `set:` switches, in port order. The start search (§7) may turn
  them on or off; otherwise they stay as they are on the page.

## 5. The checker's primitives

The whole check is written in terms of five operations on a copy of the
circuit. Both implementations MUST behave as described; how they do it is
theirs (suggestions in §11 and §12).

- **powerOn()**: throw away the copy and make a new one from the circuit (every
  page, every part and wire), before it has run a single step, then:
  - every gate whose logic type is `REGISTER` (D flip-flops, registers,
    counters, shift registers) gets `CURRENT_VALUE` = `0` (`J-K` flip-flops
    start at 0 anyway; RAM and ROM keep their contents);
  - every clock part is a **manual clock at 0**: it changes only when the
    checker pulses it (electrically, a switch the checker turns on and off);
  - every switch gets its **power-on value**: its value on the page, except
    the clock switch (0) and `set:` switches (their values);
  - then **settle**.
- **settle()**: step until three steps in a row change nothing (the engine's
  own settle: `LogicHost::settle`, engine-core's `settle`), at most **1000**
  steps. Reaching 1000 means it **never settled**: the check stops (§8, §9).
  (The Mac's settle also counts a gate parameter changing, the website's only
  wires; a gate's parameter changes with its outputs, so they agree.)
- **pulse()**: every clock (the clock parts, or the clock switch) to 1,
  settle, every clock to 0, settle.
- **set(switch, v)**: the switch's output to v (no settle by itself).
- **read(light)**: the light's value: `0`, `1`, `X` unknown, `Z` floating, `!`
  conflict, or `-` not connected: the state of the wire on the light's first
  connected pin, as `cl_truth_table` reads it.

**The power-on edge.** A clock wire goes from nothing (floating) to 0 at
power-on. A falling-edge part's clock input is inverted, so it sees that as a
rising edge and acts once: a falling-edge J-K flip-flop with J = K = 1 starts
at 1. CedarLogic does this every time such a circuit opens; the check
reproduces it rather than hiding it (cases `timing-jk-falling-edge-power-on`
and `timing-jk-falling-edge`, which uses `reset: CLR' = 0`). Rising-edge parts
don't see it. The website's engine does the same (checked, §13).

## 6. Matching names

Names match as today: by `key()` (letters and digits, lower case: "Carry In"
is "carry_in"). The `names` argument maps an asked-for name to a switch's or
light's name, one per line, `asked<TAB>port`, as today.

**match(wanted names, pool of ports)**, used for a group of names at once:

1. by hand: each wanted name with a `names` entry takes the first port in the
   pool, not yet taken, whose name has the entry's `key()`;
2. by name: each wanted name still without a port takes the first free port
   with its `key()`;
3. by position: if the names still without a port and the free ports are the
   same number (more than 0), they pair up in order, and each pair is noted
   `{wanted} is {port}`.

A wanted name still without a port is **missing**.

**findPort(name, pool)**, used for single names: by hand, then by name; never
by position, and it doesn't mind ports already taken.

Groups, in this order (each one's missing names end the check, §9):

| Kind | Inputs (pool: switches but the clock switch) | Lights (pool: lights) |
|---|---|---|
| count | none | the prefix's names, by match(); without a prefix, §8.1 |
| timing | the input columns, by match() | the output columns, by match(); then the `start:` names, by findPort() |
| state table | the input columns, by match() | the state names followed by the output names, by one match() |

`clock:`, `set:` and `reset:` names use findPort(). The result's **names**
list holds every asked-for name in this order: the `clock:` name; the inputs;
the `set:` names; the `reset:` names; the lights (as in the table above; for a
count without a prefix, the lights forming the number by their own names).
Each entry: the name, whether it's a switch, its port (−1 when missing) and
whether it was matched by hand.

## 7. The start

**reach(lights, good)** puts the copy into a state where the lights' readings
satisfy `good`, trying in this order and stopping at the first that works:

1. powerOn(); read the lights.
2. If there is a `reset:` line: powerOn(); the reset (its switches to its
   values, settle, pulse, back to their power-on values, settle); read.
3. For every set T of the **first 8** control switches, of 1, then 2, 3 and 4
   switches, in lexicographic order of port order (`{a}`, `{b}`, ...,
   `{a,b}`, `{a,c}`, ..., `{b,c}`, ...): powerOn(); every switch in T to the
   opposite of its power-on value, settle, pulse, back, settle; read.

Each try starts from a fresh power-on; only the one that works is kept. Any
settle that never settles stops the check (`never_settles` at the start). When
a reset or a try T worked, the note `To start, the check set {list("{name} to
{v}")}, gave one clock pulse and set {it|them} back.` says how (`{v}` the
value it was set to).

When nothing works: `start_unreachable` (§9). The lights' reading quoted in
its note is from a fresh power-on.

What each kind starts from:

- **count**: reach(the count's lights, good) where good means "all 0/1 and the
  number is the first value" for a count without repeat, and "all 0/1 and the
  number is one of the values" for a repeating one. A repeating count then
  starts at the first place its number appears; when that isn't the first
  value: `The lights started at {v}, so the check started there in the count.`
- **timing table**: with `start:`, reach(the `start:` lights, readings equal its
  bits). Without: powerOn(), then the `reset:` line's reset if there is one
  (with its note).
- **state table**: with `start:`, reach(the state lights, readings equal its
  bits), the bits in the state's order (`start: 01`, or names that must be
  exactly the state's names in any order: else `start: gives the state, so it
  names {list(state names)}.`; bare bits of the wrong length: `start: needs
  {plural(n, "value", "values")}, one for each of {list(state names)}.`).
  Without `start:`: powerOn(), then the `reset:` line if any; if the state
  lights aren't all 0/1: `start_unknown`.

## 8. Running each kind

Every step the check records has: a **kind** (`start`, `set` or `pulse`), a
**pulse** number, a **row** (the key's row number, 0 when none), the **state**
read before it (state tables), the inputs (**in**), what was expected
(**exp**) and what the lights showed (**got**), and whether it's **wrong**.
`exp` and `got` are strings over the kind's checked signals; `-` in `exp` is a
don't-care, which never makes a step wrong; any other difference does. A step
whose settle never settled has `~` for every `got` (or `state`) character and
is wrong; the check stops there.

The **signals** list (the step strip's columns): each signal's name, role
(`input`, `state`, `next`, `output`) and port.

### 8.1 A count

**The lights.** With a prefix, the prefix's lights (§6). Without one: every
light, except a light whose name ends in `'` or `’` when another light's name
is that name without it (`Q'` beside `Q`, compared trimmed and in any case).
None left: `no_lights`. Then, if there are 2 or more, every one is labeled, and
every name is zero or more letters (and `_`) followed by 1 to 6 digits,
with the same letters (any case) and different numbers, they're ordered by the number, the
highest first (`Q0 Q1 Q2` placed top to bottom read as Q2 Q1 Q0). Otherwise
port order. Without a prefix, a note says which: `The count is read from the
lights {names, space-separated}, most significant first.` (one light: `The
count is read from the light {name}.`). More than 16 lights: `That's {n}
lights; a count can use up to 16.`

Signals: the lights, most significant first, role `output`, named by the
prefix's names (or the lights' own names without a prefix).

**The run.** The start (§7) is step 0: kind `start`, `exp` the value it
started at, `got` the reading. Then **2L** pulses for a repeating count of L
values (twice round, so a count that fails to repeat is caught), or **L−1**
for one without repeat (the values written, once). Pulse p expects the value
p places after the start, wrapping round. Every pulse is run even after a
wrong one (the strip shows what the circuit really does), unless it never
settles.

**Verdict.**

- First wrong step p: verdict 1, `Doesn't match: after clock pulse {p} the
  lights show {got}, not {want}.` (`the light shows` for one light). `{got}`
  is the number when every bit is 0/1, else the bits as read (`X0`), and then
  the expected bits follow the expected number: `... show X0, not 2 (10).`
- Otherwise verdict 0: `Matches: the lights count {values} and repeat (checked
  twice round).`, or without repeat `Matches: the lights count {values}.` (`the
  light counts` for one light). `{values}` are the values in decimal, as the
  cycle was written (from its first value), joined by `, `; more than 8: the
  first 6, then `…`, then the last (`0, 1, 2, 3, 4, 5, …, 15`).

### 8.2 A timing table

The start (§7). Then, if the first row is numbered 1, an implicit step 0:
kind `start`, `exp` all `-`, `got` the outputs as read, `in` empty. Then each
row in order: every input to the row's value (or the value it had, for a
don't-care), settle, and unless the row is numbered 0, a pulse; read the
outputs. The step: kind `start` for row 0, else `pulse`; pulse = the row's
number; row = its row number; `in` the inputs applied; `exp` the row's
outputs. Every row runs, wrong or not, unless one never settles.

Signals: the inputs (role `input`), then the outputs (role `output`), named as
the key names them.

**Verdict.** First wrong step: verdict 1, `Doesn't match: after clock pulse
{p}, {list(parts)}.` (row 0: `Doesn't match: at the start, {list(parts)}.`),
each part `{output} is {got} instead of {want}` for every wrong output of that
step, in column order. Otherwise verdict 0: `Matches: every clock pulse gives
what was asked for.`, or with N don't-care output cells in the table
`Matches: every clock pulse gives what was asked for ({plural(N, "don't-care
wasn't", "don't-cares weren't")} checked).`; when every output cell is a
don't-care, `Matches, but every value was a don't-care, so nothing was really
checked.`

Outputs are read **after** the pulse with the row's inputs still on, which is
what a characteristic table or a Moore machine's outputs mean. A Mealy
machine's output that depends on the input before the edge is checked with a
state table, which reads outputs before the pulse.

### 8.3 A state table

The rows are the spread, merged rows of §3.4, in order; each has a state P,
inputs I, an expected next state N and outputs O (with `-` for don't-cares).

**Running a row q** (from the state it's in, `cur`): every input switch to I,
settle, read the outputs (before the pulse, so a Mealy output sees this
state and these inputs), pulse, read the state lights. The step: kind `pulse`,
pulse = pulses since the last `start` or `set` step, row = q's row number,
state = `cur`, `in` = I, `exp` = N then O, `got` = the next state read then the
outputs read. The row is now **done**, `cur` becomes the state read, and the
row's **seen** next state is recorded. A wrong step stops the check.

**The walk.**

```
start (§7); s0 = cur = the state read; step: kind start, state = cur
loop:
  if every row is done: stop
  if 1000 pulses have been given: stop (stopped)
  1. if a row not done has P == cur: run the first such row (table order); continue
  2. breadth-first search from cur over done rows (an edge from P to its seen
     next state; at each state the rows in table order), for the first state
     reached that has a row not done; if found: run the rows along the path
     (each is a done row run again, and checked again); continue
  3. if cur != s0 and the last thing done wasn't a restart:
     restart (the start again, from scratch, with another start step); continue
  4. for each row not done, in table order, whose P hasn't failed this before:
     reach(state lights, readings == P) (§7); if it works: step kind set,
     state = the reading; cur = P; continue the loop
     else remember that P can't be set
  5. stop
```

**Verdict.**

- A wrong step: verdict 1, `Doesn't match: in state {state names, space-separated} = {state} with {input names, space-separated} = {in} (row {r}), {list(parts)}.`
  (without inputs, no ` with ...`), the parts being `the next state is {got
  next} instead of {exp next}` when a next-state cell is wrong, then `{output}
  is {got} instead of {want}` for each wrong output.
- Otherwise verdict 0:
  - every row done: `Matches: every row of the state table does what was asked
    for.`, or with N don't-care cells among the done rows (counted once per
    row) `Matches: every row of the state table does what was asked for
    ({plural(N, "don't-care wasn't", "don't-cares weren't")} checked).`
  - no rows at all (all were dropped): `Matches, but every value was a
    don't-care, so nothing was really checked.`
  - rows left: `Matches on every row it reached, but {plural(n, "row", "rows")}
    ({state|states} {list(their states, table order, once each)})
    {wasn't reached, so it wasn't checked.|weren't reached, so they weren't
    checked.}` and, unless the check stopped at 1000 pulses, the warning
    `{State {s} was|States {list} were} never reached: clocking from the start
    doesn't get there, and no switch sets {it.|them.}`; when it stopped, the
    warning `The check stopped after 1000 clock pulses.`

Signals: the inputs (`input`), the state lights (`state`, named as the key's
state names), the same lights again as the next state (`next`, named as the
key writes them: `Q1+`, `Q(t+1)`), the outputs (`output`).

A worked example (case `states-moore-wrong-wire`): a Moore detector whose Q0
input is wired to X·Q1′ instead of X·(Q1+Q0)′. The rows: 1 (00,0→00), 2
(00,1→01), 3 (01,0→00), 4 (01,1→10), 5 (10,0→00), 6 (10,1→10); the row
`1 1 - | - - | -` is dropped. Start 00. Row 1 runs (00→00), then row 2
(00→01), row 3 (01→00). At 00 no row is left; the search finds 01 through
row 2, which runs again (00→01). Row 4 runs: X·Q1′ = 1, so the circuit goes to
11: wrong. Steps: start, rows 1, 2, 3, 2, 4. Summary: `Doesn't match: in state
Q1 Q0 = 01 with X = 1 (row 4), the next state is 11 instead of 10.`

## 9. Results and messages

### 9.1 The result

| Field | What |
|---|---|
| kind | the detected kind (§2): `empty`, `formula`, `table`, `count`, `states`, `timing` |
| verdict | 0 matches, 1 doesn't match, 2 couldn't check |
| error | `""`, or one of the codes in §9.2 (`never_settles` comes with verdict 1) |
| summary | one line, shown big |
| notes | `[kind, text]`: 0 for your information, 1 a warning, 2 a problem to fix |
| names | §6 |
| ports | §4.1: `[name, isSwitch]` |
| signals | §8: `[name, role, port]` |
| steps | §8: `{kind, pulse, row, state, in, exp, got, wrong}` (strings `""` when they don't apply) |
| firstWrong | the index into steps of the first wrong step, or −1 |

An error result (verdict 2) keeps only its problem notes. Today's kinds keep
today's result (summary, notes, outputs, the rows' cells, wrong rows, names).

### 9.2 Errors

Checked in this order: the key (§3: `empty`, `bad_key`), the clock (§4.3), a
timing table's `|` (§3.3), clock columns (`clock_in_key`, in column order),
inputs, `set:`/`reset:` names (both groups' missing names together), the
`set:`/input clash, then the kind's own: a count's lights, its size and
values; a timing table's outputs and `start:` names; a state table's lights
and `start:`; then the start, then the run.

| Code | When | Summary (verdict 2) | Problem note |
|---|---|---|---|
| `empty` | nothing typed | `Type or paste what the assignment asks for: a formula, a truth table, a count, a state table or a timing table.` | |
| `bad_key` | only option lines | `Add what to check under the start:, reset:, set: or clock: lines: a count, a state table or a timing table.` | |
| `bad_key` | any message of §3, §4.4, §7 and §8.1 | the message | |
| `no_switch` | a switch name with no switch | `Can't check yet: there's no switch called {X}.` / `Can't check yet: there are no switches called {list}.` | `There's no switch called {X} (the switches are {list of all switches}). Pick which switch it is under Names, or change a switch's label.` / `There are no switches called {list} (...). Pick which switch each one is under Names, or change a switch's label.`; with no switches at all, `(this page has no switches)` |
| `no_light` | a light name with no light | `Can't check yet: there's no light called {X}.` / `Can't check yet: there are no lights called {list}.` | one per missing light: `There's no light called {X} (the lights are {list of all lights}). Pick which light it is under Names, or change a light's label.`; with none, `(this page has no lights)` |
| `no_lights` | a count without a prefix, and no lights | `Can't check yet: there are no lights on this page to read the count from.` | `Put a light on each flip-flop's output and label them, like Q2, Q1 and Q0.` |
| `no_clock` | §4.3 | `Can't check yet: there's no clock.` | `Add a clock (from Input and Output) to the flip-flops' clock inputs, or say which switch is the clock with a line like clock: CLK.` |
| `clock_in_key` | §4.3 | `Can't check yet: {name} is the clock, so it can't be a column too.` | `Each row is one clock pulse, and the check turns the clock on and off itself. Leave {name} out of the table.` |
| `start_unreachable` | §7 | count: `Can't check yet: the lights can't be set to {first value} to start.`; repeating count: `Can't check yet: the lights can't be set to a number in the count to start.`; others: `Can't check yet: the circuit can't be set to {names, space-separated} = {bits} to start.` | `At the start the lights show {reading}. Turning the other switches on or off, with a clock pulse, didn't get them there. Add a reset (a switch on the flip-flops' CLR' or PRE'), or name it with a line like reset: CLR' = 0.`; when there's no `reset:` line and no control switch: `At the start the lights show {reading}, and there's no other switch to reset them with. Add a reset (...)` (the same ending). `{reading}`: a count's number (or bits when not all 0/1), else the bits |
| `start_unknown` | a state table without `start:` starts with state lights not all 0/1 | `Can't check yet: at the start {Q} is {v}, not 0 or 1.` (one state light) / `Can't check yet: at the start the state {names} is {bits}, not 0s and 1s.` | `Flip-flops made from gates start unknown. Add a start: line, like start: {names} = {all 0s}, and a reset the check can use (a switch, or a reset: line).` |
| `never_settles` (verdict 1) | a settle reached 1000 steps | `Doesn't settle: {where} the circuit was still changing after 1000 steps, so a loop of gates may be flipping back and forth.` with `{where}`: `at the start`, `at clock pulse {p}` (count, timing), `in row {r}` (state table) | (not an error result: its notes are kept) |

"No manual clock" is not an error for the check: a running clock is stepped
one pulse at a time on the copy, and a note says so. (Step Clock itself is
disabled without a manual clock; that's Group 1's.)

### 9.3 Notes, in this order

1. The clock:
   - one clock part, running, and it's the clock: `The clock runs by itself on
     your page; for the check it was stepped one pulse at a time.`
   - two or more clock parts that are the clock: `This circuit has {n} clocks;
     for the check they all pulsed together, one pulse at a time.`
   - a switch found by its name (§4.3, 3): `There's no clock, so the switch
     {name} was used as one: each pulse turned it on and off.`
   - a `clock:` line while there are clock parts: `The clock: line makes {name}
     the clock, so the clock parts on the page were held at 0.`
2. `Matched by position, as the names differ: {list(pairs)}.` (all pairs of
   the check: inputs first, then lights; today's text)
3. The control switches that aren't `reset:` switches: `Switch {name} isn't in
   what was asked for, so it stayed as it is on the page ({v}).` / `Switches
   {list} aren't in what was asked for, so they stayed as they are on the page
   ({list of values}).` (power-on values)
4. Lights the check doesn't read (not in the count; not an output or a
   `start:` light; not a state or output light): `Light {name} isn't in what
   was asked for, so it wasn't checked.` / `Lights {list} aren't in what was
   asked for, so they weren't checked.` (today's text)
5. A count without a prefix: which lights form the number (§8.1).
6. The start: the reset or try that got there (§7); a repeating count started
   midway (§7).
7. Warnings for lights that showed something other than 0 or 1, as today, one
   per light and value, the light named by its own (port) name, the lights in
   signal order (a state table: state lights, then outputs, counting the
   next-state readings), the values in the order `!`, `-`, `X`, `Z`, counting
   steps whose `got` has it (not `~`): `On {plural(n, "step", "steps")} the
   light {name} shows {what}.` with today's `{what}`: `X (unknown): a gate
   feeding it may be missing an input`, `Z (floating): nothing is driving it`,
   `! (a conflict): two outputs are wired together`, `nothing: it isn't
   connected`.
8. A state table: unreached states, or stopped at 1000 pulses (§8.3).

## 10. Limits

| What | Limit | Beyond it |
|---|---|---|
| A settle | 1000 steps | `never_settles` |
| A count's lights | 16 | `bad_key` |
| A count's values | 2ⁿ−1, 9 digits | `bad_key` |
| A timing table's rows | 1000 | `bad_key` |
| A state table's rows, spread | 1024 | `bad_key` |
| A state table's walk | 1000 pulses | stops, warning |
| The start search | the first 8 control switches, sets of up to 4 (162 tries) | not tried |

## 11. The Mac core: CedarCore.h

Add to the "Check my circuit" section; today's calls are unchanged.

```c
// ---- Check my circuit: clocked circuits (docs/CHECK-SEQUENTIAL.md) ---------
// What kind of answer key a text is, from the text alone; *options (may be
// NULL) is set when it has start:, reset:, set: or clock: lines. FORMULA and
// TABLE without options go to today's check; everything else to
// cl_check_clocked.
enum { CL_KEY_EMPTY = 0, CL_KEY_FORMULA = 1, CL_KEY_TABLE = 2, CL_KEY_COUNT = 3,
       CL_KEY_STATES = 4, CL_KEY_TIMING = 5 };
int cl_check_key_kind(const char *text, bool *options);
// Checks a page against a count, a state table or a timing table, clock pulse
// by clock pulse, on a copy of the circuit (the document isn't changed, its
// clocks keep running). Reports the errors of a key it can't read too, so
// it takes any text. `names` as for cl_check_expected; a name's column is a
// port (below). Free with cl_check_free. The verdict, summary, notes and
// names calls above work on it; cl_check_outputs is 0.
CLCheck *cl_check_clocked(CLDocument *doc, int page, const char *key, const char *names);
int cl_check_kind(const CLCheck *c);            // CL_KEY_*
const char *cl_check_error(const CLCheck *c);   // "" or the error's code: "no_clock"...
// The page's switches, then its lights, as the check named them.
int cl_check_port_count(const CLCheck *c);
const char *cl_check_port_name(const CLCheck *c, int port);
bool cl_check_port_is_input(const CLCheck *c, int port);
// The step strip's columns.
enum { CL_SIGNAL_INPUT = 0, CL_SIGNAL_STATE = 1, CL_SIGNAL_NEXT = 2, CL_SIGNAL_OUTPUT = 3 };
int cl_check_signal_count(const CLCheck *c);
const char *cl_check_signal_name(const CLCheck *c, int signal);
int cl_check_signal_role(const CLCheck *c, int signal);
int cl_check_signal_port(const CLCheck *c, int signal);
// The steps: the start (or a state set with switches) and each clock pulse.
enum { CL_STEP_START = 0, CL_STEP_SET = 1, CL_STEP_PULSE = 2 };
int cl_check_step_count(const CLCheck *c);
int cl_check_first_wrong(const CLCheck *c);     // a step, or -1
int cl_check_step_kind(const CLCheck *c, int step);
int cl_check_step_pulse(const CLCheck *c, int step);
int cl_check_step_row(const CLCheck *c, int step);  // the key's row, 0 for none
bool cl_check_step_wrong(const CLCheck *c, int step);
// A step's state (read before it), inputs, what was expected ('0', '1', '-')
// and what the lights showed ('0', '1', 'X', 'Z', '!', '-', '~' still
// changing), one character per signal of that role. Valid until the check is
// freed.
enum { CL_STEP_STATE_BITS = 0, CL_STEP_INPUTS = 1, CL_STEP_EXPECTED = 2, CL_STEP_GOT = 3 };
const char *cl_check_step_text(const CLCheck *c, int step, int what);
```

The app's flow: `cl_check_key_kind`; a formula without options goes through
the formula reader and `cl_check_expected` on the truth table, a table
without options through `cl_check_table`, as today; everything else
`cl_check_clocked(doc, page, text, names)`.

Implementation notes:

- `CLCheck` grows the result's new fields (kind, error, ports, signals,
  steps, first wrong); the key reading of §2 and §3 belongs in Check.cpp next
  to `readTable`, the run in a new file (say `CheckClocked.cpp`) that can use
  `DocumentImpl.h`.
- The copy: `cl_document_save_text(doc)`, then with settle-on-open off
  (`cl_set_settle_on_open(false)`, put back after) `cl_document_open_text`
  for each powerOn(); then the primitives of §5 through
  `circuit.sendMessageToCore(MT_SET_GATE_PARAM ...)` (as TruthTable.cpp sets
  switches) and `sim->settle(1000)`. Gate ids survive the save and reopen, so
  ports found on the real page address the copy.
- The clocks: Group 1's manual clock (`MANUAL` `true` on every clock part of
  the copy, driven by the engine's manual-clock mechanism, started at 0); the
  reference checker instead swaps each clock part for a switch with the same
  id, and the two are the same electrically (both output with no delay). The
  check doesn't call `cl_document_clock_step`: that pulses one page's manual
  clocks on the live document, while the check pulses every clock part of
  every page, on its copy.
- Ports and names: `cl_truth_table`'s code, minus the selection and the
  8-switch limit; keep the fallback/label distinction (§4.1).
- `mac/Tools/make_check_cases.cpp`'s `Checker` is a complete reference of
  §2–§10 against CedarCore; porting it is fine, as long as it then runs on
  the real copy (not a twin).
- No Apple-only APIs: Check.cpp is shared with the Linux app (and later
  Windows).

## 12. CedarLogic Online: sim-check.js

`public/assets/js/sim-check.js` holds the whole check without the DOM, so the
page, the Web Worker (`importScripts`) and node (tests) all load it. It sets
`(self or globalThis).CedarLogicCheck`:

```js
CedarLogicCheck = {
  // §2. { kind: "empty" | "formula" | "table" | "count" | "states" | "timing", options: bool }
  detect(text),

  // Today's kinds, ported from Check.cpp word for word, on a truth table as
  // sim-truth.js's makeTable makes it ({ names, inputs, rows, sequential, unsettled }).
  // A formula's `expected` is today's "in\t...\nout\tF\t0110..." text, which
  // specFromFormulas makes from CedarTruth.parseAll's result.
  checkExpected(tt, expected, names),     // cl_check_expected
  checkTable(tt, keyText, names),         // cl_check_table
  specFromFormulas(parsed),

  // Clocked kinds (and the errors of any key with option lines, or an empty
  // one), on a machine with the primitives of §5. Synchronous: runs in the worker.
  checkClocked(machine, keyText, names),

  // From the page: everything above for the board on screen. Asks the worker
  // for a truth table or a clocked check as needed. Resolves to a result.
  check(app, keyText, names),             // Promise<Result>
};
```

`names` is an object `{ askedName: portName }`. A **Result** is exactly the
`expect` object of a case in cases.json (§13): for clocked kinds `kind`,
`verdict`, `error`, `summary`, `notes` (`[kind, text]`), `names`
(`[name, isSwitch, port, byHand]`), `ports` (`[name, isSwitch]`), `signals`
(`[name, role, port]`), `firstWrong`, `steps` (objects as in §9.1); for
today's kinds `kind`, `verdict`, `error` (`""`), `summary`, `notes`, `names`,
`table` (`{names, inputs, rows}`), `outputs` (`[name, column, wrong]`) and
`rowWrong` (row indices). (The sheet will also want today's per-row
`expected`/`result` cells, as CLCheck has them; cases.json leaves them out.)

The **machine** checkClocked runs on (made in the worker by a new
`CLEngine.checkMachine(M, spec, ports)` in engine-core.js):

```js
machine = {
  ports,                        // §4.1: [{ id, name, input, labeled, value: "0"|"1" (switches), net (lights) }]
  clocks,                       // §4.2: [{ id, manual }], every page
  powerOn(base, clockSwitchId), // §5: a new EngineCircuit from the spec, every CLOCK gate a manual clock
                                //   at 0 (Group 1's JS manual clock, or a DRIVER with the same id and its
                                //   output pin in the clock's net), every REGISTER gate lp.CURRENT_VALUE "0",
                                //   each switch's OUTPUT_NUM from base (port index -> "0"/"1"); settle. -> settled?
  set(port, v),                 // setParam(id, "OUTPUT_NUM", v)
  settle(),                     // settle(1000) -> settled?
  pulse(),                      // -> settled?
  read(port),                   // "01Z!X"[state of the light's net], "-" when it has none
}
```

The worker learns a new message: `{ t: "check", id, spec, ports, key, names }`
→ `{ t: "check", id, result }` (or `{ t: "check", id, error }`), on its own
copies, like `{ t: "table" }`: the board's circuit keeps running untouched.
The page builds `spec` and `ports` as sim-truth.js's `plan()` does (all
pages' circuit; the page's switches and LEDs, every one, named and ordered as
there, with `labeled` set when the name came from the part's own name or a
label).

The sheet: Check is a fourth tab in the truth table sheet, and opens on its
own when the page has no switches (a counter has none, so there is no truth
table); see §14.

## 13. The shared test cases

`tests/check-sequential/cases.json`:

```
{
  "format": 1,
  "about": "...",
  "detect":   [ { "text", "kind", "options" }, ... ],      34 texts and what §2 makes of them
  "cases":    [ { "id", "title", "page", "key", "names"?, "parsed"?, "circuit", "expect" }, ... ],
  "circuits": { "<name>": "<.cdl text>", ... }              32 circuits, as the Mac app saves them (v3)
}
```

- `circuit` names the circuit in `circuits` to open (`cl_document_open_text` on
  the Mac, `clFromCdl` on the website; open it as the app opens a file).
- `key` goes in the Check box as is; `names` (optional) is the by-hand names.
- `parsed` (formula cases only) is what the Mac's formula reader gives
  `cl_check_expected`; the website checks its own `specFromFormulas(
  CedarTruth.parseAll(key))` against it and then uses it.
- `expect` is the Result (§9.1, §12). Implementations MUST match every field
  exactly: strings character for character, notes in order, steps in order.

What they cover: correct 2- and 3-bit counters (D and J-K, binary and decimal
keys, arrows, a count read by the numbers in its lights' names), a counter
with one wrong wire (two of them), an arbitrary sequence (0, 3, 5, 6) right
and wrong, a repeating count started midway, an up/down counter (a `set:`
line, a switch left as it is, a timing table with UP changing), the library's
counting register (16 values), Moore and Mealy detectors (right and wrong, as
state tables and timing tables), J-K and T flip-flops (characteristic timing
and state tables, toggling), a shift register right and wrong, falling-edge
parts (a counter; a J-K with `reset:`; the power-on edge), a switch as the
clock (by name, by `clock:`), two clocks, a manual clock, a ring that
oscillates once Q is 1, a light through an AND gate with an open input (X), a
clocked gate-level latch (unknown start, unreachable start, a `reset:` line
that loads it), start states through PRE' (a count, a `start:` line, and
states set by PRE' in a state table walk), a state table that isn't
self-correcting, unreachable rows, don't-cares, names by hand, every error
code, and today's kinds (formulas, minterms, matching by position, tables
right and wrong, a don't-care, names by hand).

**Regenerating** (only when this document changes; review the diff):

```
mac/build.sh && mac/Tools/build-tools.sh
mac/build/make_check_cases res/cl_gatedefs.xml tests/check-sequential/cases.json
```

It prints every case's verdict, summary and notes, and writes nothing if any
case disagrees with its hand-worked answer (verdict, error, the first wrong
step's pulse or row, its expected and got, the number of steps).

**Checked so far:** the reference on the Mac engine (every case, and its
output byte for byte the same on a second run); and 36 of the clocked cases
replayed step by step on CedarLogic Online's WebAssembly engine, power-on edge
included (every reading the same; the six not replayed need the start search
or a `clock:` line, which the replay didn't model).

**Tests to write** (Group 4 steps 2 and 3): a core test that runs every case
through `cl_check_key_kind`, `cl_check_clocked` (or today's calls) and
compares every field; a node test on the website that runs every case
through `CedarLogicCheck` on the real engine and compares every field, and
the `detect` list on both.

## 14. For the apps

Not part of the shared behavior, but things the implementers will meet:

- **The Mac's Check tab** lives in the truth table window, and
  `cl_truth_table` refuses a page with no switches, which is every counter.
  A clocked check doesn't need a truth table: the window should open (or a
  Check window) when a page has lights and a clock but no switches. The
  formula/table chooser can become the detected kind, shown as a label ("Read
  as a count"); `CheckMemory` keeps the text as now.
- **The step strip** (Mac): one column per step, the signals as rows (inputs,
  state, next state, outputs), expected over got, the first wrong step
  highlighted and scrolled to; counts show the number above the bits. The
  website's sheet shows the same as a table.
- Placeholders could rotate through one example of each kind.
- The check never changes the student's circuit and doesn't need the
  simulation paused; Step Clock and the check share only the idea of a pulse.
