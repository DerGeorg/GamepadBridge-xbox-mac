//
//  main.swift — checks the version comparison behaves
//  SPDX-License-Identifier: GPL-2.0-or-later
//
//  Named main.swift because Swift only allows statements at the top level in
//  a file with that name.
//
//  swiftc -o /tmp/update-check-test \
//      src/update_check.swift test/update_check/main.swift && /tmp/update-check-test
//

import Foundation

var failures = 0

func expect(_ actual: Bool, _ expected: Bool, _ what: String) {
    if actual == expected {
        print("  ok    \(what)")
    } else {
        print("  FAIL  \(what) — expected \(expected), got \(actual)")
        failures += 1
    }
}

print("isNewer:")
expect(isNewer("1.0.1", than: "1.0.0"), true,  "1.0.1 > 1.0.0")
expect(isNewer("1.0.0", than: "1.0.0"), false, "1.0.0 is not newer than itself")
expect(isNewer("1.0.0", than: "1.0.1"), false, "1.0.0 < 1.0.1")
expect(isNewer("1.10.0", than: "1.9.0"), true, "1.10.0 > 1.9.0 (not a string compare)")
expect(isNewer("1.9.0", than: "1.10.0"), false, "1.9.0 < 1.10.0")
expect(isNewer("2.0", than: "1.9.9"),   true,  "2.0 > 1.9.9")
expect(isNewer("1.0", than: "1.0.0"),   false, "1.0 equals 1.0.0")
expect(isNewer("1.0.1", than: "1.0"),   true,  "1.0.1 > 1.0")

print("latestVersion:")
let json = """
[{"tag_name":"v1.0.0"},{"tag_name":"v1.10.0"},{"tag_name":"v1.9.0"}]
""".data(using: .utf8)!
let highest = latestVersion(from: json)
expect(highest == "1.10.0", true, "picks the highest, not the first: got \(highest ?? "nil")")

expect(latestVersion(from: Data("not json".utf8)) == nil, true, "garbage yields nil")
expect(latestVersion(from: Data("[]".utf8)) == nil, true, "no releases yields nil")

print(failures == 0 ? "\nall passed" : "\n\(failures) failed")
exit(failures == 0 ? 0 : 1)
