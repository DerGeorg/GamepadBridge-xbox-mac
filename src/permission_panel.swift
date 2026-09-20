//
//  permission_panel.swift — guidance that stays put
//  Copyright (C) 2026 GamepadBridge contributors
//  SPDX-License-Identifier: GPL-2.0-or-later
//
//  An alert was the wrong shape for this. Pressing its button dismisses it,
//  so the instructions disappeared at the exact moment System Settings opened
//  and they were needed. This is an ordinary window that stays on screen
//  until the permission is granted, and then says so.
//

import AppKit

final class PermissionPanel: NSObject {
    private var window: NSWindow?
    private var timer: Timer?
    private let settings: String

    init(settingsURL: String) {
        settings = settingsURL
    }

    func show(message: String) {
        let content = NSStackView()
        content.orientation = .vertical
        content.alignment = .leading
        content.spacing = 14
        content.edgeInsets = NSEdgeInsets(top: 20, left: 20, bottom: 20, right: 20)

        let heading = NSTextField(labelWithString: "GamepadBridge needs permission")
        heading.font = .boldSystemFont(ofSize: 15)

        let body = NSTextField(wrappingLabelWithString: message)
        body.preferredMaxLayoutWidth = 380

        let buttons = NSStackView()
        buttons.orientation = .horizontal
        buttons.spacing = 10

        let open = NSButton(title: "Open System Settings", target: self,
                            action: #selector(openSettings))
        open.keyEquivalent = "\r"

        // Dragging the app onto the list always works, whatever macOS does
        // or does not do about registering it. Finding it in a folder is the
        // part people get stuck on, so do that for them.
        let reveal = NSButton(title: "Show Me the App", target: self,
                              action: #selector(revealApp))

        let quit = NSButton(title: "Quit", target: self, action: #selector(quit))

        buttons.addArrangedSubview(open)
        buttons.addArrangedSubview(reveal)
        buttons.addArrangedSubview(quit)

        content.addArrangedSubview(heading)
        content.addArrangedSubview(body)
        content.addArrangedSubview(buttons)

        let panel = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 420, height: 240),
                             styleMask: [.titled, .closable],
                             backing: .buffered,
                             defer: false)
        panel.title = "GamepadBridge"
        panel.contentView = content
        panel.isReleasedWhenClosed = false

        // Above System Settings, so the steps stay readable while they are
        // being followed rather than disappearing behind it.
        panel.level = .floating
        panel.center()

        window = panel

        NSApplication.shared.activate(ignoringOtherApps: true)
        panel.makeKeyAndOrderFront(nil)

        // Nobody should have to come back and tell the app they granted it.
        timer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) {
            [weak self] _ in self?.check()
        }
    }

    private func check() {
        guard gpb_permission_granted() != 0 else { return }

        timer?.invalidate()
        timer = nil

        window?.close()
        window = nil

        let alert = NSAlert()
        alert.messageText = "Permission granted"
        alert.informativeText = "GamepadBridge has to restart to use it."
        alert.addButton(withTitle: "Quit and Reopen")
        alert.addButton(withTitle: "Later")

        NSApplication.shared.activate(ignoringOtherApps: true)

        if alert.runModal() == .alertFirstButtonReturn {
            relaunch()
        }
    }

    private func relaunch() {
        let configuration = NSWorkspace.OpenConfiguration()
        configuration.createsNewApplicationInstance = true

        // The replacement starts before this one exits, so it may find the
        // dongle still claimed. That is what the retry loop around opening it
        // is for — it outlasts the old process.
        NSWorkspace.shared.openApplication(at: Bundle.main.bundleURL,
                                           configuration: configuration) { _, _ in
            DispatchQueue.main.async { gpb_request_quit() }
        }
    }

    @objc private func openSettings() {
        guard let url = URL(string: settings) else { return }

        NSWorkspace.shared.open(url)
    }

    @objc private func revealApp() {
        NSWorkspace.shared.activateFileViewerSelecting([Bundle.main.bundleURL])
    }

    @objc private func quit() {
        gpb_request_quit()
    }
}
