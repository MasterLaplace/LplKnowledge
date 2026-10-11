# Changelog

What changed in each release, generated from the commit titles on `main`. Regenerate it with
`tools/changelog.sh` from [MasterLaplace/.github](https://github.com/MasterLaplace/.github);
an edit by hand is lost at the next release, whose check refuses a file that differs from
what the history gives.

## [0.2.1] - 2026-10-08

### Fixed

- **harvest**: Stop the TEI reader when two mentions collide on one identifier (#106)

## [0.2.0] - 2026-10-08

### Added

- **tests**: Declare gate P18 corpus once, for the host and ring 0 (#105)

### Documentation

- Call it the full validation, as the issues do (#103)

## [0.1.0] - 2026-10-06

### Added

- **config**: LplKnowledge states its version and checks the LplPlugin it builds with (#96)
- **knowledge**: The library acquires, indexes and hands over a real corpus
- Le corpus, le format .lplknow et la gate P18

### Fixed

- **config**: LplPlugin's header is lplplugin/config.h (#98)

### Documentation

- **readme**: Rewrite the README in English, in the four parts a README holds (#92)

### Housekeeping

- **license**: Name the copyright holder and add a citation file (#91)
