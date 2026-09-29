# Architecture diagrams

Pictures of how LatiBot is put together, drawn from the code. There are
three documents, each at a different level of detail:

| Document | Question it answers | Diagram types |
| --- | --- | --- |
| [Components.md](Components.md) | What are the big pieces, and which talks to which? | flowcharts |
| [Classes.md](Classes.md) | Which classes and structs exist, and how do they relate? | class diagrams, one flowchart |
| [Execution_Flow.md](Execution_Flow.md) | Starting at `main()`, what runs, in what order, on which thread? | flowcharts, sequence diagrams |

Read them in that order the first time. After that, each section stands on
its own. Every name in a diagram is the name in the code, so searching the
source for a box's label finds the code behind it.

Third-party code (DPP, SQLite, DECtalk, OpenSSL) is drawn as a closed box:
what goes in and what comes out, never what happens inside.

## Viewing them

The diagrams are [Mermaid](https://mermaid.js.org/), written as text inside
the Markdown.

- **GitHub** draws them when you open the file.
- **VS Code's** built-in Markdown preview does not. Install the extension
  *Markdown Preview Mermaid Support* (`bierner.markdown-mermaid`), then
  open the preview with `Ctrl+Shift+V`.
- **Anywhere else**, paste a diagram's text into
  [mermaid.live](https://mermaid.live) to view it, or to edit it.

## How to read each kind of diagram

### Flowchart

Boxes are things or steps. Arrows point the way something flows: a call, a
message, data. A labelled arrow says what flows along it. A shaded frame
around a group of boxes (a *subgraph*) just groups them, for example by
folder or by thread.

| Shape | Means |
| --- | --- |
| `[ rectangle ]` | a piece of our code, or a step in it |
| `{ diamond }` | a decision; each arrow out of it is labelled with the answer that takes it |
| `[( cylinder )]` | somewhere data is kept: a file, a database, a table |
| `[[ double-edged box ]]` | third-party code, treated as a closed box |
| `([ rounded box ])` | the start or the end of a flow, or a person |
| solid arrow `-->` | calls, or flows to |
| dotted arrow `-.->` | a looser link: implements, or happens later, as the label says |

### Class diagram (UML)

Each box is a class or struct. The top part is its name, the middle its
fields, the bottom its functions. Only the members that explain the design
are drawn, never every one.

- `+` is public, `-` is private.
- `<<interface>>` marks an abstract class of pure virtual functions (a *port*,
  see Components.md).
- `<<struct>>` is plain data. `<<variant>>` is a `std::variant` of the types
  it points to.
- `~T~` is a template argument, so `vector~trigger~` means `std::vector<trigger>`.

The lines between boxes say how two classes relate:

| Line | Reads as | In C++ terms |
| --- | --- | --- |
| `A <\|-- B` (hollow triangle) | B **is an** A | `class B : public A` |
| `A <\|.. B` (hollow triangle, dotted) | B **implements** interface A | `class B final : public A`, where A is all pure virtual |
| `A *-- B` (filled diamond at A) | A **owns** B | B is a member held by value, or by `unique_ptr`, and dies with A |
| `A o-- B` (hollow diamond at A) | A **holds** B without owning it | a pointer or reference member; something else owns B |
| `A --> B` | A **uses** B | A holds B, or a collection of B, as data |
| `A ..> B` (dotted) | A **depends on** B | A takes or returns a B, but keeps none |

Numbers at a line's ends are counts: `"1" --> "*"` is one-to-many.

### Sequence diagram

The boxes across the top are objects, each with a line going down: its
*lifeline*. Time runs down the page. A solid arrow is a call or a message, and
a dashed arrow is what comes back. A thin bar on a lifeline shows when that
object is busy. The labelled frames group calls:

- `loop` repeats what is inside.
- `alt` / `else` chooses one branch.
- `opt` may not happen at all.
- `par` runs its branches at the same time.

Sequence diagrams are used where the order and the thread something happens
on matter: coroutines that pause, worker threads, and replies that arrive later.

## Keeping them current

These are drawn by hand from commit `4848a7e`. Nothing checks them against the
code, so a renamed class or a new subsystem needs its box changed here too.
Each diagram is small and about one thing, so a change usually touches one of
them.
