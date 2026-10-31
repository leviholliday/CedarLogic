#!/usr/bin/env python3
"""Check winget manifests the way `winget validate` would, where winget isn't (a Mac).

    python3 packaging/winget/validate.py packaging/winget/manifests/l/LeviHolliday/CedarLogic/0.4.0
        --placeholders-ok    zero hashes are only a warning (the committed 0.4.0 before the release)
        --schemas DIR        where the JSON schemas are cached (default ~/.cache/winget-schemas)

Needs PyYAML and jsonschema (python3 -m venv v && v/bin/pip install pyyaml jsonschema).
Each file is checked against Microsoft's JSON schema for its ManifestType and
ManifestVersion (downloaded once from microsoft/winget-cli), then the checks
`winget validate` adds on top: one version, one installer and one default
locale file, all with the same identifier and version, named and placed as
winget-pkgs wants (manifests/<first letter>/<Publisher>/<Name>/<version>/),
unique architectures, https URLs, and real SHA-256s.
"""
import json
import os
import re
import sys
import urllib.request

try:
    import yaml
    import jsonschema
except ImportError:
    sys.exit("validate: needs PyYAML and jsonschema: python3 -m venv v && v/bin/pip install pyyaml jsonschema, then v/bin/python3 " + " ".join(sys.argv))

SCHEMA_URL = "https://raw.githubusercontent.com/microsoft/winget-cli/master/schemas/JSON/manifests/v{v}/manifest.{t}.{v}.json"
TYPES = {"version": "version", "installer": "installer", "defaultLocale": "defaultLocale", "locale": "locale"}


class Loader(yaml.SafeLoader):
    """YAML as winget reads it: a date like 2026-11-01 stays a string."""


Loader.yaml_implicit_resolvers = {
    k: [(tag, rx) for tag, rx in v if tag != "tag:yaml.org,2002:timestamp"]
    for k, v in yaml.SafeLoader.yaml_implicit_resolvers.items()
}


