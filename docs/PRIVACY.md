# Privacy: crash reports and usage stats

CedarLogic can send two kinds of anonymous information to its developer:
**crash reports** and **usage stats**. Both are **off** until you turn them on,
and you are asked about them once:

- On a new install, the welcome has a page called "A small favour" with two
  buttons, **Yes, send anonymous reports** and **No thanks**. "Choose what to
  send" lets you pick one and not the other.
- After an update, the same question comes up once as a sheet over your
  circuit window, after What's New.
- You can change your answer any time in **Settings > Privacy**.

The question reads:

> I work on this version of CedarLogic in my spare time, for free. I can't
> see what breaks or what people actually use unless you tell me. If you're
> willing, let the app send anonymous crash reports and usage stats. It would
> genuinely help, and nothing identifies you: no name, no circuits, no files.
> Thank you either way.
>
> — Levi

This page covers the Mac, Windows and Linux apps; [On Windows](#on-windows)
and [On Linux](#on-linux) say where they differ. It uses the same words as the apps' "See exactly
what's sent".

## Crash reports

If CedarLogic crashes, it can send a report of where in the app it went wrong
(the code's stack), the macOS version and the CedarLogic version. Paths in the
report have your user name taken out (`/Users/<user>/`). There is no trail of
clicks, windows or network requests, and nothing is sent unless the app
crashed. Crash reports go through [Sentry](https://sentry.io) and are deleted
by Sentry after its retention period. Turning them off takes effect the next
time CedarLogic opens.

## Usage stats

At most once a day, CedarLogic sends a small report (JSON, at most 16 KB) to
`https://cedarlogic.netlify.app/api/usage` over HTTPS. It says:

- **The app**: the platform (`mac`), CedarLogic's version and build, the macOS
  version as major.minor (`14.6`), the processor type (`arm64` or `x86_64`)
  and the language code only (`en`, never your region).
- **Counters since the last report**: how many times CedarLogic was opened;
  how long it was in front with the Mac in use (counting stops after 5 minutes
  without a key press or mouse move); how long a window was in focus mode or
  Simulation View; how many times Classroom was opened; whether sync is on;
  and how many times each command was used, by its id (`save`, `tidy`,
  `checkCircuit` ...; at most 200 of them).
- **Settings you've changed from their defaults**, from a fixed list of
  appearance and behaviour settings (`showGrid`, `toolbarStyle`, `accent` ...).
  Values are only on/off, numbers or a few fixed words. The template a new
  circuit starts from is sent as a built-in template's id, or just `yours`.
- **A random id**, made when you turn usage stats on. It's a random string,
  not anything about your Mac, and it changes if you turn usage stats off and
  on again.
- **Dates**: the days the counts cover, and when it was sent to the minute, in
  UTC.

Only what's new goes. The first report after you turn usage stats on is a
"snapshot" of your changed settings; after that each report has only the
settings that changed since the last one (a "delta"), or, when nothing
changed and nothing was counted, a short "still here" (a "ping"). If a report
can't be sent, up to 7 are kept on your Mac and tried again later.

### Never sent

- Your name, or the name on your exports
- Your circuits: their names, contents, notes or drawings
- File names, folders or anything you type
- Your sync code, classroom codes, class names or students
- Your Mac's name, your user name, its serial number or IP address (the server keeps no IP)
- Your time zone or exact times (dates, and the minute in UTC)

The report is built only from a fixed list in the app (`mac/App/UsageReport.swift`,
`UsageSettings.table`); nothing else in your settings is read. A self-test
(`mac/Tools/usage-check.sh`, and `mac/Tools/launch-selftest.sh`) scans real
reports for your home folder, user name, full name, Mac name, recent files,
sync code and classroom codes, and fails if any appear.

### On your Mac

What's been counted is kept in
`~/Library/Application Support/CedarLogic/Usage/usage.json`, readable only by
you (0600), written every 5 minutes and when you quit. The on/off switch and
the random id are in CedarLogic's settings (`cl.usageStats`, `cl.usageId`).
Turning usage stats off deletes the counts on your Mac.

### On the server

The server (the CedarLogic website, on Netlify) keeps, under the random id:
the settings it last heard, one report per day, and when it first and last
heard from the id. It never keeps the address a report came from (rate limits
use a keyed hash of it that changes every day), the User-Agent or the exact
time. Each id's daily reports are deleted after 180 days, and an id with none
left is deleted entirely. Totals for each day, with no ids in them, are kept.

## Seeing and deleting it

- **Settings > Privacy > See What's Sent...** (or "See exactly what's sent" on
  the welcome) shows the next report exactly as it would go.
- **Settings > Privacy** shows when the last report went and what kind it was.
- **Turning usage stats off** tells the server to delete everything under your
  id (`DELETE /api/usage?id=<id>`) and forgets the id. If you're offline, it's
  asked again the next time CedarLogic opens.
- **Delete What's Been Sent...** does the same while leaving usage stats on;
  reports start again under a new id.

## For labs and schools

An administrator can turn either off for every user of a Mac with a managed
preference (a configuration profile for the app's bundle id,
`com.leviholliday.cedarlogic.native`):

- `DisableCrashReporting` (boolean, true): no crash reports
- `DisableUsageStats` (boolean, true): no usage stats

The switches in Settings > Privacy are then greyed out, and nothing of that
kind is counted or sent.

## On Windows

The Windows app asks the same question with the same words, in the same
places: "A small favour" is page 6 of the first-run welcome (no Continue or
Skip there; Escape means No thanks), and anyone updating sees it once as a
sheet over the circuit window, after What's New and before Show Me Around.
**Settings > Privacy** has both switches, See What's Sent..., Last sent (the
date and kind) and Delete What's Been Sent...

**Crash reports** go to the same Sentry project as the Mac's, tagged
`platform=windows`. They use sentry-native's in-process backend: when
CedarLogic crashes, the report (where in the app it went wrong, as addresses
in its modules, and the Windows and CedarLogic versions) is written to
`%LOCALAPPDATA%\CedarLogic\Crash reports` and sent the next time CedarLogic
opens, if crash reports are still on. Before it goes, every path loses its
user name (`C:\Users\<user>\...`), your user name and your PC's name are
taken out wherever they are, and a report that still has one in it is
dropped rather than sent. There's no trail of clicks or network requests,
no session tracking, no user and no memory dump (crashpad's minidump would
carry raw memory and full paths, which can't be cleaned first, so it isn't
used).

**Usage stats** are the same report, with:

- `app.platform` `windows`, `app.os` `10.0` for Windows 10 or `11.0` for
  Windows 11 (build 22000 on), `app.arch` `x86_64` or `aarch64` (ARM PCs).
- The same setting names as the Mac wherever the setting is the same
  (`gateLabels`, `categoryShortcuts`, `trackpadScroll` for the touchpad's
  two-finger scroll...), plus `showPalette` and `checkUpdates`. There's no
  Classroom on Windows, so `classroomSessions` is always 0 and
  `classroomEnabled` isn't sent.
- Commands counted where every one of them runs (`CircuitWindow::run`), by
  the action's id, never a menu's text.
- What's been counted, the random id and any reports waiting to go are in
  `%APPDATA%\CedarLogic\Usage\usage.json`, which only your Windows user can
  open (its folder too). The switch and your answer are in
  `%APPDATA%\CedarLogic\native.ini` (`usageStats`, `crashReports`,
  `privacyAsked`, `privacyChoice`). Turning usage stats off deletes the id
  and the counts, and asks the server to delete what it has.
- Sent with WinHTTP over HTTPS only, as JSON, with no Origin header.

**For labs and schools**, two DWORD values of 1 under
`HKLM\Software\Policies\CedarLogic` (a Group Policy registry item, or
`reg add`):

- `DisableCrashReporting`: no crash reports
- `DisableUsageStats`: no usage stats

The switches in Settings > Privacy are then greyed out, nothing of that kind
is counted or sent, and with both set nobody is asked.

## On Linux

The Linux app (GTK, `linux/`) asks the same question with the same words, in
the same places: "A small favour" is page 6 of 7 of the first-run welcome (no
Continue or Skip there; Escape means No thanks), and anyone updating sees it
once as a sheet over the circuit window, after What's New and before Show Me
Around, never over the launch screen or the intro. On a small screen (the
Raspberry Pi touchscreen's 800x480, a Wayland desktop's panels) both take the
room the screen has and scroll. **Settings > Privacy** has both switches, See
exactly what's sent, Last sent (the date and kind) and Delete what's been sent.

**Crash reports** aren't in the Linux app yet: it has no crash reporter built
in and sends none. The question is the same (one consent for both), and your
answer is kept (`crashReports` in `native.ini`) for a version that has one;
Settings > Privacy and "See exactly what's sent" say so.

**Usage stats** are the same report, with:

- `app.platform` `linux`; `app.os` the distribution and its major version
  from `/etc/os-release` (`ID` and `VERSION_ID` up to the first dot:
  `debian 12`, `ubuntu 24`, `raspbian 12`; a rolling one such as Arch just
  `arch`; 16 characters at most); `app.arch` `x86_64` or `aarch64`
  (Raspberry Pi 4 and 5, ARM laptops); `app.lang` the language only.
- The same setting names as the Mac and Windows wherever the setting is the
  same (`themeMode`, `accent`, `showGrid`, `gridStyle`, `majorGrid`,
  `wireThickness`, `gateSize`, `gateLabels`, `toolbarStyle`, `actionsOn`,
  `actionsCollapsed`, `actionsHoverOpen`, `openReplaces`, `tidyMode`,
  `confirmQuit`, `atLaunch`, `testingGroup`...), plus `showPalette` and
  `checkUpdates`. There's no Classroom on Linux, so `classroomSessions` is
  always 0 and `classroomEnabled` isn't sent.
- Commands counted where every one of them runs (the window's actions: the
  menus, the keys, the toolbar, the Actions panel), by the action's id, never
  a menu's text. Time counts while a CedarLogic window has the keyboard and
  there's been a key, click or pointer move in it in the last 5 minutes.
- What's been counted, the random id and any reports waiting to go are in
  `~/.config/CedarLogic/usage.json`, mode 0600 (only you can read it; the
  folder is 0700). The switch and your answer are in
  `~/.config/CedarLogic/native.ini` (`usageStats`, `crashReports`,
  `privacyAsked`, `privacyChoice`). Turning usage stats off deletes the id
  and the counts, and asks the server to delete what it has.
- Sent once a day at most, at launch, in the background, through the `curl`
  program (run directly, never through a shell, not reading `~/.curlrc`):
  HTTPS only, as JSON, with no Origin header. Without curl nothing is sent
  (the `.deb` recommends it).

**For labs and schools**, a file `/etc/cedarlogic/policy.ini` (root's, so
users can't change it) with either or both of:

```ini
[Policy]
DisableCrashReporting=true
DisableUsageStats=true
```

(`true`, `1`, `yes` or `on`; the section name doesn't matter.) The switches
in Settings > Privacy are then greyed out, nothing of that kind is counted or
sent, and with both set nobody is asked (the welcome passes the page by).

## For developers

- The app never sends from a test copy, a self-test, a render run or CI.
  `CL_USAGE_URL` can point it at a stand-in on the same Mac (127.0.0.1) or,
  over HTTPS, at the site or one of its Netlify drafts.
- `mac/Tools/usage-check.sh` checks the client against a local stand-in of the
  server; `CL_USAGE_URL=https://<draft>--cedarlogic.netlify.app/api/usage
  mac/Tools/usage-check.sh --live` runs snapshot, delta, ping and DELETE
  against a real one with a throwaway id.
- Windows: `CedarLogic.exe --usage-check` (with `CL_USAGE_URL` pointing at
  `mac/Tools/usage-check/server.py`) runs the same checks against the same
  stand-in; `--usage-live` runs the live one; `--crash-check` checks the
  crash reports' cleaning; `--privacy-check` presents the welcome's page and
  the sheet in real windows and answers them with real clicks and keys. CI
  (`.github/workflows/windows-native.yml`) runs them all, and crashes a copy
  of the app on purpose to check the report that arrives at a stand-in for
  Sentry (`windows/Tools/sentry-mock.py`). Nothing is sent from a test or
  picture run, from CI, or with `CEDARLOGIC_HOME` or any `CL_` variable set,
  unless `CL_USAGE_URL` names a stand-in on the same PC or the site or one of
  its drafts.
- Linux: `cedarlogic --usage-check` (with `CL_USAGE_URL` pointing at
  `mac/Tools/usage-check/server.py`, which checks `app.os` as
  `<distribution> <major>` for Linux) runs the same checks against the same
  stand-in; `--usage-live` runs the live one (with a throwaway id, deleted at
  the end); `--privacy-check` presents the welcome's page and the sheet in
  real windows on an X display and answers them with the real pointer
  (xdotool), and with `CL_POLICY_FILE` naming a stand-in policy file and
  `CL_EXPECT_POLICY=1` checks that nothing is asked, counted or offered. CI
  (`.github/workflows/linux-native.yml`) runs the usage check and the consent
  check at 1600x1000 and 800x480, and with the policy. Nothing is sent from a
  test or picture run, from CI, or with any `CL_` variable set, unless
  `CL_USAGE_URL` names a stand-in on the same computer or the site or one of
  its drafts.
- The server side is `netlify/functions/usage.mjs` and `netlify/lib/usage.mjs`
  in the website's repository.
