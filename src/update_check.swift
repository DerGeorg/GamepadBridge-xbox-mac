//
//  update_check.swift — is there a newer GamepadBridge?
//  Copyright (C) 2026 GamepadBridge contributors
//  SPDX-License-Identifier: GPL-2.0-or-later
//
//  Kept apart from the menu bar because it is pure logic with no UI and no
//  network of its own, which is the only way the version comparison can be
//  tested — and comparing versions is exactly the kind of thing that looks
//  obvious and is quietly wrong.
//
//  Verify with:
//    swiftc -o /tmp/t src/update_check.swift test/update_check/main.swift && /tmp/t
//

import Foundation

/*
 * Homebrew handles updates on its own through the cask's livecheck block, but
 * anyone who downloaded the disk image has no way of hearing about a new
 * version otherwise.
 *
 * The check only ever runs when the menu item is clicked. Checking on a timer
 * would mean the app quietly contacts a server on its own schedule, and that
 * is not something to decide on a user's behalf without asking.
 */
let releasesAPI = URL(string:
    "https://gitlab.dergeorg.at/api/v4/projects/mac%2Fgamepadbridge/releases")!

let releasesPage = URL(string:
    "https://gitlab.dergeorg.at/mac/gamepadbridge/-/releases")!

func currentVersion() -> String {
    Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "0"
}

// Compare as numbers per component: "1.10.0" is newer than "1.9.0", which a
// plain string comparison gets backwards.
func isNewer(_ candidate: String, than current: String) -> Bool {
    let new = candidate.split(separator: ".").map { Int($0) ?? 0 }
    let old = current.split(separator: ".").map { Int($0) ?? 0 }

    for index in 0 ..< max(new.count, old.count) {
        let a = index < new.count ? new[index] : 0
        let b = index < old.count ? old[index] : 0

        if a != b {
            return a > b
        }
    }

    return false
}

func latestVersion(from data: Data) -> String? {
    guard let releases = try? JSONSerialization.jsonObject(with: data)
            as? [[String: Any]] else {
        return nil
    }

    // The API returns newest first, but a release without a tag would break
    // that assumption quietly, so pick the highest rather than the first.
    return releases
        .compactMap { $0["tag_name"] as? String }
        .map { $0.hasPrefix("v") ? String($0.dropFirst()) : $0 }
        .max { isNewer($1, than: $0) }
}
