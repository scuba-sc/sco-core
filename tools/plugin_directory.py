"""Validate docs/plugin-directory.json and render the listing tables of docs/plugin-directory.md.

  python3 tools/plugin_directory.py --check   # validate; fail if the rendered tables drifted
  python3 tools/plugin_directory.py --write   # validate; rewrite the tables between the markers

The index is the source of truth; the markdown between "<!-- plugin-directory:start -->" and
"<!-- plugin-directory:end -->" is generated. Validation: the documented schema (top-level keys,
the in-file "fields" description naming exactly the entry keys), https repository URLs without
credentials, query or fragment, plugin ids unique across the whole directory and not reserved by
the host, table-safe one-line text (no HTML, Markdown links or code spans), a stated SPDX-shaped
licence, plugin kinds/languages from a fixed set, SDK and game-pack versions no newer than this
checkout's, sco-plugin-check results with a real UTC date that is not in the future, the offline
confirmation, and an in-game field that can never claim verification. Community listings must
record a passing full-mode checker run.
"""
import argparse
import datetime
import json
import re
import sys
from pathlib import Path, PurePosixPath
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parent.parent
INDEX = PurePosixPath("docs/plugin-directory.json")
PAGE = PurePosixPath("docs/plugin-directory.md")
VERSION_FILE = PurePosixPath("sdk/VERSION")
GAME_PACK_HEADER = PurePosixPath("include/sc_game_pack.h")
START = "<!-- plugin-directory:start -->"
END = "<!-- plugin-directory:end -->"

TOP_KEYS = {"schema_version", "about", "fields", "first_party", "community"}
SECTIONS = (("first_party", "First-party"), ("community", "Community"))
REQUIRED = ("name", "ids", "repository", "author", "licence", "languages", "sdk_version",
            "game_pack_version", "checker", "offline", "in_game", "description", "listed")
OPTIONAL = ("note",)
ENTRY_KEYS = set(REQUIRED) | set(OPTIONAL)
CHECKER_KEYS = {"full", "short", "date"}
RESULTS = {"pass", "fail", "not-recorded"}
LANGUAGES = ("c", "cpp", "csharp", "lua", "data")
LANGUAGE_LABELS = {"c": "C", "cpp": "C++", "csharp": "C#", "lua": "Lua", "data": "data pack"}
IN_GAME = {"not-run": "not run in game", "not-verified": "not verified in game"}
NO_LICENCE = {"NONE", "NOASSERTION", "UNLICENSED"}
RESERVED_IDS = ("game", "host", "menu", "sco")
UNSAFE_TEXT = set("|<>`[]")
PLUGIN_ID = re.compile(r"[a-z0-9_]{1,31}")
SEMVER = re.compile(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)")
DATE = re.compile(r"\d{4}-\d{2}-\d{2}")
LICENCE_TOKEN = re.compile(r"(?:LicenseRef-|DocumentRef-[A-Za-z0-9.-]+:LicenseRef-)?[A-Za-z0-9][A-Za-z0-9.-]*\+?")
LICENCE_OPERATORS = {"AND", "OR", "WITH"}
GAME_PACK_DEFINE = re.compile(r'#define\s+SCO_GAME_PACK_VERSION\s+"([^"]+)"')
MAX_DESCRIPTION = 200


class DirectoryError(Exception):
    pass


def version_tuple(text):
    return tuple(int(part) for part in text.split("."))


def current_versions(root):
    """The SDK and game-pack versions of this checkout: listings may not claim newer ones."""
    sdk = (root / VERSION_FILE).read_text(encoding="utf-8").strip()
    found = GAME_PACK_DEFINE.search((root / GAME_PACK_HEADER).read_text(encoding="utf-8"))
    if not SEMVER.fullmatch(sdk) or not found or not SEMVER.fullmatch(found.group(1)):
        raise DirectoryError("cannot read the SDK or game-pack version of this checkout")
    return sdk, found.group(1)


def one_line(value):
    return isinstance(value, str) and value.strip() == value and bool(value) \
        and "|" not in value and "\n" not in value and "\r" not in value


