#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The two books (task 0386): the references list and the index tables generated from the sources that cite things,
the figures taken headless, the PDFs built with latexmk.

    python tools/make_books.py --bib            write docs/book/refs.bib and docs/book/generated/*.tex
    python tools/make_books.py --check          regenerate to a temp dir and fail on drift, an uncited entry or a
                                                \\cite of a key that no .bib holds (no TeX needed)
    python tools/make_books.py --check-cited    the same without the drift check (build.yml: the committed files
                                                agree with each other; a new citation elsewhere waits for --bib)
    python tools/make_books.py --screenshots <Blendity exe>   the figures, from a scratch project's starter scene
    python tools/make_books.py --pdf [--wsl]    latexmk both books (through WSL with --wsl) and copy the PDFs to docs/

Sources of references (every one ends in refs.bib, deduplicated by title):
  src/editor/lessons.cpp      the Learn lessons' "> " lines (papers, books' chapters, manuals) and "@ " lines (Blender's source)
  research/FEATURES.md        the paper -> feature table
  licenses/README.md          every library, with its licence and link
  docs/book/refs_extra.bib    hand-written: the reference books, standards, the sources the architecture's tables name
Stdlib only."""
import argparse
import difflib
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOOK = os.path.join(ROOT, "docs", "book")
GEN = os.path.join(BOOK, "generated")
LESSONS = os.path.join(ROOT, "src", "editor", "lessons.cpp")
FEATURES = os.path.join(ROOT, "research", "FEATURES.md")
LICENSES = os.path.join(ROOT, "licenses", "README.md")
EXTRA_BIB = os.path.join(BOOK, "refs_extra.bib")
BOOKS = ["architecture", "user-guide"]
PDF_NAMES = {"architecture": "Blendity-Architecture.pdf", "user-guide": "Blendity-User-Guide.pdf"}

# The reference books the lessons cite by tag: their refs_extra.bib keys.
BOOK_TAGS = {"FoCG": "MarschnerShirley2021", "GEA1": "Gregory2018vol1", "GEA2": "Gregory2018vol2"}


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def norm_title(t):
    return re.sub(r"[^a-z0-9]+", "", t.lower())


def tex_escape(s):
    """Plain text into LaTeX: the characters LaTeX gives a meaning."""
    out = []
    for ch in s:
        if ch in "&%$#_{}":
            out.append("\\" + ch)
        elif ch == "~":
            out.append("\\textasciitilde{}")
        elif ch == "^":
            out.append("\\textasciicircum{}")
        elif ch == "\\":
            out.append("\\textbackslash{}")
        else:
            out.append(ch)
    return "".join(out)


def bib_escape(s, field=""):
    """A field's text as biblatex will typeset it: LaTeX's specials escaped (a url is left alone)."""
    if field == "url":
        return s
    s = s.replace("`", "")  # Markdown's code marks would typeset as quotes
    return s.replace("\\", "\\textbackslash{}").replace("&", "\\&").replace("%", "\\%").replace("#", "\\#").replace("_", "\\_").replace("$", "\\$")


ASCII = str.maketrans("éèêëáàâäãåíìîïóòôöõúùûüçñýÿÉÈÊËÁÀÂÄÍÌÎÏÓÒÔÖÚÙÛÜÇÑ", "eeeeaaaaaaiiiiooooouuuucnyyEEEEAAAAIIIIOOOOUUUUCN")


def key_for(author_text, year, title):
    """A citation key from the first author's surname, the year and the title's first telling word."""
    author_text = author_text.translate(ASCII)
    orgs = {"The Khronos Group": "Khronos", "W3C": "W3C", "The Linux Foundation": "Linux"}
    first = re.split(r",| &| and |;", author_text.strip())[0].strip()
    first = re.sub(r"\s+et al\.?$", "", first)
    if first in orgs:
        surname = orgs[first]
    else:
        surname = re.sub(r"[^A-Za-z]", "", first.split()[-1] if first.split() else "Anon")
    if not surname or len(surname) < 2:
        surname = re.sub(r"[^A-Za-z]", "", first) or "Anon"
    words = [w for w in re.findall(r"[A-Za-z]+", title) if w.lower() not in ("a", "an", "the", "of", "for", "on", "in", "and", "to", "via", "from", "with")]
    word = words[0].capitalize() if words else "Ref"
    return f"{surname}{year}{word}"


INITIALS_RE = re.compile(r"^(?:[A-Z]\.(?:\s?-?[A-Z]\.)*)$")


def bib_authors(s):
    """'Lévy, B., Petitjean, S., Ray, N. & Maillot, J.' / 'Hosek & Wilkie' / 'Kang, B. et al.' / 'The Khronos Group'
    into biblatex's 'Last, First and Last, First', organisations braced, 'et al.' as 'and others'."""
    s = s.strip()
    others = bool(re.search(r"\bet al\.?$", s))
    s = re.sub(r"\s*\bet al\.?$", "", s)
    if re.match(r"^(The |W3C$|Khronos|Free Software|American |Academy )", s) or (" " in s and "," not in s and "&" not in s and not re.search(r"\b[A-Z]\.", s)):
        return "{" + s + "}"
    tokens = [t.strip() for t in re.split(r",|&|\band\b", s) if t.strip()]
    names = []
    for t in tokens:
        if INITIALS_RE.match(t) and names:
            names[-1] = names[-1] + ", " + t
        else:
            names.append(t)
    if others:
        names.append("others")
    return " and ".join(names)


class Bib:
    def __init__(self):
        self.entries = {}  # key -> dict(type, fields, sources)
        self.by_title = {}

    def add(self, etype, key, fields, source, keyword):
        t = norm_title(fields.get("title", key))
        if t in self.by_title:
            e = self.entries[self.by_title[t]]
            e["sources"].add(source)
            for k, v in fields.items():  # the longer form wins (a fuller venue, an author list)
                if len(v) > len(e["fields"].get(k, "")):
                    e["fields"][k] = v
            return self.by_title[t]
        base = key
        n = 2
        while key in self.entries:
            key = f"{base}{chr(ord('a') + n - 2)}"
            n += 1
        fields = dict(fields)
        fields["keywords"] = keyword
        self.entries[key] = {"type": etype, "fields": fields, "sources": {source}}
        self.by_title[t] = key
        return key

    def write(self, path, extra_text):
        lines = ["% GENERATED by tools/make_books.py --bib from src/editor/lessons.cpp, research/FEATURES.md and",
                 "% licenses/README.md; refs_extra.bib holds the hand-written entries. Do not edit: regenerate.", ""]
        for key in sorted(self.entries):
            e = self.entries[key]
            lines.append(f"@{e['type']}{{{key},")
            for k in sorted(e["fields"]):
                if k != "note":
                    lines.append(f"  {k} = {{{bib_escape(e['fields'][k], k)}}},")
            note = e["fields"].get("note", "").rstrip(".")
            lines.append(f"  note = {{{bib_escape(note) + '. ' if note else ''}Cited by {', '.join(sorted(e['sources']))}.}}")
            lines.append("}")
            lines.append("")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines))


# --- the lessons -------------------------------------------------------------------------------------------------

PAPER_RE = re.compile(r"^(?P<authors>.+?)\s\((?P<year>\d{4}[a-z]?)\)\s(?P<title>[^.]+?)\.\s*(?P<venue>.*)$")
MANUAL_RE = re.compile(r"^(?P<what>Unity Manual|Blender Manual|Godot Docs|Unreal Engine docs|Unreal Engine|VRChat Creator Docs|VRChat Creators|CATS Blender Plugin)[:,]\s*(?P<rest>.+)$")


def lessons():
    """[(title, [ref lines], [source lines])] from lessons.cpp's lesson table."""
    text = read(LESSONS)
    out = []
    for m in re.finditer(r'\{"([^"]+)", "([^"]*)", R"\((.*?)\)"\}', text, re.S):
        title, body = m.group(1), m.group(3)
        refs = [l[2:].strip() for l in body.splitlines() if l.startswith("> ")]
        srcs = [l[2:].strip() for l in body.splitlines() if l.startswith("@ ")]
        out.append((title, refs, srcs))
    if len(out) < 40:
        sys.exit(f"make_books: only {len(out)} lessons parsed from lessons.cpp (the table's form changed?)")
    return out


def split_manual_refs(rest):
    """'Collider components; Lighting > Static GameObjects (docs.unity3d.com)' -> [(title, domain)] per ';' piece."""
    pieces = []
    domain = None
    m = re.search(r"\(([a-z0-9.\-]+\.[a-z]{2,}(?:/[^\s)]*)?)(?:,\s*MIT)?\)\s*$", rest)
    if m:
        domain = m.group(1)
        rest = rest[: m.start()].strip()
    for piece in re.split(r";\s*", rest):
        piece = piece.strip().rstrip(".")
        if not piece:
            continue
        sub = re.match(r"^(Godot Docs|Unreal|Blender Manual|Unity Manual|VRChat Creators?(?: Docs)?)[:,]\s*(.+)$", piece)
        pieces.append((piece, domain) if not sub else (f"{sub.group(1)}: {sub.group(2)}", None))
    return pieces


# Lessons whose text names no paper or manual: the references of the ADRs and tasks they teach (refs_extra.bib keys and
# generated keys), so the UI index leaves no lesson without its sources.
LESSON_EXTRA = {
    "Overlays: See the Mesh, Hide the Rig": ["UnityManual2026", "BlenderManual2026"],
    "Editing: Develop a Render": ["ASCCDL", "ACES2026"],
    "Drawing in Perspective": ["Igarashi1997Interactive", "Chen2013Sweep", "Gryaditskaya2020Lifting", "SketchBookDocs2026"],
    "Drawing like SketchBook": ["SketchBookDocs2026", "Krita2026", "PhotoshopHelp2026"],
    "Papers": ["SketchBookDocs2026", "PureRef2026"],
    "Blender's Shader Nodes in the Textures Window": ["Perlin2002Improving", "BlenderManual2026"],
    "Fog with a Volume Material": ["BlenderManual2026"],
    "Working Faster with Textures and Materials": ["BlenderManual2026", "SubstancePainterDocs2026"],
    "Flatten to the Camera (the 2D Look)": ["Motomura2015"],
    "Shapes by the Camera's Angle": ["Motomura2015"],
    "Animating Any Property": ["UnityManual2026", "BlenderManual2026"],
    "Customizing the Editor": ["ZBrushDocs2026", "BlenderManual2026"],
    "Sculpting": ["ZBrushDocs2026", "SculptrisDocs2026", "Jacobson2013Robust", "Gibson1998Constrained", "Huang2018Quadriflow"],
    "Painting the Surface": ["SubstancePainterDocs2026"],
    "The Hand-painted Look": ["Baxter2004Impasto"],
    "Play Mode and the Game Loop": ["UnityManual2026", "libJoltPhysics"],
    "Performance and Stress Testing": ["Blender2026"],
}


def add_lesson_refs(bib, title, refs):
    """A lesson's '> ' lines into bib entries; returns the keys it cites and its book-chapter notes."""
    keys, chapters = [], []
    for r in refs:
        tag = re.match(r"^\[(FoCG|GEA1|GEA2)\]\s*(.*)$", r)
        if tag:
            keys.append(BOOK_TAGS[tag.group(1)])
            if re.match(r"^(\d|ch\.)", tag.group(2).strip()):
                chapters.append((tag.group(1), tag.group(2).strip()))
            continue
        if r.startswith("docs/"):
            continue  # the project's own documents: the documentation index lists them
        man = MANUAL_RE.match(r)
        if man:
            what = man.group("what")
            for piece, domain in split_manual_refs(man.group("rest")):
                pd = re.search(r"\s*\(([a-z0-9.\-]+\.[a-z]{2,}(?:/[^\s)]*)?)\)\s*$", piece)  # a domain inside the piece
                if pd:
                    domain, piece = domain or pd.group(1), piece[: pd.start()].strip()
                full = piece if piece.lower().startswith(what.lower()[:5]) or ":" in piece else f"{what}: {piece}"
                url = f"https://{domain}" if domain else None
                fields = {"title": full, "year": "2026"}
                if url:
                    fields["url"] = url
                fields["organization"] = {"Unity Manual": "Unity Technologies", "Blender Manual": "Blender Foundation",
                                          "Godot Docs": "Godot Engine contributors", "Unreal Engine docs": "Epic Games",
                                          "Unreal Engine": "Epic Games", "VRChat Creator Docs": "VRChat Inc.",
                                          "VRChat Creators": "VRChat Inc.", "CATS Blender Plugin": "absolute-quantum (MIT)"}.get(what, what)
                k = re.sub(r"[^A-Za-z0-9]", "", full.split(":")[0].split()[0]) + re.sub(r"[^A-Za-z0-9]", "", full.split(":", 1)[-1])[:28]
                keys.append(bib.add("online", k, fields, f"Learn: {title}", "websites"))
            continue
        pm = PAPER_RE.match(r)
        if pm:
            authors, year, ptitle, venue = pm.group("authors"), pm.group("year"), pm.group("title"), pm.group("venue").strip().rstrip(".")
            etype = "thesis" if re.search(r"thesis", venue, re.I) else "article" if re.search(r"journal|transactions|forum|computer-aided design|siam", venue, re.I) else "inproceedings"
            fields = {"author": bib_authors(authors), "year": year, "title": ptitle}
            if venue:
                fields["journaltitle" if etype == "article" else "institution" if etype == "thesis" else "booktitle"] = venue
            if etype == "thesis":
                fields["type"] = "Master's thesis" if re.search(r"master|msc", venue, re.I) else "PhD thesis"
            keys.append(bib.add(etype, key_for(authors, year[:4], ptitle), fields, f"Learn: {title}", "papers"))
            continue
        # A documentation set named without a year ("lilToon, Poiyomi, MToon and UTS2 documentation; VRChat Creators:
        # PhysBones (creators.vrchat.com)"): the set as one online entry, the rest as manual pieces.
        ds = re.match(r"^(?P<what>.+?) documentation(?:;\s*(?P<rest>.+))?$", r)
        if ds:
            keys.append(bib.add("online", re.sub(r"[^A-Za-z0-9]", "", ds.group("what"))[:30] + "Docs", {"title": ds.group("what") + " documentation", "year": "2026"}, f"Learn: {title}", "websites"))
            if ds.group("rest"):
                keys += add_lesson_refs(bib, title, [ds.group("rest")])[0]
            continue
        sys.exit(f"make_books: a reference line of the lesson '{title}' does not parse (a paper is 'Authors (Year) Title. Venue.', a manual 'Unity Manual: ...'): {r}")
    keys += LESSON_EXTRA.get(title, [])
    return keys, chapters


# --- research/FEATURES.md ----------------------------------------------------------------------------------------

FEATURE_RE = re.compile(r"^(?P<authors>.+?)\s\((?P<year>\d{4})\)\.\s\*(?P<title>.+?)\*\.?\s*(?P<venue>.*)$")


def features(bib):
    """[(paper key or None, feature, status, source file, test)] from FEATURES.md's table."""
    rows = []
    for line in read(FEATURES).splitlines():
        if not line.startswith("| ") or line.startswith("| Paper") or line.startswith("|---"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split(" | ")]
        if len(cells) < 5:
            continue
        paper, feature, status, source, test = cells[:5]
        key = None
        pm = FEATURE_RE.match(paper)
        if pm:
            authors, year, title, venue = pm.group("authors"), pm.group("year"), pm.group("title"), pm.group("venue").strip()
            venue = re.sub(r"\s*\(`[^`]*`\)\s*", " ", venue).strip().rstrip(".")
            etype = "article" if re.search(r"journal|transactions|CACM|CAD|IJMMS|J\. Exp|Psychol", venue) else "inproceedings"
            if re.match(r"^(The Khronos Group|W3C)", authors):
                etype = "report"
            fields = {"author": bib_authors(authors), "year": year, "title": title}
            if venue:
                fields["journaltitle" if etype == "article" else "institution" if etype == "report" else "booktitle"] = venue
            key = bib.add(etype, key_for(authors, year, title), fields, "research/FEATURES.md", "papers")
        rows.append((key, paper if not key else None, feature, status, source, test))
    if len(rows) < 20:
        sys.exit("make_books: FEATURES.md parsed into too few rows")
    return rows


# --- licenses/README.md --------------------------------------------------------------------------------------------


def libraries(bib):
    """[(key, name, version, used for, licence)] from both tables of licenses/README.md."""
    rows = []
    section = ""
    in_table = False  # (only the tables whose header starts with "| Library": the ported / read tables are credits, not libraries)
    for line in read(LICENSES).splitlines():
        if line.startswith("## "):
            section = line[3:].strip()
        if line.startswith("| Library"):
            in_table = True
            continue
        if not line.startswith("|"):
            in_table = False
            continue
        if not in_table or line.startswith("|---"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split(" | ")]
        if len(cells) < 4:
            continue
        name_cell = cells[0]
        if re.match(r"^`[^`]+`", name_cell):  # a file named in code is its own title
            name_cell = re.sub(r"^`([^`]+)`", r"\1", name_cell)
        if section.startswith("Blender's prebuilt"):
            version, used, lic = cells[1], cells[2], cells[3]
        else:
            version, used, lic = "", cells[1], cells[2]
        urls = re.findall(r"\[([^\]]+)\]\(([^)]+)\)", name_cell)
        plain = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", name_cell)
        plain = re.sub(r"`[^`]*`", "", plain).strip()
        if not version:
            vm = re.match(r"^(.*?)\s+(v?\d[\w.]*|[0-9a-f]{7})\s*(\(.*)?$", plain)
            if vm and urls:
                plain, version = vm.group(1).strip(), vm.group(2)
        title = plain.split(" (")[0].strip() if "(" in plain and not plain.startswith("(") else plain
        fields = {"title": title, "year": "2026", "type": "software"}
        if urls:
            fields["url"] = urls[0][1]
        if version:
            fields["version"] = version
        fields["note"] = f"{used}. Licence: {re.sub(r'`[^`]*`', '', lic).replace('()', '').strip()}"
        key = "lib" + re.sub(r"[^A-Za-z0-9]", "", title)[:30]
        k = bib.add("software", key, fields, "licenses/README.md", "software")
        rows.append((k, title, version, used, lic))
    if len(rows) < 25:
        sys.exit("make_books: licenses/README.md parsed into too few rows")
    return rows


# --- the index tables ------------------------------------------------------------------------------------------------


def md_inline(s):
    """Markdown's `code`, *em* and [text](url) into LaTeX."""
    parts = re.split(r"(`[^`]*`)", s)
    out = []
    for p in parts:
        if p.startswith("`") and p.endswith("`") and len(p) > 1:
            out.append("\\code{" + tex_escape(p[1:-1]) + "}")
        else:
            p = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", p)
            p = re.sub(r"\*\*([^*]+)\*\*", lambda m: "\\textbf{" + tex_escape(m.group(1)) + "}", p)
            p = re.sub(r"\*([^*]+)\*", lambda m: "\\emph{" + tex_escape(m.group(1)) + "}", p)
            # tex_escape what is left outside the replacements above
            seg = re.split(r"(\\textbf\{[^}]*\}|\\emph\{[^}]*\})", p)
            out.append("".join(x if x.startswith("\\") else tex_escape(x) for x in seg))
    return "".join(out)


def cites(keys):
    keys = [k for k in dict.fromkeys(keys) if k]
    return "\\cite{" + ",".join(keys) + "}" if keys else "---"


def write_generated(outdir, bib, lesson_rows, feature_rows, lib_rows, doc_rows):
    os.makedirs(outdir, exist_ok=True)
    head = "% GENERATED by tools/make_books.py --bib. Do not edit: regenerate.\n"
    # A: code -> references
    lines = [head, "\\begin{longtable}{>{\\raggedright\\arraybackslash}p{0.30\\textwidth}>{\\raggedright\\arraybackslash}p{0.46\\textwidth}>{\\raggedright\\arraybackslash}p{0.16\\textwidth}}",
             "\\toprule \\textbf{Source} & \\textbf{What it implements} & \\textbf{References} \\\\ \\midrule \\endfirsthead",
             "\\toprule \\textbf{Source} & \\textbf{What it implements} & \\textbf{References} \\\\ \\midrule \\endhead",
             "\\bottomrule \\endfoot"]
    for key, paper, feature, status, source, test in feature_rows:
        what = md_inline(feature) + f" ({tex_escape(status)})"
        ref = cites([key]) if key else md_inline(paper)
        lines.append(f"{md_inline(source)} & {what} & {ref} \\\\")
    for k, title, version, used, lic in lib_rows:
        lines.append(f"{tex_escape(title)}{(' ' + tex_escape(version)) if version else ''} & {md_inline(used)} & {cites([k])} \\\\")
    lines.append("\\end{longtable}")
    with open(os.path.join(outdir, "code_refs.tex"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    # B: lessons (the UI's teaching surface) -> references and Blender's source
    lines = [head, "\\begin{longtable}{>{\\raggedright\\arraybackslash}p{0.20\\textwidth}>{\\raggedright\\arraybackslash}p{0.36\\textwidth}>{\\raggedright\\arraybackslash}p{0.36\\textwidth}}",
             "\\toprule \\textbf{Learn lesson} & \\textbf{Blender source it points to} & \\textbf{References} \\\\ \\midrule \\endfirsthead",
             "\\toprule \\textbf{Learn lesson} & \\textbf{Blender source it points to} & \\textbf{References} \\\\ \\midrule \\endhead",
             "\\bottomrule \\endfoot"]
    for title, keys, chapters, srcs in lesson_rows:
        # A path breaks after each "/" so a narrow column can hold Blender's long ones.
        src = "; ".join(tex_escape(re.sub(r"\s*\(.*\)\s*$", "", s)).replace("/", "/\\allowbreak{}") for s in srcs) or "---"
        ch = "; ".join(f"{tag} {tex_escape(c)}" for tag, c in chapters)
        refs = cites(keys + (["Blender2026"] if srcs else []))
        if ch:
            refs += " (" + ch + ")"
        lines.append(f"{tex_escape(title)} & {src} & {refs} \\\\")
    lines.append("\\end{longtable}")
    with open(os.path.join(outdir, "ui_refs.tex"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    # C: documents -> references
    lines = [head, "\\begin{longtable}{>{\\raggedright\\arraybackslash}p{0.42\\textwidth}>{\\raggedright\\arraybackslash}p{0.52\\textwidth}}",
             "\\toprule \\textbf{Document} & \\textbf{References it draws on} \\\\ \\midrule \\endfirsthead",
             "\\toprule \\textbf{Document} & \\textbf{References it draws on} \\\\ \\midrule \\endhead",
             "\\bottomrule \\endfoot"]
    for doc, keys in doc_rows:
        lines.append(f"\\code{{{tex_escape(doc)}}} & {cites(keys)} \\\\")
    lines.append("\\end{longtable}")
    with open(os.path.join(outdir, "docs_refs.tex"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def parse_bib_keys(text):
    return re.findall(r"^@\w+\{([^,\s]+),", text, re.M)


def entry_match_terms(bib_text):
    """For each key of a .bib: (surname of the first author or the title's first word, year) to find it in a document."""
    terms = {}
    for m in re.finditer(r"^@(\w+)\{([^,\s]+),(.*?)^\}", bib_text, re.M | re.S):
        key, body = m.group(2), m.group(3)
        f = dict((k.lower(), v) for k, v in re.findall(r"^\s*(\w+)\s*=\s*\{(.*)\},?\s*$", body, re.M))
        title = f.get("title", "")
        author = f.get("author", "")
        year = f.get("year", "")
        surname = ""
        if author:
            first = re.split(r" and ", author)[0]
            surname = first.split(",")[0].strip() if "," in first else (first.split()[-1] if first.split() else "")
        terms[key] = (surname, year, title)
    return terms


def docs_index(bib_path, extra_path):
    """Every document of docs/ (and the README) with the references it names: a surname + year, or the title."""
    terms = {}
    for p in (bib_path, extra_path):
        terms.update(entry_match_terms(read(p)))
    docs = [os.path.join(ROOT, "README.md"), os.path.join(ROOT, "docs", "ARCHITECTURE.md"), os.path.join(ROOT, "docs", "MANUAL.md"),
            os.path.join(ROOT, "docs", "LIVELINK.md"), os.path.join(ROOT, "docs", "PERFORMANCE.md"), os.path.join(ROOT, "docs", "USABILITY.md"),
            os.path.join(ROOT, "docs", "AI_DISCLOSURE.md"), os.path.join(ROOT, "research", "FEATURES.md"), os.path.join(ROOT, "licenses", "README.md"),
            os.path.join(ROOT, "CREDITS.md")]
    docs += sorted(os.path.join(ROOT, "docs", "decisions", f) for f in os.listdir(os.path.join(ROOT, "docs", "decisions")) if f.endswith(".md") and f != "README.md")
    docs += sorted(os.path.join(ROOT, "docs", "hci", f) for f in os.listdir(os.path.join(ROOT, "docs", "hci")) if f.endswith(".md"))
    rows = []
    for d in docs:
        if not os.path.exists(d):
            continue
        text = read(d)
        low = text.lower()
        keys = []
        for key, (surname, year, title) in terms.items():
            hit = False
            if surname and year and len(surname) > 3 and surname.lower() in low and year in text:
                # the surname and the year near each other (within a line)
                for line in text.splitlines():
                    if surname.lower() in line.lower() and year in line:
                        hit = True
                        break
            if not hit and title and len(title) > 12 and norm_title(title) in norm_title(text):
                hit = True
            # A library or a program's manual: its name as a whole word (Embree, oneTBB, ufbx, "Unity User Manual").
            if not hit and not surname and 4 <= len(title) <= 40 and re.search(r"(?<![A-Za-z0-9])" + re.escape(title.split(" (")[0]) + r"(?![A-Za-z0-9])", text, re.I):
                hit = True
            if hit:
                keys.append(key)
        if keys:
            rows.append((os.path.relpath(d, ROOT).replace(os.sep, "/"), sorted(keys)))
    return rows


def generate(outdir):
    """Everything --bib writes, into outdir (refs.bib and generated/)."""
    bib = Bib()
    lesson_rows = []
    for title, refs, srcs in lessons():
        keys, chapters = add_lesson_refs(bib, title, refs)
        lesson_rows.append((title, keys, chapters, srcs))
    feature_rows = features(bib)
    lib_rows = libraries(bib)
    os.makedirs(outdir, exist_ok=True)
    bib_path = os.path.join(outdir, "refs.bib")
    bib.write(bib_path, "")
    doc_rows = docs_index(bib_path, EXTRA_BIB)
    write_generated(os.path.join(outdir, "generated"), bib, lesson_rows, feature_rows, lib_rows, doc_rows)
    return bib


def all_cited_keys():
    keys = set()
    for dirpath, _, files in os.walk(BOOK):
        if os.path.basename(dirpath) == "build":
            continue
        for f in files:
            if f.endswith(".tex"):
                for m in re.finditer(r"\\(?:cite|textcite|parencite|nocite|footcite)\*?(?:\[[^\]]*\])*\{([^}]*)\}", read(os.path.join(dirpath, f))):
                    for k in m.group(1).split(","):
                        k = k.strip()
                        if k and k != "*":
                            keys.add(k)
    return keys


def check(drift=True):
    """drift: the committed refs.bib and generated/*.tex equal a fresh generation (the release's books job and a local
    run); without it only that the committed files agree with each other (build.yml: a new citation elsewhere in the
    tree is not a red build but a --bib to run when the books are next rebuilt, ADR 0104)."""
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        if drift:
            generate(tmp)
        for rel in (["refs.bib", "generated/code_refs.tex", "generated/ui_refs.tex", "generated/docs_refs.tex"] if drift else []):
            a, b = os.path.join(BOOK, rel), os.path.join(tmp, rel)
            if not os.path.exists(a):
                print(f"missing: docs/book/{rel} (run --bib)")
                ok = False
                continue
            if read(a) != read(b):
                ok = False
                print(f"drift: docs/book/{rel} differs from what --bib would write:")
                for l in list(difflib.unified_diff(read(a).splitlines(), read(b).splitlines(), "committed", "generated", lineterm=""))[:40]:
                    print("  " + l)
    keys = set(parse_bib_keys(read(os.path.join(BOOK, "refs.bib")))) | set(parse_bib_keys(read(EXTRA_BIB)))
    cited = all_cited_keys()
    for k in sorted(cited - keys):
        print(f"cited but in no .bib: {k}")
        ok = False
    for k in sorted(keys - cited):
        print(f"in the .bib but never cited: {k}")
        ok = False
    print(f"{len(keys)} references, {len(cited)} cited; {'ok' if ok else 'FAILED'}")
    return 0 if ok else 1


# --- the figures ------------------------------------------------------------------------------------------------------

SHOTS = [
    ("editor-default", "Default", ["select Cube"]),
    ("layout-modeling", "Modeling & Topology", ["select Cube", "edit on"]),
    ("layout-uv", "UV Unwrapping", ["select Cube", "edit on", "selectall", "uv unwrap"]),
    ("layout-texturing", "Texturing & Materials", ["select Cube"]),
    ("layout-lighting", "Lighting & Rendering", ["select Cube"]),
    ("layout-animation", "Animation", ["select Cube", "anim new"]),
    ("layout-learning", "Learning", ["lesson 1"]),
    ("layout-drawing", "Drawing", []),
]


def screenshots(exe):
    """The figures of the user guide, from a fresh scratch project's starter scene (never the user's Assets)."""
    figs = os.path.join(BOOK, "figures")
    os.makedirs(figs, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ)
        env["BLENDITY_PROJECT"] = os.path.join(tmp, "project")
        env["BLENDITY_TRASH"] = os.path.join(tmp, "trash")
        env["BLENDITY_PREFS_DIR"] = os.path.join(tmp, "prefs")
        for name, layout, cmds in SHOTS:
            out = os.path.join(figs, name + ".png")
            args = [exe, "--headless-screenshot", out, "--size", "1600x900", "--cmd", f"layout {layout}"]
            for c in cmds:
                args += ["--cmd", c]
            args += ["--frames", "12"]
            print(" ".join(args))
            r = subprocess.run(args, env=env, cwd=tmp, capture_output=True, text=True, timeout=600)
            if r.returncode != 0 or not os.path.exists(out):
                sys.exit(f"screenshot {name} failed ({r.returncode}): {r.stdout[-800:]} {r.stderr[-800:]}")
    print(f"{len(SHOTS)} figures in docs/book/figures")


# --- the PDFs ---------------------------------------------------------------------------------------------------------


def to_wsl(path):
    """A Windows path as WSL sees it; a subst drive is resolved to the folder it stands for (WSL can't see it)."""
    p = os.path.abspath(path)
    if os.name == "nt":
        try:
            for line in subprocess.run(["subst"], capture_output=True, text=True).stdout.splitlines():
                m = re.match(r"^([A-Za-z]):\\: => (.+)$", line.strip())
                if m and p.upper().startswith(m.group(1).upper() + ":"):
                    p = m.group(2).rstrip("\\") + p[2:]
        except OSError:
            pass
    p = p.replace("\\", "/")
    m = re.match(r"^([A-Za-z]):/(.*)$", p)
    return f"/mnt/{m.group(1).lower()}/{m.group(2)}" if m else p


def pdf(wsl):
    for book in BOOKS:
        if wsl:
            cmd = ["wsl", "-e", "bash", "-lc", f"cd '{to_wsl(BOOK).replace(chr(39), chr(39) + chr(92) + chr(39) + chr(39))}' && latexmk -lualatex -interaction=nonstopmode -halt-on-error -outdir=build {book}.tex"]
        else:
            cmd = ["latexmk", "-lualatex", "-interaction=nonstopmode", "-halt-on-error", "-outdir=build", f"{book}.tex"]
        print(" ".join(cmd))
        r = subprocess.run(cmd, cwd=BOOK)
        if r.returncode != 0:
            sys.exit(f"latexmk failed for {book} (see docs/book/build/{book}.log)")
        src = os.path.join(BOOK, "build", book + ".pdf")
        dst = os.path.join(ROOT, "docs", PDF_NAMES[book])
        shutil.copyfile(src, dst)
        print(f"wrote docs/{PDF_NAMES[book]} ({os.path.getsize(dst) // 1024} KB)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bib", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--check-cited", action="store_true", help="the committed files agree with each other (no drift check)")
    ap.add_argument("--screenshots", metavar="EXE")
    ap.add_argument("--pdf", action="store_true")
    ap.add_argument("--wsl", action="store_true")
    a = ap.parse_args()
    if not (a.bib or a.check or a.check_cited or a.screenshots or a.pdf):
        ap.print_help()
        return 2
    if a.bib:
        bib = generate(BOOK)
        print(f"docs/book/refs.bib: {len(bib.entries)} generated entries; generated/*.tex written")
    if a.check or a.check_cited:
        rc = check(drift=a.check)
        if rc:
            return rc
    if a.screenshots:
        screenshots(a.screenshots)
    if a.pdf:
        pdf(a.wsl)
    return 0


if __name__ == "__main__":
    sys.exit(main())
