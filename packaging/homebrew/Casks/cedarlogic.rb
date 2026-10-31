cask "cedarlogic" do
  version "0.3.4"
  sha256 "8e09f61dff66b8dbb2a43e6c38700f30789e569c819a0132a2686776267862ca"

  url "https://github.com/leviholliday/CedarLogic-Releases/releases/download/v#{version}/CedarLogic-#{version}-Mac.dmg"
  name "CedarLogic"
  desc "Digital logic simulator"
  homepage "https://cedarlogic.netlify.app/"

  # Full releases only (the Mac betas are pre-releases), and only ones with a
  # Mac DMG: a release can be Windows/Linux only.
  livecheck do
    url :url
    regex(/^v?(\d+(?:\.\d+)+)$/i)
    strategy :github_releases do |json, regex|
      json.map do |release|
        next if release["draft"] || release["prerelease"]

        match = release["tag_name"]&.match(regex)
        next if match.blank?
        next unless release["assets"]&.any? { |asset| asset["name"] == "CedarLogic-#{match[1]}-Mac.dmg" }

        match[1]
      end
    end
  end

  # It updates itself (Sparkle, Help > Check for Updates).
  auto_updates true
  depends_on arch: :arm64
  depends_on macos: :sonoma

  app "CedarLogic.app"

  uninstall quit: "com.leviholliday.cedarlogic.native"

  # Application Support/CedarLogic is also where "Your Circuits", your own
  # parts and templates live: `brew uninstall --zap` deletes them.
  # CedarLogic keeps nothing in the Keychain (its sync secret is a file in
  # Application Support/CedarLogic/Sync).
  zap trash: [
    "~/Library/Application Support/CedarLogic",
    "~/Library/Caches/com.leviholliday.cedarlogic.native",
    "~/Library/HTTPStorages/com.leviholliday.cedarlogic.native",
    "~/Library/HTTPStorages/com.leviholliday.cedarlogic.native.binarycookies",
    "~/Library/Preferences/com.leviholliday.cedarlogic.native.plist",
    "~/Library/Saved Application State/com.leviholliday.cedarlogic.native.savedState",
    "~/Library/WebKit/com.leviholliday.cedarlogic.native",
  ]

  caveats <<~EOS
    CedarLogic isn't notarized by Apple (it's a free app from an independent developer).
    The first time you open it, macOS may block it: open System Settings > Privacy & Security,
    scroll down and click "Open Anyway". After that it opens normally and updates itself.

    `brew uninstall --zap cedarlogic` also deletes "Your Circuits" (the library inside
    the app), your own parts and templates. Circuits you saved as .cdl files elsewhere stay.
  EOS
end