def table_text(value):
    """One line that renders as plain text in a table cell and on the docs site: no HTML tag, no
    Markdown link or code span, and so never the generator's own markers."""
    return one_line(value) and not UNSAFE_TEXT.intersection(value)


def check_date(value, today, where, key):
    if not isinstance(value, str) or not DATE.fullmatch(value):
        return ["%s: %s %r is not a YYYY-MM-DD date" % (where, key, value)]
    try:
        day = datetime.date.fromisoformat(value)
    except ValueError:
        return ["%s: %s %r is not a real date" % (where, key, value)]
    if day > today:
        return ["%s: %s %s is in the future (today is %s UTC)" % (where, key, value, today.isoformat())]
    return []


def check_url(value, where):
    if not isinstance(value, str) or any(c.isspace() or c in "|()<>[]" for c in value):
        return ["%s: repository must be one https URL" % where]
    url = urlsplit(value)
    if url.scheme != "https" or not url.hostname:
        return ["%s: repository %r is not an https URL" % (where, value)]
    if url.username is not None or url.password is not None or "@" in url.netloc:
        return ["%s: repository URL must not carry credentials" % where]
    if url.query or url.fragment or value.endswith(("?", "#")):
        return ["%s: repository URL must not carry a query or fragment" % where]
    if url.path.strip("/") == "":
        return ["%s: repository URL %r names no repository" % (where, value)]
    return []


def spdx_expression(tokens):
    """True when tokens form expression := term ((AND | OR) term)*; term := '(' expression ')' | id [WITH id]."""
    position = 0

    def identifier():
        nonlocal position
        token = tokens[position] if position < len(tokens) else ""
        if token not in LICENCE_OPERATORS and LICENCE_TOKEN.fullmatch(token):
            position += 1
            return True
        return False

    def term():
        nonlocal position
        if position < len(tokens) and tokens[position] == "(":
            position += 1
            if not expression() or position >= len(tokens) or tokens[position] != ")":
                return False
            position += 1
            return True
        if not identifier():
            return False
        if position < len(tokens) and tokens[position] == "WITH":
            position += 1
            return identifier()
        return True

    def expression():
        nonlocal position
        if not term():
            return False
        while position < len(tokens) and tokens[position] in ("AND", "OR"):
            position += 1
            if not term():
                return False
        return True

    return expression() and position == len(tokens)


def check_licence(value, where):
    if not isinstance(value, str) or not value.strip():
        return ["%s: licence missing: state the SPDX licence the author gives" % where]
    tokens = value.replace("(", " ( ").replace(")", " ) ").split()
    if not spdx_expression(tokens):
        return ["%s: licence %r is not an SPDX expression" % (where, value)]
    if any(token.upper() in NO_LICENCE for token in tokens):
        return ["%s: licence %r states no licence; a listing needs one" % (where, value)]
    return []


def check_checker(checker, section, where, today):
    if not isinstance(checker, dict) or set(checker) != CHECKER_KEYS:
        return ["%s: checker must be an object with exactly %s" % (where, sorted(CHECKER_KEYS))]
    errors = []
    for mode in ("full", "short"):
        if not isinstance(checker[mode], str) or checker[mode] not in RESULTS:
            errors.append("%s: checker %s %r not in %s" % (where, mode, checker[mode], sorted(RESULTS)))
    if errors:
        return errors
    recorded = checker["full"] != "not-recorded" or checker["short"] != "not-recorded"
    if recorded:
        errors.extend(check_date(checker["date"], today, where, "checker date"))
    elif checker["date"] is not None:
        errors.append(where + ": checker date must be null when no result is recorded")
    if section == "community" and checker["full"] != "pass":
        errors.append(where + ": a community listing needs a passing full-mode sco-plugin-check run")
    return errors


