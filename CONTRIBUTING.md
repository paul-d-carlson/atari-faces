# Contributing

Thanks for your interest in atari-faces. Bug reports, fixes and tests are
welcome.

## Before you start

The demo fills a 48K Atari 800 almost completely: the build prints how
many bytes are left below the C stack (about 200). Every byte of code or
static data a change adds comes out of that margin, and the linker refuses
an image that does not fit. Prefer changes that keep `faces.xex` the same
size or make it smaller, and say in your pull request how the size
changed.

Speed matters too: the integration test fails if training takes more than
60 s of math, a match more than 3 s, or the screen stands still for more
than 1.5 s.

## Workflow

1. Build and test on a clean checkout first, so you know the baseline:

   ```
   make deps
   make test
   ```

   Note the `MEM` line printed when `faces.xex` is linked, and the timing
   table printed by `make test-integration`.

2. Make your change, then run `make test` again. All tests must pass.
   Compare the free memory and timings with your baseline. CI runs the
   same tests on every pull request and shows these figures in the run
   summary.

3. Add or update tests for any behavior you change:
   - a unit test in the matching `tests/unit/test_<module>.c`. These run
     natively (`make test-host`, with sanitizers, in under a second) and
     on the 6502 (`make test-target`); a new suite also needs an entry in
     `tests/unit/suites.h`, `tests/host/main_host.c` and the matching
     `tests/target/main_*.c` and `mk/test-target.mk` image;
   - a check in `tests/driver/demo_runner.c` for anything that changes
     what the demo shows or how long it takes;
   - a test in `tools/test_export_faces.py` for changes to the exporter.

4. If you change the data or the exporter, run `make data` and commit the
   regenerated `data/FACES.DAT` and `tests/fixtures/ef_reference.h`
   together.

5. Update [CHANGELOG.md](CHANGELOG.md) under an `Unreleased` heading.

## Code style

- Portable C89 that compiles with cc65 and, for the host tests, with a
  C99 compiler under `-pedantic`. Four-space indentation, no tabs (see
  [.editorconfig](.editorconfig)).
- No floating point on the Atari. Keep values in 8 or 16 bits where you
  can: `int` is 16 bits on cc65, and the host build's `EF_CHECK_RANGE`
  fails any value that would not fit.
- Use `unsigned char` for values that fit in 8 bits; 6502 code for `int`
  is much larger. Make sure loop counters are wide enough for their bounds.
- Avoid large automatic arrays: the C stack is 512 bytes.
- Reach Atari hardware only through `src/hw.h`, so the code still builds
  natively for the host tests.

## Changing the memory layout

`src/atari.cfg` fixes where the demo loads (just above DOS 2.5's MEMLO)
and where the screen and C stack live. Read the comments in it and in
[third_party/atari-dos25/README.md](third_party/atari-dos25/README.md)
before changing it, and run the full test suite afterwards.

## Commit messages

Use a short summary line in the imperative ("Speed up the covariance
loop"), a blank line, then an explanation of what changed and why,
including size or timing effects where relevant.
