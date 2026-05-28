# The transcription-interrogation method

The two-actor loop. Claude writes the code; you transcribe it and
interrogate every line you couldn't have written yourself. Work proceeds
step by step, not exercise by exercise — one small chunk at a time.

**Layout assumption:** `EXERCISES.md`, `METHOD.md`, and `.solutions/`
(the reference-implementation dir you transcribe from) all live in this
repo. Your transcribed code lives alongside them in `qemu-device/` and
`pf-driver/`. Paths in exercises are relative to the repo root unless
noted.

## Per exercise

1. **Predict.** Fill the Predict block before any code is written. Wrong
   predictions are higher-retention than right ones — write them down
   even when uncertain.

2. **Step-loop** until the exercise's file is complete:

   a. **Claude appends the next chunk** to `.solutions/<track>/<file>`
      and prints it in the chat so you transcribe from there — one
      construct, not the whole exercise. A struct, a function, a
      capability registration. Just enough to introduce one or two new
      ideas at a time. `.solutions/` is always exactly the floor for the
      step you are on — never further. Alongside the chunk, Claude gives
      a concise tutorial: what the construct is, why it's shaped this
      way, which one or two new ideas it introduces, and the specific
      lines worth interrogating in step (b). Tight — a few sentences per
      idea, not a textbook chapter.

   b. **You transcribe and interrogate** in one pass. Type the chunk by
      hand into the matching source dir (`qemu-device/` or `pf-driver/`),
      and stop on any macro, cast, field order, or call you couldn't have
      written yourself — ask before moving on.
      Specific questions ("why this, not that?") force a real model;
      vague questions ("what is this?") get vague answers.

   c. **Run it.** Build at minimum; run the functional check for the
      chunk if it produces a testable change (a new BAR shows up in
      `lspci`, a new register reads back, an IRQ fires). Incremental
      verification turns "the whole exercise is broken" into "the line I
      just typed is broken."

   d. **Loop back to (a).** Claude writes the next chunk only after the
      previous one is transcribed, understood, and running.

3. **Verify** with the commands in the Verify block. If they fail, debug
   before moving on.

4. **Reflect.** Three sentences past-you would want to read. Seed
   material for the blog post.

5. **Reference.** Diff your transcribed version against the QEMU/Linux
   source files called out per exercise.

Each exercise has a `- [ ] Done` checkbox. Tick it when the full loop
is complete.

## Cadence

Roughly one sub-exercise per day (E1.1 on day 1, E1.2 on day 2, and so
on). One chunk-loop is the unit of progress; the rest of the day is for
the questions it surfaces, the reading it points you at, and the Reflect
block while the experience is fresh. Doing two in a day is fine when a
chunk is small; doing zero is fine when the interrogate step opens a
rabbit hole worth chasing. Don't batch chunks to "make up ground" — the
method's whole leverage is the slow pass.
