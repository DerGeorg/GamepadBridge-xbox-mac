//
//  menubar.swift — GamepadBridge's menu bar item
//  Copyright (C) 2026 GamepadBridge contributors
//  SPDX-License-Identifier: GPL-2.0-or-later
//
//  Without this the app is a background process with no way to see whether it
//  is working or to stop it short of Activity Monitor. The menu shows the link
//  state and battery level, offers pairing, and quits cleanly.
//
//  It polls the status board once a second rather than being pushed to from
//  the USB thread: a status line does not need to be more immediate than that,
//  and polling keeps AppKit entirely on the main thread.
//

import AppKit

#if GAMEPADBRIDGE_SPARKLE
import Sparkle
#endif

private func connectionText() -> String {
    var buffer = [CChar](repeating: 0, count: 256)

    gpb_status_connection(&buffer, buffer.count)

    return String(cString: buffer)
}

private func batteryText() -> String {
    var buffer = [CChar](repeating: 0, count: 256)

    gpb_status_battery(&buffer, buffer.count)

    return String(cString: buffer)
}

private final class MenuBar: NSObject {
    private let item = NSStatusBar.system.statusItem(
        withLength: NSStatusItem.variableLength)

    private let connection = NSMenuItem(title: "", action: nil, keyEquivalent: "")
    private let battery = NSMenuItem(title: "", action: nil, keyEquivalent: "")

    /*
     * Sparkle downloads and installs updates itself, and asks on first launch
     * whether it may check automatically. Without it the menu item falls back
     * to the built-in check, which can only tell you a new version exists and
     * open the release page — still better than a build that never mentions
     * updates at all.
     */
#if GAMEPADBRIDGE_SPARKLE
    private let updater = SPUStandardUpdaterController(startingUpdater: true,
                                                       updaterDelegate: nil,
                                                       userDriverDelegate: nil)
#endif

    private let updates = NSMenuItem(title: "Check for Updates…",
                                     action: nil,
                                     keyEquivalent: "")

    // Which version is running is the first thing anyone is asked in a bug
    // report, and the app has no window to put it in.
    private let version = NSMenuItem(
        title: "GamepadBridge \(currentVersion())",
        action: nil,
        keyEquivalent: "")

    private var timer: Timer?

    override init() {
        super.init()

        let menu = NSMenu()

        connection.isEnabled = false
        battery.isEnabled = false

        menu.addItem(connection)
        menu.addItem(battery)
        menu.addItem(.separator())

        let pair = NSMenuItem(title: "Pair a Controller",
                              action: #selector(startPairing),
                              keyEquivalent: "")
        pair.target = self
        menu.addItem(pair)

        menu.addItem(.separator())

        version.isEnabled = false
        menu.addItem(version)

#if GAMEPADBRIDGE_SPARKLE
        updates.target = updater
        updates.action = #selector(
            SPUStandardUpdaterController.checkForUpdates(_:))
#else
        updates.target = self
        updates.action = #selector(checkForUpdates)
#endif

        menu.addItem(updates)

        menu.addItem(.separator())

        let quit = NSMenuItem(title: "Quit GamepadBridge",
                              action: #selector(quit),
                              keyEquivalent: "q")
        quit.target = self
        menu.addItem(quit)

        item.menu = menu

        refresh()

        timer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) {
            [weak self] _ in self?.refresh()
        }
    }

    private func refresh() {
        connection.title = connectionText()

        let level = batteryText()

        battery.title = "Battery: \(level)"
        battery.isHidden = level.isEmpty

        // A filled icon while a controller is attached, an outline otherwise,
        // so the state is readable without opening the menu at all.
        let connected = gpb_status_controller_present() != 0
        let symbol = connected ? "gamecontroller.fill" : "gamecontroller"

        let image = NSImage(systemSymbolName: symbol,
                            accessibilityDescription: "GamepadBridge")
        image?.isTemplate = true

        item.button?.image = image
    }

    @objc private func startPairing() {
        gpb_request_pairing()
    }

    @objc private func quit() {
        gpb_request_quit()
    }

    // MARK: - Updates (only without Sparkle)

