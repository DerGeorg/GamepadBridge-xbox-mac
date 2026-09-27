//
//  menubar.swift — GamepadBridge's menu bar item
//  Copyright (C) 2026 GamepadBridge contributors
//  SPDX-License-Identifier: GPL-2.0-or-later
//
//  Without this the app is a background process with no way to see whether it
//  is working or to stop it short of Activity Monitor. The menu shows each
//  connected controller with its battery level, offers pairing, and quits
//  cleanly.
//
//  It polls the status board once a second rather than being pushed to from
//  the USB thread: a status line does not need to be more immediate than that,
//  and polling keeps AppKit entirely on the main thread.
//

import AppKit
import ServiceManagement

#if GAMEPADBRIDGE_SPARKLE
import Sparkle
#endif

// The message and one line per controller - see status_c.h.
private func statusLines() -> [String] {
    (0..<Int(gpb_status_line_count())).map { index in
        var buffer = [CChar](repeating: 0, count: 256)

        gpb_status_line(Int32(index), &buffer, buffer.count)

        return String(cString: buffer)
    }
}

/*
 * A status line as a view of its own rather than an item title. An open menu
 * lays out its titles once, when it opens, and cuts off whatever grows or
 * arrives afterwards — a controller connecting while the menu was open came
 * out as "Controller…: medium", however wide the menu was. A view keeps the
 * width it is given.
 */
private final class StatusLine: NSView {
    // Measured from a standard menu with NSMenu.size: rows are 24 points
    // high, and a title sits 16 points in from either edge - 14 more on the
    // left once any item carries a check mark, since the menu then makes
    // room for the mark in front of every title. A view has to follow that
    // by itself, or it stays behind while every other line moves.
    static let height: CGFloat = 24
    static let inset: CGFloat = 16
    static let stateColumn: CGFloat = 14

    private static func width(of text: String) -> CGFloat {
        ceil((text as NSString)
            .size(withAttributes: [.font: NSFont.menuFont(ofSize: 0)]).width)
            + 2 * inset
    }

    // Wide enough for the longest a controller line can become, and for its
    // own text when that is longer still - a notice, say.
    static let width = width(of: "Controller 16 \u{00b7} Battery: medium")

    private let label = NSTextField(labelWithString: "")

    init(text: String, indent: CGFloat) {
        super.init(frame: NSRect(x: 0, y: 0,
                                 width: max(StatusLine.width,
                                            StatusLine.width(of: text)) + indent,
                                 height: StatusLine.height))
        autoresizingMask = [.width]

        label.font = NSFont.menuFont(ofSize: 0)
        label.textColor = .disabledControlTextColor
        label.lineBreakMode = .byTruncatingTail
        label.translatesAutoresizingMaskIntoConstraints = false
        addSubview(label)

        NSLayoutConstraint.activate([
            label.leadingAnchor.constraint(equalTo: leadingAnchor,
                                           constant: StatusLine.inset + indent),
            label.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor,
                                            constant: -StatusLine.inset),
            label.centerYAnchor.constraint(equalTo: centerYAnchor),
        ])

        label.stringValue = text
    }

    required init?(coder: NSCoder) {
        fatalError("not used from a nib")
    }

}

private func statusItem(_ text: String, indent: CGFloat) -> NSMenuItem {
    let entry = NSMenuItem()
    entry.view = StatusLine(text: text, indent: indent)
    entry.isEnabled = false
    return entry
}

private final class MenuBar: NSObject {
    private let item = NSStatusBar.system.statusItem(
        withLength: NSStatusItem.variableLength)

    /*
     * Pairing is two different things. The adapter is put into pairing mode
     * from here; the Xbox 360 receiver only by its own button - there is no
     * known command for it - so its entry explains the two buttons instead.
     * Which of them show, and what they are called, follows what is plugged
     * in (see updatePairing).
     */
    private let pairAdapter = NSMenuItem(title: "Pair a Controller",
                                         action: #selector(startPairing),
                                         keyEquivalent: "")
    private let pairReceiver = NSMenuItem(title: "Pair a Controller…",
                                          action: #selector(explainReceiverPairing),
                                          keyEquivalent: "")

    /*
     * When another app holds the Xbox 360 receiver, nothing behind it can be
     * listed - not even which controllers are there. An entry says who has
     * it, and opens what that means and what to do.
     */
    private let receiverBusy = NSMenuItem(title: "",
                                          action: #selector(explainReceiverBusy),
                                          keyEquivalent: "")
    private var receiverOwner = ""

