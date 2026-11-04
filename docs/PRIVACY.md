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

This page covers the Mac app. It uses the same words as the app's "See
exactly what's sent" sheet.

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

## For developers

- The app never sends from a test copy, a self-test, a render run or CI.
  `CL_USAGE_URL` can point it at a stand-in on the same Mac (127.0.0.1) or,
  over HTTPS, at the site or one of its Netlify drafts.
- `mac/Tools/usage-check.sh` checks the client against a local stand-in of the
  server; `CL_USAGE_URL=https://<draft>--cedarlogic.netlify.app/api/usage
  mac/Tools/usage-check.sh --live` runs snapshot, delta, ping and DELETE
  against a real one with a throwaway id.
- The server side is `netlify/functions/usage.mjs` and `netlify/lib/usage.mjs`
  in the website's repository.