def validate_entry(entry, section, index, today, versions):
    where = "%s[%d]" % (section, index)
    if not isinstance(entry, dict):
        return [where + ": not an object"]
    if one_line(entry.get("name")):
        where = "%s (%s)" % (where, entry["name"])
    errors = []
    missing = [key for key in REQUIRED if key not in entry]
    extra = set(entry) - ENTRY_KEYS
    if missing:
        errors.append("%s: missing keys %s" % (where, missing))
    if extra:
        errors.append("%s: unknown keys %s" % (where, sorted(extra)))
    if missing:
        return errors
    for key in ("name", "author"):
        if not table_text(entry[key]):
            errors.append("%s: %s must be one non-empty line without any of | < > ` [ ]" % (where, key))
    description = entry["description"]
    if not table_text(description) or len(description) > MAX_DESCRIPTION:
        errors.append("%s: description must be one line without any of | < > ` [ ] and at most %d characters"
                      % (where, MAX_DESCRIPTION))
    if "note" in entry and not table_text(entry["note"]):
        errors.append(where + ": note must be one non-empty line without any of | < > ` [ ]")
    ids = entry["ids"]
    if not isinstance(ids, list) or not ids:
        errors.append(where + ": ids must be a non-empty list of plugin ids")
    else:
        for plugin_id in ids:
            if not isinstance(plugin_id, str) or not PLUGIN_ID.fullmatch(plugin_id):
                errors.append("%s: id %r is not [a-z0-9_], 1-31 characters" % (where, plugin_id))
            elif plugin_id in RESERVED_IDS:
                errors.append("%s: id %s is reserved by the host (%s)" % (where, plugin_id, ", ".join(RESERVED_IDS)))
    errors.extend(check_url(entry["repository"], where))
    errors.extend(check_licence(entry["licence"], where))
    languages = entry["languages"]
    if (not isinstance(languages, list) or not languages or len(set(map(str, languages))) != len(languages)
            or not all(language in LANGUAGES for language in languages)):
        errors.append("%s: languages must be a non-empty list of distinct %s" % (where, list(LANGUAGES)))
    sdk, pack = versions
    if not isinstance(entry["sdk_version"], str) or not SEMVER.fullmatch(entry["sdk_version"]):
        errors.append("%s: sdk_version %r is not x.y.z" % (where, entry["sdk_version"]))
    elif version_tuple(entry["sdk_version"]) > version_tuple(sdk):
        errors.append("%s: sdk_version %s is newer than this SDK (%s)" % (where, entry["sdk_version"], sdk))
    game_pack = entry["game_pack_version"]
    if game_pack is not None:
        if not isinstance(game_pack, str) or not SEMVER.fullmatch(game_pack):
            errors.append("%s: game_pack_version %r is not x.y.z or null" % (where, game_pack))
        elif version_tuple(game_pack) > version_tuple(pack):
            errors.append("%s: game_pack_version %s is newer than this game pack (%s)" % (where, game_pack, pack))
    errors.extend(check_checker(entry["checker"], section, where, today))
    if entry["offline"] is not True:
        errors.append(where + ": offline must be true: every listing follows the offline-only plugin rules")
    if not isinstance(entry["in_game"], str) or entry["in_game"] not in IN_GAME:
        errors.append("%s: in_game %r not in %s; the directory never records in-game verification"
                      % (where, entry["in_game"], sorted(IN_GAME)))
    errors.extend(check_date(entry["listed"], today, where, "listed"))
    return errors


def validate(directory, root, today=None):
    today = today or datetime.datetime.now(datetime.timezone.utc).date()
    if not isinstance(directory, dict) or directory.get("schema_version") != 1:
        return ["schema_version must be 1"]
    errors = []
    if set(directory) != TOP_KEYS:
        errors.append("top-level keys must be exactly %s" % sorted(TOP_KEYS))
    if not one_line(directory.get("about")):
        errors.append("about must be one non-empty line")
    fields = directory.get("fields")
    if not isinstance(fields, dict) or set(fields) != ENTRY_KEYS \
            or not all(isinstance(text, str) and text for text in fields.values()):
        errors.append("fields must describe exactly the entry keys %s" % sorted(ENTRY_KEYS))
    versions = current_versions(root)
    seen_ids, seen_names = {}, set()
    for section, _ in SECTIONS:
        entries = directory.get(section)
        if not isinstance(entries, list):
            errors.append(section + " must be a list (empty when nothing is listed)")
            continue
        for index, entry in enumerate(entries):
            entry_errors = validate_entry(entry, section, index, today, versions)
            errors.extend(entry_errors)
            if not isinstance(entry, dict):
                continue
            where = "%s[%d]" % (section, index)
            name = entry.get("name")
            if isinstance(name, str):
                if name.lower() in seen_names:
                    errors.append("%s: name %r is listed twice" % (where, name))
                seen_names.add(name.lower())
            ids = entry.get("ids")
            for plugin_id in ids if isinstance(ids, list) else []:
                if not isinstance(plugin_id, str):
                    continue
                if plugin_id in seen_ids:
                    errors.append("%s: id %s is already listed by %s" % (where, plugin_id, seen_ids[plugin_id]))
                else:
                    seen_ids[plugin_id] = where
    return errors


