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
