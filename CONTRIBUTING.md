# Contributing

Thanks for helping. The aim is to run Off-Road as if the best programmers had rebuilt it with unlimited time,
without ever changing how it plays.

## The rules

- **No game files, ever.** Nothing from the game goes into the repository or into an issue: no executables, data,
  textures, models, sounds, music, movies, extracted tables, and no translated C (`gen/` is made on each player's
  machine and is never committed). Hand-written code that does what a game function does is fine. Addresses,
  sizes and descriptions of the game's code are fine.
- **Gameplay never changes.** Timing, physics, the 60 ticks a second and the AI stay exactly as the original.
  Graphics and options may change how it looks, never how it plays.
- **The checks must pass.** Run `tools/dev/translator_check.sh` before sending a change that touches the
  translator, the runtime or the game's code paths. It has to report no new failing functions and an identical
  lockstep run.
- **Settings belong in the game's own menus.** A new option is a row in Game Options, drawn in the game's style,
  never only an environment variable, a launcher or a file to edit. The original look stays one setting away.
- **Every controller works.** Any controller drives player 1, plugged in before or after the game starts, in any
  order, with the keyboard still working; the second one connected drives player 2 only in a two-player race.
- **Every enhancement can be turned off,** and what's on by default has to hold the full frame rate on a
  Radeon 890M handheld (the GPD Win Max 2) and the Steam Deck.
- **Measure.** Speed changes come with before and after numbers (frame times, CPU and GPU time) on the same route.
- **Write it down.** `DESIGN.md` is the project's record: add what you found and what you changed, and mark claims
  you haven't checked as [guess] and checked ones as [verified], with the evidence.

## How to send a change

Open an issue first for anything big, so the approach can be agreed. Keep a pull request to one subject, describe
how you tested it (which track, car and settings), and attach frame captures for anything visual. Pull requests are
read in full before anything in them is built or run.