def checker_cell(checker):
    if checker["full"] == "not-recorded" and checker["short"] == "not-recorded":
        return "not recorded"
    return "full %s, short %s (%s)" % (checker["full"], checker["short"], checker["date"])


def render_entry(entry):
    cells = [
        "[%s](%s)" % (entry["name"], entry["repository"]),
        entry["description"] + (" " + entry["note"] if entry.get("note") else ""),
        ", ".join("`%s`" % plugin_id for plugin_id in entry["ids"]),
        entry["author"],
        "`%s`" % entry["licence"],
        ", ".join(LANGUAGE_LABELS[language] for language in entry["languages"]),
        entry["sdk_version"],
        entry["game_pack_version"] or "none",
        checker_cell(entry["checker"]),
        "confirmed" if entry["offline"] else "no",
        IN_GAME[entry["in_game"]],
        entry["listed"],
    ]
    return "| " + " | ".join(cells) + " |"


HEADER = ("| Plugin | Description | Plugin ids | Author | Licence | Languages | SDK tested | Game pack "
          "| sco-plugin-check | Offline | In game | Listed |")
SEPARATOR = "|---|---|---|---|---|---|---|---|---|---|---|---|"
EMPTY = {"first_party": "No first-party plugins are listed.",
         "community": "No community plugins are listed yet. [Request a listing](#request-a-listing)."}


def render(directory):
    lines = []
    for section, title in SECTIONS:
        lines += ["", "### " + title, ""]
        entries = directory[section]
        if entries:
            lines += [HEADER, SEPARATOR] + [render_entry(entry) for entry in entries]
        else:
            lines.append(EMPTY[section])
    return "\n".join(lines[1:]) + "\n"


def splice(text, table):
    if text.count(START) != 1 or text.count(END) != 1 or text.index(START) > text.index(END):
        raise DirectoryError("%s needs exactly one %s ... %s block" % (PAGE, START, END))
    head, rest = text.split(START, 1)
    _, tail = rest.split(END, 1)
    return head + START + "\n" + table + END + tail


def load(root):
    try:
        return json.loads((root / INDEX).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise DirectoryError("%s unreadable: %s" % (INDEX, error))


def run(root, write, today=None):
    directory = load(root)
    errors = validate(directory, root, today)
    if errors:
        raise DirectoryError("\n".join(["%s invalid:" % INDEX] + ["  " + e for e in errors]))
    path = root / PAGE
    text = path.read_text(encoding="utf-8")
    expected = splice(text, render(directory))
    counts = tuple(len(directory[section]) for section, _ in SECTIONS)
    if expected == text:
        return "%s: tables match %s (%d first-party, %d community)" % ((PAGE, INDEX) + counts)
    if not write:
        raise DirectoryError("%s drifted from %s; run python3 tools/plugin_directory.py --write" % (PAGE, INDEX))
    path.write_text(expected, encoding="utf-8")
    return "%s: tables rewritten from %s" % (PAGE, INDEX)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Validate docs/plugin-directory.json and render docs/plugin-directory.md.")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="validate and fail on drift")
    mode.add_argument("--write", action="store_true", help="validate and rewrite the tables")
    parser.add_argument("--root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        print(run(args.root.resolve(), args.write))
    except DirectoryError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
