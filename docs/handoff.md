# Handoff: Round 14 audit (2026-10-21)

Tier P, track I. The Round 14 checklist (claude.ai doc "Round 14/14: Horizon Pad check")
run top to bottom with sound-design-bob, ux-ui-design-bob and ship-bob. Findings table and
exception IDs go back into that doc and to the owner; skill bugs are reported, not patched
here (the owner fixes the skills).

## Owner decisions taken during the run (2026-10-21)

- VERSION 0.1.0 (SemVer 0.y.z while friends test); state versioning starts now, follow the
  skills (supersedes "no backwards compatibility until told").
- Parameters 0..1 -> 0..100 (S.1): state version 2 with a migration (v1 values x100), a
  corrupted-state test, goldens replaced only after a null test shows the audio unchanged;
  changelog carries a state-format note, not "Sounds different". ATTACK/RELEASE keep their
  hand-written time display (paramgen has no display column yet; the owner fixes the skill).
- Plugin codes stay Qumu / Hzpd / com.quellemusic.horizonpad (README codes table added with the D.9.9 rewrite).
- dspVersion in the state, getNumPrograms() = 1, editor scale machine-local only.
- Presets as files now (S.4): .quellepreset per preset, Factory/ per version, User/.

## Plan (numbered; tick as done) - paused 2026-10-22 by the owner

1. [x] Step 0 (0.1-0.8): all done; JUCE 8.0.16 bit-identical on 17 presets.
2. [x] S.1 0..100 + parameters.csv + Params.h + docs/parameters.md; test parameter_table_matches_plugin.
3. [x] S.2/S.3 state v2 (version, dspVersion, Extra, buffers by ID, v1 migration, truncation refused),
   getNumPrograms 1, editor scale machine-local; golden states tools/tests/state; 4 state tests.
4. [x] S.4 presets as files (presets/, presets.csv, BinaryData, Factory/.version, User/, legacy XML
   migrated, import/export API); Lagerfeuer = init (owner). presetlint bank OK with
   --categories Pad,In-between,Lead. presetlint render NOT run yet.
5. [x] S.5 harness contract v3 (+ fixed: blocks larger than prepared; test larger_block_than_prepared);
   tools/tests/test_harness_contract.py (kit scaffold) fast + render in CTest.
6. [x] S.6 validators pass (Steinberg 47/0, pluginval L10 six rates); malloc trap in the tool
   (self-test aborts on both slices). EX-006 (ASan not run).
7. [x] S.7 perf under Rosetta -> EX-007; docs/perf.md. S.8 goldens tools/tests/golden (x86_64,
   bit-identical to the pre-round build). S.9 test output_dc_and_knee. S.10 CHANGELOG.md.
   S.11 EX-002 amended, EX-004..EX-007 written.
8. [x] U.1 200 % step, tooltip fixed (EX-008: content-component transform, the plugin
   drives setScaleFactor). U.2 docs/brand-sheet.md (EX-009 footer 3.54:1, EX-010 active pill
   3.19:1); EX-011 bank loudness span. U.3 tooltips from Params::tooltipFor (+ setHelpText).
9. [x] U.4 About panel (Diagnostics, AboutPanel, DocsData; test about_copy_is_fourteen_lines).
10. [x] U.5 code ok (Live row: owner); U.6 Browse menu + import/export/drop (test
    preset_file_round_trip); U.7 --reopen=20 flat at 47 MB (Live row: owner); U.8 German quick start.
11. [ ] R.1 pass, R.2 clean-clone preset build ok (lipo x86_64 arm64), R.3 only "not tagged",
    R.4 blocked only by shipcheck's removed-parameter false positives, R.5 README from D.9.9 +
    DEVELOPING.md, R.9 message ok, R.10 docs/feedback.md, R.11 Issues off + main protected.
    R.6/R.7: dry run v0.0.0-test (second run after the MSVC fix, 0a23018) - check the draft's
    assets with release-assets, then DELETE the draft and the tag. R.8: owner (Live 10.1 Rosetta).
12. [ ] Findings table into the Round 14 doc; skill-bug list for the owner.

Notes: caffeinate -i every long run (the Mac idle-sleeps and CTest times out); the tool's LTO link
takes ~10 min; scratch preset root via HORIZON_PRESET_ROOT; x86_64 harness wrapper exec's
`arch -x86_64 build-release/HorizonPadSoundTool` (python under Rosetta has no numpy).

Baselines for the null tests: seeded renders of all 17 presets (notes 36,60,64,67,74, hold 4,
tail 6, seed 1) from the JUCE 8.0.4 build and the 8.0.16 build, bit-identical to each other.
