# Homebrew cask for GamepadBridge.
#
# Copy this into your tap (a repository named homebrew-tap, file
# Casks/gamepadbridge.rb) and bump version + sha256 for each release;
# scripts/release.sh prints both.
#
# A cask rather than a formula on purpose: the virtual HID entitlement is
# authorized by a provisioning profile embedded at signing time, so an app
# built from source on someone else's machine would not have it and would
# fail silently. Only the signed build works, so the signed build is what
# gets shipped.
cask "gamepadbridge" do
  version "1.0.2"
  sha256 "cf7650b97c2b6c2d861b2326107b8516fe4c6a8a151ab1fed9e024dfe462db51"

  # The package registry, not the /-/releases/.../downloads/ permalink: GitLab
  # treats an absolute asset URL as external and answers that permalink with a
  # "you are being redirected away from GitLab" interstitial, so a download
  # yields 571 bytes of HTML instead of a disk image.
  url "https://gitlab.dergeorg.at/api/v4/projects/mac%2Fgamepadbridge/" \
      "packages/generic/gamepadbridge/#{version}/GamepadBridge.dmg"
  name "GamepadBridge"
  desc "Use Xbox wireless controllers through the Xbox Wireless Adapter"
  homepage "https://gitlab.dergeorg.at/mac/gamepadbridge"

  livecheck do
    url "https://gitlab.dergeorg.at/api/v4/projects/mac%2Fgamepadbridge/releases"
    strategy :json do |json|
      json.map { |release| release["tag_name"]&.delete_prefix("v") }
    end
  end

  # CoreHID's HIDVirtualDevice needs macOS 15.
  depends_on macos: ">= :sequoia"

  app "GamepadBridge.app"

  uninstall quit: "at.dergeorg.gamepadbridge"

  # The firmware is Microsoft's and was downloaded on first run, not shipped.
  zap trash: [
    "~/Library/Application Support/GamepadBridge",
  ]

  caveats <<~EOS
    GamepadBridge runs as a menu bar item. On first launch it offers to
    download the adapter's firmware from Microsoft (about 200 KB), which
    cannot be bundled with the app.

    macOS attributes device permissions to whichever app starts the driver,
    so launch GamepadBridge.app itself rather than its binary from a
    terminal, and allow it under System Settings > Privacy & Security >
    Input Monitoring when asked.
  EOS
end