def schema_for(mtype, mversion, cache):
    os.makedirs(cache, exist_ok=True)
    path = os.path.join(cache, "manifest.%s.%s.json" % (mtype, mversion))
    if not os.path.exists(path):
        url = SCHEMA_URL.format(v=mversion, t=mtype)
        with urllib.request.urlopen(url, timeout=30) as r:
            data = r.read()
        json.loads(data)  # it is JSON
        with open(path + ".tmp", "wb") as f:
            f.write(data)
        os.replace(path + ".tmp", path)
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def main():
    args = sys.argv[1:]
    placeholders_ok = "--placeholders-ok" in args
    args = [a for a in args if a != "--placeholders-ok"]
    cache = os.path.expanduser("~/.cache/winget-schemas")
    if "--schemas" in args:
        i = args.index("--schemas")
        cache = args[i + 1]
        del args[i:i + 2]
    if len(args) != 1 or not os.path.isdir(args[0]):
        sys.exit(__doc__)
    folder = os.path.abspath(args[0])
    errors, warnings = [], []
    docs = {}
    files = sorted(f for f in os.listdir(folder) if f.endswith(".yaml"))
    if not files:
        sys.exit("validate: no .yaml files in " + folder)
    for name in files:
        path = os.path.join(folder, name)
        raw = open(path, "rb").read()
        if raw.startswith(b"\xef\xbb\xbf"):
            warnings.append("%s: starts with a BOM" % name)
        if b"\r" in raw:
            warnings.append("%s: has CR line endings" % name)
        if b"\t" in raw:
            errors.append("%s: has a tab" % name)
        doc = yaml.load(raw.decode("utf-8"), Loader=Loader)
        if not isinstance(doc, dict):
            errors.append("%s: not a YAML mapping" % name)
            continue
        mtype, mversion = doc.get("ManifestType"), str(doc.get("ManifestVersion", ""))
        if mtype not in TYPES:
            errors.append("%s: ManifestType %r isn't version/installer/defaultLocale/locale" % (name, mtype))
            continue
        if not re.fullmatch(r"1\.(\d+)\.0", mversion) or int(mversion.split(".")[1]) < 6:
            errors.append("%s: ManifestVersion %s (want 1.6.0 or newer)" % (name, mversion))
            continue
        schema = schema_for(TYPES[mtype], mversion, cache)
        validator = jsonschema.Draft7Validator(schema)
        for e in sorted(validator.iter_errors(doc), key=lambda e: list(e.path)):
            errors.append("%s: %s: %s" % (name, "/".join(str(p) for p in e.path) or "(top)", e.message))
        docs.setdefault(mtype, []).append((name, doc))

    for mtype in ("version", "installer", "defaultLocale"):
        if len(docs.get(mtype, [])) != 1:
            errors.append("want exactly one %s manifest, found %d" % (mtype, len(docs.get(mtype, []))))
    every = [(n, d) for v in docs.values() for n, d in v]
    ids = {d.get("PackageIdentifier") for _, d in every}
    versions = {str(d.get("PackageVersion")) for _, d in every}
    mversions = {str(d.get("ManifestVersion")) for _, d in every}
    if len(ids) != 1:
        errors.append("PackageIdentifier differs between files: %s" % sorted(map(str, ids)))
    if len(versions) != 1:
        errors.append("PackageVersion differs between files: %s" % sorted(versions))
    if len(mversions) != 1:
        errors.append("ManifestVersion differs between files: %s" % sorted(mversions))
    if not errors:
        pid, pver = ids.pop(), versions.pop()
        want = {"version": pid + ".yaml", "installer": pid + ".installer.yaml"}
        for mtype, name in want.items():
            if docs[mtype][0][0] != name:
                errors.append("the %s manifest should be named %s" % (mtype, name))
        dl = docs["defaultLocale"][0][1]
        if docs["version"][0][1].get("DefaultLocale") != dl.get("PackageLocale"):
            errors.append("DefaultLocale isn't the defaultLocale manifest's PackageLocale")
        if docs["defaultLocale"][0][0] != "%s.locale.%s.yaml" % (pid, dl.get("PackageLocale")):
            errors.append("the default locale manifest should be named %s.locale.%s.yaml" % (pid, dl.get("PackageLocale")))
        parts = pid.split(".")
        tail = os.path.join(pid[0].lower(), *parts, pver)
        if not folder.endswith(os.sep + tail):
            errors.append("the folder should end in manifests/%s" % tail.replace(os.sep, "/"))
        inst = docs["installer"][0][1]
        seen = set()
        for i, it in enumerate(inst.get("Installers") or []):
            key = (it.get("Architecture"), it.get("Scope", inst.get("Scope")), it.get("InstallerLocale"))
            if key in seen:
                errors.append("Installers[%d]: a second installer for %s" % (i, key))
            seen.add(key)
            url = it.get("InstallerUrl") or inst.get("InstallerUrl") or ""
            if not url.startswith("https://"):
                errors.append("Installers[%d]: InstallerUrl isn't https: %s" % (i, url))
            sha = str(it.get("InstallerSha256") or "")
            if re.fullmatch(r"0{64}", sha):
                (warnings if placeholders_ok else errors).append("Installers[%d] (%s): InstallerSha256 is a placeholder (packaging/winget/bump.sh fills it in)" % (i, it.get("Architecture")))
        for entry in inst.get("AppsAndFeaturesEntries") or []:
            if str(entry.get("DisplayVersion", pver)) != pver:
                warnings.append("AppsAndFeaturesEntries DisplayVersion %s isn't PackageVersion %s (fine only if the installer really says that)" % (entry.get("DisplayVersion"), pver))

    for w in warnings:
        print("warning: " + w)
    for e in errors:
        print("error: " + e)
    if errors:
        print("validate: %d error(s) in %s" % (len(errors), folder))
        sys.exit(1)
    print("validate: %s: %d files, valid against the %s schemas" % (folder, len(files), mversions.pop()))


if __name__ == "__main__":
    main()