    // The status lines at the top of the menu, and what they show.
    private var lines: [NSMenuItem] = []
    private var shownLines: [String] = []
    private var shownIndent: CGFloat = 0

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
    /*
     * On by default, so the app is there before anything else can take the
     * Xbox 360 receiver - whoever gets to it first keeps it, and Steam takes
     * it when it can. Switched off here, it stays off.
     */
    private let openAtLogin = NSMenuItem(title: "Open at Login",
                                         action: #selector(toggleOpenAtLogin),
                                         keyEquivalent: "")

    private let version = NSMenuItem(
        title: "GamepadBridge \(currentVersion())",
        action: nil,
        keyEquivalent: "")

    private let permissions = PermissionPanel()

    private var timer: Timer?

    override init() {
        super.init()

        let menu = NSMenu()

        // The status lines go in at the very top, in refresh().
        receiverBusy.target = self
        receiverBusy.isHidden = true
        menu.addItem(receiverBusy)
        menu.addItem(.separator())

        pairAdapter.target = self
        pairReceiver.target = self
        menu.addItem(pairAdapter)
        menu.addItem(pairReceiver)

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

        openAtLogin.target = self
        menu.addItem(openAtLogin)

        menu.addItem(.separator())

        let quit = NSMenuItem(title: "Quit GamepadBridge",
                              action: #selector(quit),
                              keyEquivalent: "q")
        quit.target = self
        menu.addItem(quit)

        menu.delegate = self
        item.menu = menu

        setUpOpenAtLoginOnce()
        updateOpenAtLogin()

        refresh()

        /*
         * In the common modes, not the default one: while a menu is open the
         * run loop tracks it in a mode of its own, and a timer scheduled the
         * usual way stands still - the open menu kept showing whatever was
         * true when it was opened, however many controllers came and went.
         */
        let ticker = Timer(timeInterval: 1.0, repeats: true) {
            [weak self] _ in self?.refresh()
        }

        RunLoop.main.add(ticker, forMode: .common)
        timer = ticker
    }

    private func refresh() {
        showLines(statusLines())
        updatePairing()
        updateReceiverBusy()

        // A filled icon while a controller is attached, an outline otherwise,
        // so the state is readable without opening the menu at all.
        let connected = gpb_status_controller_present() != 0
        let symbol = connected ? "gamecontroller.fill" : "gamecontroller"

        let image = NSImage(systemSymbolName: symbol,
                            accessibilityDescription: "GamepadBridge")
        image?.isTemplate = true

        item.button?.image = image

        // Whenever something is refused for want of a permission - at startup
        // or hours later when a controller connects - the window comes back.
        if gpb_needs_permission() != 0, !permissions.isVisible {
            permissions.show()
        }
    }

    // Rebuilt only when something changed, so an open menu does not flicker.
    private func showLines(_ texts: [String]) {
        // "Open at Login" is the only item that can carry a check mark.
        let indent = openAtLogin.state == .on ? StatusLine.stateColumn : 0

        guard texts != shownLines || indent != shownIndent,
              let menu = item.menu else {
            return
        }

        shownLines = texts
        shownIndent = indent

        lines.forEach(menu.removeItem)
        lines = texts.map { statusItem($0, indent: indent) }

        for (position, entry) in lines.enumerated() {
            menu.insertItem(entry, at: position)
        }
    }

    private func updatePairing() {
        let adapter = gpb_status_adapter_present() != 0
        let receiver = gpb_status_receiver_present() != 0

        // With neither plugged in, one greyed-out entry says pairing exists.
        pairAdapter.isHidden = receiver && !adapter
        pairAdapter.title = receiver ? "Pair an Xbox One Controller" : "Pair a Controller"

        pairReceiver.isHidden = !receiver
        pairReceiver.title = adapter ? "Pair an Xbox 360 Controller…" : "Pair a Controller…"
    }

    private func updateReceiverBusy() {
        var buffer = [CChar](repeating: 0, count: 128)

        gpb_status_receiver_owner(&buffer, buffer.count)

        receiverOwner = String(cString: buffer)
        receiverBusy.isHidden = receiverOwner.isEmpty
        receiverBusy.title = "Xbox 360 receiver in use by \(receiverOwner)…"
    }

