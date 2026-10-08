# Page link groups

TO and FROM parts (`DE_TO`, `DA_FROM`) connect by name (their `JUNCTION_ID`).
Until now every page of a circuit was one big namespace. A **link group** is a
number on each page: pages with the same number connect by name, pages with
different numbers never do, even when the names match. Example: pages 1+2
share, 3+6+7 share, 4 is on its own.

Only *which pages share a number* means anything. The numbers themselves are
renumbered whenever they are written.

## 1. On file (v3 `.cdl`)

Each page may hold one extra child, `(linkgroup N)`:

    (page 2
      (name "Decoder")
      (linkgroup 1)
      (gate ...)
      ...)

- `N` is a whole number written in decimal digits (0-9999). A page with no
  `(linkgroup ...)` is in group **0**.
- **Writers** number the groups in page order: the first page's group is 0,
  the next new group they meet is 1, then 2, and so on. They write
  `(linkgroup N)` only when N is not 0, right after `(name ...)` (or straight
  after the page index when there is no name). So:
  - a circuit whose pages all connect writes **nothing new**: its bytes are
    exactly what every earlier version wrote, and
  - one partition always has one text.
- **Readers** take any `N` and compare numbers only for equality. A value that
  is not 1-4 decimal digits (a list, a sign, a fraction, a word) reads as 0;
  it is never an error. When a page holds more than one, the last counts.
- The legacy XML formats (v1/v2) have no place for groups; saving to them
  connects every page.

Fixtures: `format/tests/fixtures/pagelinks/` (three pages: none, `0 0 1`,
`0 1 2`; and a truth table that passes through a second page).

## 2. Older readers

Every reader of v3 before this ignores children of `(page ...)` it doesn't
know (`format/circuit_file_io.cpp: readCircuitFile` reads only `name`, `gate`,
`wire` and the drawing). So an older app or site **opens the file without a
word and connects every page**, as it always did. If it saves, the groups are
gone (it never had them). `mac/Tools/pagelinks-old-readers.sh` checks that the
format library from before groups reads every sample and finds the same pages,
gates and wires, and that read-then-write of every v3 fixture without groups
gives the same bytes as before.

The wx app reads and keeps the number (`GUICanvas::linkGroup`) and writes it
back, but still connects every page while it runs.

## 3. The engine

The logic engine knows nothing of pages. The Mac core (`LogicHost`) hands it a
TO/FROM's name as `linkKey(group, name)`: the plain name for group 0 and
`"\x1f" + group + "\x1f" + name` for any other, so names can only meet within a
group. When groups change (`relinkJunctions`) every TO/FROM's name is handed
over again; the engine moves the label's wires to its new junction. A file is
renumbered from 0 on opening, so gates made later agree. The oscilloscope and
Find still list a name once, whichever groups it is in.

Truth tables and Check My Circuit read the running circuit (or a copy made by
saving and reopening it), so they see only the pages connected to the page
checked.

## 4. In the Mac app

- Settings > Canvas > New pages: *Share links with the other pages* (the
  default, today's behaviour) or *Start on their own*. Sharing joins the
  group most pages are in (the first page's on a tie); when no two pages
  share, a new page starts on its own.
- File > Connect All Pages / Disconnect All Pages.
- Right-click a tab > Connect to: a checklist of the other pages. Ticking a
  page puts it with this one (it leaves its old group); unticking takes it
  out (it stays in a group of its own numbers).
- Tabs in a group of two or more pages show a small dot in that group's
  colour; there are no dots while every page connects.
- Every change is one undo step (`Connect Pages`, `Connect All Pages`,
  `Disconnect All Pages`).

## 5. Sync

Sync carries the circuit file, so groups travel with it. The structure digest
(`docs/SYNC.md` 2.4) gains, only when the pages are **not** all in one group, a
line per page:

    L <page index> <n>

where `n` numbers the groups 0, 1, 2... in the order the pages come in the
file (the same numbering writers use). Circuits without groups keep their
structure text and hash. Other clients that compute the digest (the website's
`sync-core.js`, `ref/clsync.py`) need the same rule, or a circuit with groups
reads as changed between them (a spurious version, never lost data).

## 6. Share links

A Share Link carries the same v3 text (`cl_document_save_text_ex`), so the
groups go with it in the same `(linkgroup N)` form.
