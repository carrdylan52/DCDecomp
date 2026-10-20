# Ixion crossover fork

This branch is the pinned source dependency of Ixion's `showcase-crossover`
branch. Read the parent project's README and docs/SHOWCASE.md for build, local
pack preparation and known limitations. Upstream port instructions are in
README.md and docs/PC.md. This candidate uses the older binary-asset generation;
do not mix it with current upstream data or use `submodule update --remote`.

## Build and tests

Build the standalone port with its normal Windows launcher first. Add
`IXION_BUILD_PLUGIN=ON`, `IXION_API_DIR=<Ixion>/include` and the sibling donor
source directory as described by Ixion. Donor provenance checks must remain
active. Build `darkcloud` and the matching Ixion DLL together. Keep retail `ps2/`
source unchanged; crossover hooks belong in `port/src` or the isolated donor
library.

Configure a separate test build with `DC_BUILD_TESTS=ON`, then build
`darkcloud_tests` and any discovered donor test targets. Run focused native Ixion
cases with the test binary's `--gtest_filter=*Ixion*`; inspect the test listing
before choosing additional filters. The donor archive build runs its symbol and source-provenance gate automatically.
The separate `symbol_gate_test.py` negative tests require a compiler, nm, ar and
the built donor archive/DLL; use `--help` for the required paths. They are direct
scripts, not unittest-discovery modules.

The full native suites and rendered replays need the documented build/runtime
requirements. Report skips and retain the distinction between synthetic tests,
rendered runs and manual gameplay acceptance. Use cached dependencies for
repeated builds; offline mode also needs cached vendor headers and Cargo crates.

## Data and publication

No discs, extracted assets, converted packs, saves or runtime logs enter Git.
Use fresh disposable save/run directories. Never overwrite an existing extraction
or campaign save. Paths are local arguments/environment settings.

There is no deployment service. Publish dependency branches before parent pins,
preserve upstream licences and scan source changes before publication. To roll
back, check out the prior parent pin in an idle checkout, update recursive
submodules and rebuild into a new directory. Keep the upstream default branch
unchanged.