    @objc private func explainReceiverBusy() {
        let owner = receiverOwner.isEmpty ? "another app" : receiverOwner

        let alert = NSAlert()
        alert.messageText = "\(owner) is using the Xbox 360 receiver"
        alert.informativeText = "\(owner) has taken the receiver for itself, "
            + "and while it holds it GamepadBridge cannot see anything behind "
            + "it - not even which controllers are connected. They may still "
            + "work inside \(owner), but nowhere else.\n\n"
            + "To use them everywhere, quit \(owner). GamepadBridge takes the "
            + "receiver over within a couple of seconds, by itself.\n\n"
            + "Whoever gets to the receiver first keeps it: with GamepadBridge "
            + "running before \(owner) starts, it stays here."
        alert.addButton(withTitle: "OK")

        NSApplication.shared.activate(ignoringOtherApps: true)
        alert.runModal()
    }

    // MARK: - Open at Login

    private static let openAtLoginSetUpKey = "OpenAtLoginSetUp"

    /*
     * The default, applied once per Mac: the first time an installed copy
     * runs, it adds itself to the login items. Only from /Applications -
     * a development build would register the build folder, and since both
     * share a bundle identifier, the installed app would then never do it.
     */
    private func setUpOpenAtLoginOnce() {
        let defaults = UserDefaults.standard

        guard !defaults.bool(forKey: MenuBar.openAtLoginSetUpKey),
              Bundle.main.bundlePath.hasPrefix("/Applications/") else {
            return
        }

        // Once, whether it works or not: asking again at every launch
        // would overrule someone who said no in System Settings.
        defaults.set(true, forKey: MenuBar.openAtLoginSetUpKey)

        try? SMAppService.mainApp.register()
    }

    private func updateOpenAtLogin() {
        openAtLogin.state = SMAppService.mainApp.status == .enabled ? .on : .off
    }

    @objc private func toggleOpenAtLogin() {
        let service = SMAppService.mainApp

        do {
            if service.status == .enabled {
                try service.unregister()
            } else {
                try service.register()
            }
        } catch {
            // Switched off in System Settings > General > Login Items: only
            // the user can switch it back on there, so take them there.
            if service.status == .requiresApproval {
                SMAppService.openSystemSettingsLoginItems()
            } else {
                let alert = NSAlert()
                alert.messageText = "Could not change Open at Login"
                alert.informativeText = error.localizedDescription
                alert.addButton(withTitle: "OK")

                NSApplication.shared.activate(ignoringOtherApps: true)
                alert.runModal()
            }
        }

        updateOpenAtLogin()
        showLines(statusLines())
    }

    @objc private func startPairing() {
        gpb_request_pairing()
    }

    @objc private func explainReceiverPairing() {
        let alert = NSAlert()
        alert.messageText = "Pairing an Xbox 360 controller"
        alert.informativeText = "The Xbox 360 receiver pairs with its own "
            + "button, not from the Mac.\n\n"
            + "1. Press the button on the receiver. Its light starts blinking.\n"
            + "2. Within 20 seconds, press the small round connect button on "
            + "the top edge of the controller, between the bumpers.\n\n"
            + "The ring spins faster, then lights one quarter: that is the "
            + "controller's slot. A controller paired once connects by "
            + "itself from then on."
        alert.addButton(withTitle: "OK")

        NSApplication.shared.activate(ignoringOtherApps: true)
        alert.runModal()
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

// The menu enables every item with a target by itself; pairing the adapter
// needs one plugged in.
extension MenuBar: NSMenuItemValidation {
    func validateMenuItem(_ menuItem: NSMenuItem) -> Bool {
        if menuItem === pairAdapter {
            return gpb_status_adapter_present() != 0
        }

        return true
    }
}

// Up to date the moment it opens, rather than up to a second behind.
extension MenuBar: NSMenuDelegate {
    func menuWillOpen(_ menu: NSMenu) {
        // Checked when shown rather than every second: it can also change
        // in System Settings.
        updateOpenAtLogin()

        refresh()
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
 * Asked before the firmware is fetched - when an adapter is plugged in, from
 * the thread that found it. A dialog belongs on the main thread, so this
 * hops there and waits for the answer.
 */
@_cdecl("gpb_confirm_firmware")
public func gpb_confirm_firmware(_ message: UnsafePointer<CChar>) -> Int32 {
    if !Thread.isMainThread {
        let text = String(cString: message)

        return DispatchQueue.main.sync {
            text.withCString { askForFirmware($0) }
        }
    }

    return askForFirmware(message)
}

private func askForFirmware(_ message: UnsafePointer<CChar>) -> Int32 {
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