#if !GAMEPADBRIDGE_SPARKLE
    @objc private func checkForUpdates() {
        updates.title = "Checking…"
        updates.isEnabled = false

        let task = URLSession.shared.dataTask(with: releasesAPI) {
            [weak self] data, _, error in

            DispatchQueue.main.async {
                self?.updates.title = "Check for Updates…"
                self?.updates.isEnabled = true

                self?.report(data: data, error: error)
            }
        }

        task.resume()
    }

    private func report(data: Data?, error: Error?) {
        let alert = NSAlert()
        let current = currentVersion()

        if let error = error {
            alert.alertStyle = .warning
            alert.messageText = "Could not check for updates"
            alert.informativeText = error.localizedDescription
            alert.addButton(withTitle: "OK")
        }

        else if let data = data, let latest = latestVersion(from: data) {
            if isNewer(latest, than: current) {
                alert.messageText = "GamepadBridge \(latest) is available"
                alert.informativeText = "You are running \(current)."
                alert.addButton(withTitle: "Open Release Page")
                alert.addButton(withTitle: "Later")
            }

            else {
                alert.messageText = "GamepadBridge is up to date"
                alert.informativeText = "You are running \(current)."
                alert.addButton(withTitle: "OK")
            }
        }

        else {
            alert.alertStyle = .warning
            alert.messageText = "Could not read the release list"
            alert.informativeText = "The server answered, but not with "
                + "anything this version understands."
            alert.addButton(withTitle: "OK")
        }

        NSApplication.shared.activate(ignoringOtherApps: true)

        if alert.runModal() == .alertFirstButtonReturn,
           alert.buttons.first?.title == "Open Release Page" {
            NSWorkspace.shared.open(releasesPage)
        }
    }
#endif
}

nonisolated(unsafe) private var menuBar: MenuBar?

// MARK: - C ABI consumed by src/main.cpp

@_cdecl("gpb_menubar_run")
public func gpb_menubar_run() {
    let app = NSApplication.shared

    // Accessory, not regular: a menu bar item, no Dock tile, no main window.
    app.setActivationPolicy(.accessory)

    menuBar = MenuBar()

    app.run()
}

@_cdecl("gpb_menubar_stop")
public func gpb_menubar_stop() {
    DispatchQueue.main.async {
        let app = NSApplication.shared

        app.stop(nil)

        /*
         * stop() only takes effect the next time the loop finishes processing
         * an event, so hand it one rather than waiting for the user to move
         * the mouse over the menu bar.
         */
        if let wake = NSEvent.otherEvent(with: .applicationDefined,
                                         location: .zero,
                                         modifierFlags: [],
                                         timestamp: 0,
                                         windowNumber: 0,
                                         context: nil,
                                         subtype: 0,
                                         data1: 0,
                                         data2: 0) {
            app.postEvent(wake, atStart: true)
        }
    }
}

/*
 * Asked before the firmware is fetched, from main() on the main thread and
 * before app.run(), so NSAlert has an application to attach to.
 */
@_cdecl("gpb_confirm_firmware")
public func gpb_confirm_firmware(_ message: UnsafePointer<CChar>) -> Int32 {
    let app = NSApplication.shared
    app.setActivationPolicy(.accessory)

    let alert = NSAlert()
    alert.messageText = "GamepadBridge needs the adapter's firmware"
    alert.informativeText = String(cString: message)
    alert.alertStyle = .informational
    alert.addButton(withTitle: "Download")
    alert.addButton(withTitle: "Quit")

    app.activate(ignoringOtherApps: true)

    return alert.runModal() == .alertFirstButtonReturn ? 1 : 0
}

/*
 * Shown when Input Monitoring is missing. Returns 1 to carry on, 0 to quit.
 *
 * Carrying on matters: macOS only offers its own "Quit and Reopen" button
 * when the app granted the permission is actually running. An app that exits
 * first leaves the user to find and start it again themselves.
 */
@_cdecl("gpb_permission_alert")
public func gpb_permission_alert(_ message: UnsafePointer<CChar>,
                                 _ settingsURL: UnsafePointer<CChar>) -> Int32 {
    let app = NSApplication.shared
    app.setActivationPolicy(.accessory)

    let alert = NSAlert()
    alert.messageText = "GamepadBridge needs permission"
    alert.informativeText = String(cString: message)
    alert.alertStyle = .warning
    alert.addButton(withTitle: "Open System Settings")
    alert.addButton(withTitle: "Quit")

    app.activate(ignoringOtherApps: true)

    guard alert.runModal() == .alertFirstButtonReturn else {
        return 0
    }

    if let url = URL(string: String(cString: settingsURL)) {
        NSWorkspace.shared.open(url)
    }

    return 1
}
