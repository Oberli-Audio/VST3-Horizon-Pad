#!/usr/bin/env python3
"""Write the playbook's document templates into a project's docs/ folder.

The templates live in the playbook itself (F.15.2 brief, F.15.6 gate status, D.9.x the
rest), so there is one source and no copies to drift. This script extracts them.

    python3 new_project_docs.py <project>/docs            # writes missing files only
    python3 new_project_docs.py <project>/docs --list     # show what it would write
    python3 new_project_docs.py <project>/docs --force    # overwrite (asks nothing - careful)
"""
import re
import sys
from pathlib import Path

DOCS = Path(__file__).resolve().parents[2] / "references"   # the skill's playbook files
# output file -> (playbook file, heading that contains the template's code block)
TEMPLATES = {
    "brief.md": ("dsp-foundations.md", "### F.15.2"),
    "gate-status.md": ("dsp-foundations.md", "### F.15.6"),
    "g1-algorithm.md": ("dsp-development.md", "### D.9.1"),
    "g3-measurements.md": ("dsp-development.md", "### D.9.2"),
    "g5-null-test.md": ("dsp-development.md", "### D.9.3"),
    "exceptions.md": ("dsp-development.md", "### D.9.4"),
    "test-plan.md": ("dsp-development.md", "### D.9.5"),
    "measurement-report.md": ("dsp-development.md", "### D.9.6"),
    "handoff.md": ("dsp-development.md", "### D.9.7"),
    "decision-record.md": ("dsp-development.md", "### D.9.8"),
    "README.md": ("dsp-development.md", "### D.9.9"),
    "notices.toml": ("dsp-development.md", "### D.9.10"),   # THIRD_PARTY_NOTICES.md is generated from it by ship-bob notices.py
}


def template(fname, heading):
    text = (DOCS / fname).read_text(encoding="utf-8")
    start = text.index(heading)
    # the template is the first fenced block after the heading (templates contain their
    # own "## " lines, so the section cannot be cut at the next heading)
    m = re.search(r"```(?:markdown|yaml|toml)\n(.*?)\n```", text[start:], re.S)
    if not m:
        sys.exit("no template code block under %s in %s" % (heading, fname))
    body = m.group(1) + "\n"
    if fname == "dsp-foundations.md" and heading.endswith("F.15.2"):
        body = "# Sonic brief (owner-confirmed; amend below, never edit the YAML)\n\n```yaml\n" + body + "\n```\n\n## Amendments\n"
    if heading.endswith("D.9.4"):
        body = "# Exceptions (append-only, F.18.5)\n\n" + body
    return body


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    target = Path(sys.argv[1])
    listing, force = "--list" in sys.argv, "--force" in sys.argv
    target.mkdir(parents=True, exist_ok=True)
    for out, (fname, heading) in TEMPLATES.items():
        body = template(fname, heading)
        path = target / out
        if listing:
            print("%-24s <- %s %s (%d lines)" % (out, fname, heading, body.count("\n")))
            continue
        if path.exists() and not force:
            print("kept     %s (exists)" % path)
            continue
        path.write_text(body, encoding="utf-8")
        print("written  %s" % path)


if __name__ == "__main__":
    main()
