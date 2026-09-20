//
//  permission_panel.swift — guidance that stays put
//  Copyright (C) 2026 GamepadBridge contributors
//  SPDX-License-Identifier: GPL-2.0-or-later
//
//  An alert was the wrong shape for this. Pressing its button dismissed it,
//  so the instructions disappeared at the exact moment System Settings opened
//  and they were needed. This is an ordinary window that stays on screen,
//  updates itself as each permission is granted, and says so when both are.
//

import AppKit

final class PermissionPanel: NSObject {
    private var window: NSWindow?
    private var timer: Timer?
    private let body = NSTextField(wrappingLabelWithString: "")

    var isVisible: Bool { window != nil }

    func show() {
        guard window == nil else { return }

        let content = NSStackView()
        content.orientation = .vertical
        content.alignment = .leading
        content.spacing = 14
        content.edgeInsets = NSEdgeInsets(top: 20, left: 20, bottom: 20, right: 20)

        let heading = NSTextField(labelWithString: "GamepadBridge needs permission")
        heading.font = .boldSystemFont(ofSize: 15)

        body.preferredMaxLayoutWidth = 420
        body.stringValue = messageText()

        let buttons = NSStackView()
        buttons.orientation = .horizontal
        buttons.spacing = 10

        let input = NSButton(title: "Input Monitoring", target: self,
                             action: #selector(openInputMonitoring))
        input.keyEquivalent = "\r"

        let access = NSButton(title: "Accessibility", target: self,
                              action: #selector(openAccessibility))

        // Dragging the app onto a list always works, whatever macOS does or
        // does not do about registering it. Finding the app in a folder is
        // where people get stuck, so do that part for them.
        let reveal = NSButton(title: "Show Me the App", target: self,
                              action: #selector(revealApp))

        let quit = NSButton(title: "Quit", target: self, action: #selector(quit))

        for button in [input, access, reveal, quit] {
            buttons.addArrangedSubview(button)
        }

        content.addArrangedSubview(heading)
        content.addArrangedSubview(body)
        content.addArrangedSubview(buttons)

        let panel = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 470, height: 320),
                             styleMask: [.titled, .closable],
                             backing: .buffered,
                             defer: false)
        panel.title = "GamepadBridge"
        panel.contentView = content
        panel.isReleasedWhenClosed = false

        // Above System Settings, so the steps stay readable while they are
        // being followed instead of disappearing behind it.
        panel.level = .floating
        panel.center()

        window = panel

        NSApplication.shared.activate(ignoringOtherApps: true)
        panel.makeKeyAndOrderFront(nil)

        // Nobody should have to come back and tell the app what they just did.
        timer = Timer.scheduledTimer(withTimeInterval: 1.5, repeats: true) {
            [weak self] _ in self?.check()
        }
    }

    private func messageText() -> String {
        gpb_permission_message().map { String(cString: $0) } ?? ""
    }

    private func check() {
        guard gpb_permission_granted() != 0 else {
            // Tick off whichever has been granted so far, so progress is
            // visible rather than all-or-nothing.
            body.stringValue = messageText()
            return
        }

        timer?.invalidate()
        timer = nil

        window?.close()
        window = nil

        gpb_set_needs_permission(0)

        let alert = NSAlert()
        alert.messageText = "Permissions granted"
        alert.informativeText = "GamepadBridge has to restart to use them."
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

    private func open(_ url: UnsafePointer<CChar>?) {
        guard let text = url.map({ String(cString: $0) }),
              let target = URL(string: text) else { return }

        NSWorkspace.shared.open(target)
    }

    @objc private func openInputMonitoring() {
        open(gpb_permission_input_url())
    }

    @objc private func openAccessibility() {
        open(gpb_permission_accessibility_url())
    }

    @objc private func revealApp() {
        NSWorkspace.shared.activateFileViewerSelecting([Bundle.main.bundleURL])
    }

    @objc private func quit() {
        gpb_request_quit()
    }
}
