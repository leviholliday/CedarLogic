# The PowerShell one-liner (Windows)

```powershell
irm https://cedarlogic.netlify.app/install.ps1 | iex
```

`install.ps1` finds the newest **full** release of
`leviholliday/CedarLogic-Releases` that has a Windows installer
(`CedarLogic-<version>-Windows-Setup-x64.exe` / `-ARM64.exe`), skipping betas
(pre-releases) and Mac-only releases. It picks ARM64 on Windows on ARM (it
reads the system's architecture from the registry, since x64 PowerShell runs
there too) and x64 otherwise, falling back to x64 on ARM if a release has no
ARM64 installer. It downloads the installer to a temp folder and checks it:

1. its size and SHA-256 against what GitHub's API records for the file
   (`digest`), and
2. when the release has `SHA256SUMS` + `SHA256SUMS.sig`, the Ed25519
   signature against the update key (the `PUBHEX` in `scripts/sign-preview.sh`,
   the key the apps' updaters trust), the header line, `tag <release tag>`,
   and the `file <name> <size> <sha256>` line for this installer.

If either check fails, nothing is installed. If the release has neither, the
script refuses too. (GitHub records a digest for every asset today, so this
shouldn't happen.) Then it runs the installer silently
(`/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-`, the switches CI's installer
test uses), checks `CedarLogic.exe` is where setup recorded it
(`HKCU\Software\CedarLogic\Native\InstallDir`), and starts it.

No administrator: the installer is per-user (`PrivilegesRequired=lowest`).
If PowerShell is elevated, it says to use a normal one, because an elevated
PowerShell may belong to a different account. If CedarLogic is open, it stops
before installing and asks the user to quit (setup's `AppMutex` would stop
anyway, and this way unsaved work isn't at risk). It never uses `exit`, which
would close the user's window under `iex`. Errors print in red, starting
"CedarLogic wasn't installed:".

The signature check is RFC 8032 Ed25519 written out on .NET's `BigInteger`
(Windows has no Ed25519 built in). It runs on Windows PowerShell 5.1 and
PowerShell 7 and takes a second or two.

## Hosting it

The site should serve this file, byte for byte, at
**`https://cedarlogic.netlify.app/install.ps1`** with
`Content-Type: text/plain; charset=utf-8`. Copy it into the site repo at
release time (a copy, not a redirect, so the URL doesn't depend on GitHub
raw caching). The alternative stable URL, once `pkg/installers` is merged to
`main` here, is
`https://raw.githubusercontent.com/leviholliday/CedarLogic/main/packaging/windows/install.ps1`.

The file is plain ASCII with LF line endings. Windows PowerShell 5.1 reads
`irm` output fine either way.

## Options

Run it as a script block to pass them:

```powershell
& ([scriptblock]::Create((irm https://cedarlogic.netlify.app/install.ps1))) -DryRun
```

| Option | What |
|---|---|
| `-DryRun` | find, download and check, then stop (nothing installed) |
| `-SelfTest` | check the Ed25519 code against RFC 8032's test vectors (and that changed ones fail), then stop |
| `-Version 0.4` | that release instead of the newest |
| `-Arch x64` / `-Arch ARM64` | instead of this PC's |
| `-NoLaunch` | don't start CedarLogic afterwards |
| `-Tag T -Repo O/R` | a specific release tag, pre-releases included, file names with or without a version: for testing |

## Testing it (needs a Windows PC; not run here: there's no `pwsh` on the Mac)

From a checkout or a downloaded copy, in a normal (not admin) PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -SelfTest
#   Self-test passed: Ed25519 verifies RFC 8032's signatures and refuses changed ones.
# The signed rolling test build, today (verifies its real SHA256SUMS.sig):
powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -Repo leviholliday/CedarLogic -Tag windows-native-testing -DryRun
#   Checked: GitHub's SHA-256 and the signed list (SHA-256 ...).
# A real install from it (CedarLogic closed):
powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -Repo leviholliday/CedarLogic -Tag windows-native-testing
# After v0.4 is out, exactly what people will run:
irm https://cedarlogic.netlify.app/install.ps1 | iex
```

Run it in both `powershell` (5.1) and `pwsh` (7) if you can. What was checked
on the Mac instead: the Ed25519 steps, mirrored line for line in Python,
pass RFC 8032's vectors and verify the real `windows-native-testing`
`SHA256SUMS.sig` (and refuse a changed manifest); the brackets and strings
balance. The PowerShell itself has not been run.
