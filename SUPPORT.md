# Support

## Which versions get fixes

Fixes go into the **current release** and the **current beta**. Older versions don't get fixes; the apps update themselves, so the fix is to update.

| App | Gets fixes | Notes |
|---|---|---|
| CedarLogic for Mac | the newest release and the newest beta | Normal testers follow releases, Beta testers get betas first (Settings > General > Testing group). On 2026-11-12 the newest release is 0.4.2. |
| CedarLogic for Windows | the newest release and the newest test build | Normal testers follow releases (0.4.2 on 2026-11-12), Beta testers get the rolling test build too. |
| CedarLogic for Linux | the newest release and the newest test build | Normal testers follow releases (0.4.2 on 2026-11-12), Beta testers get the rolling test build too. |
| CedarLogic Online | the website as it is now | |
| CedarLogic Classic (the wx app) | none | Retired. The last version was 4.0.2 (beta), September 2026. Use the native app for your computer instead. |

Every release is on [GitHub](https://github.com/leviholliday/CedarLogic-Releases/releases); [CHANGELOG.md](CHANGELOG.md) says what changed.

## Where to ask

- **Send Feedback in the app** (on the Mac: Help > Send Feedback, or the speech bubble on the toolbar). It's the quickest way to reach the developer, and it can include a screenshot, a short recording and your circuit.
- **GitHub issues** on [leviholliday/CedarLogic](https://github.com/leviholliday/CedarLogic/issues), for bugs and ideas. The forms ask for what helps.
- **Email:** [cedarlogic@proton.me](mailto:cedarlogic@proton.me), for anything that doesn't fit an issue.
- **Security problems:** see [SECURITY.md](SECURITY.md). Please don't post them in public.

Replies usually come within a week.

## Privacy

The apps don't track you, show ads or need an account. They only send something when you use one of the features below. This is what each one sends, read from the code.

### Send Feedback (every app)

Sent only when you press Send, to cedarlogic.netlify.app:

- what you write (a summary, the details), the tags and priority you pick;
- your name as it is set in the app (Settings), and your email only if you type one in;
- attachments you add: screenshots, a recording of the CedarLogic window, and the open circuit if you tick that box (without your Notes);
- about the app: its version, build and testing group, and a random id for this install (used to count the 10-a-day limit);
- about the computer: the system version, model, memory, number of processors, screen sizes, language setting and light or dark mode, and on the Mac the chip;
- a short hash of your network address (the address itself isn't kept).

Reports are kept on the website's storage (Netlify) until the developer deletes them. The summary line, the priority, the app and its version also go to the developer's phone as a notification, through the ntfy.sh service.

### Sync (every app, when you turn it on)

Your circuits are encrypted on your device before they leave it, with a key made from your sync code. The code stays on your devices (when you link a device by scanning, it passes between them encrypted). The website stores the encrypted copies and can't read them. It can see that a sync space exists, how many records it has, their random ids, versions and sizes, when each changed, and the network addresses requests come from (for rate limits, kept only as a keyed hash). It can't see names, contents or which circuit is which. A space that is never used for a year is deleted. The design is in [docs/SYNC.md](docs/SYNC.md).

### Classroom (Mac and the website, when you join or run a class)

Classes use codes, not accounts. Names, assignments, hand-ins and answers are encrypted before they leave a device, so the class service (on Cloudflare) can't read them. The design is in [docs/CLASSROOM.md](docs/CLASSROOM.md).

### Crash reports and usage stats (Mac only, if you say yes)

Both are off until you say yes. The Mac app asks once, one question for both (the welcome's "A small favour" page, or a sheet after an update), and Settings > Privacy turns each on or off, shows what's sent and deletes what has been. A school can turn them off for everyone with the `DisableCrashReporting` and `DisableUsageStats` settings. [docs/PRIVACY.md](docs/PRIVACY.md) has exactly what each one sends; usage stats are counts and settings, never circuits, files or names.

Crash reports go to [Sentry](https://sentry.io) and carry where the app crashed (the stack of each thread), the app version and build, your testing group, and the basic device details Sentry adds (macOS version, Mac model, memory). They don't carry your circuits, files or name: Sentry's trail of clicks, windows and web requests is off, its personal-data option is off, and any `/Users/<your name>/` in a path becomes `/Users/<user>/` before the report leaves. The Windows and Linux apps have no crash reporter.

### Updates

The apps check for updates by downloading a file from GitHub (the Mac a Sparkle feed, Windows and Linux the release list). Nothing about you is sent beyond what any web request carries.

### The website

cedarlogic.netlify.app asks before it measures anything. Google Analytics and Cloudflare Web Analytics load only after you choose Accept; until then neither runs. If you accept, Google gets basic visit information (pages without query strings, the referring site, browser and device, a pseudonymous browser id in a cookie that lasts up to 60 days); advertising features and Google Signals are off. "Privacy settings" in the footer changes your choice at any time. The pages that hold circuits and codes (CedarLogic Online, `/sync/`, Classroom) don't load analytics at all. The full notice is in that footer dialog.
