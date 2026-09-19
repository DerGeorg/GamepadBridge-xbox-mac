//
//  corehid_shim.swift — CoreHID bridge for GamepadBridge
//  Copyright (C) 2026 GamepadBridge contributors
//  SPDX-License-Identifier: GPL-2.0-or-later
//
//  CoreHID's HIDVirtualDevice is a Swift-only actor, so this shim exposes a
//  small C ABI that src/output_corehid.cpp drives. It owns the device, funnels
//  input reports through an AsyncStream (preserving order without spawning a
//  Task per report — they arrive at ~125 Hz) and surfaces errors back to C++,
//  which the legacy IOHIDUserDevice SPI never did: it just returned NULL.
//

import Foundation
import CoreHID

private final class VirtualPad: HIDVirtualDeviceDelegate, @unchecked Sendable {
    private var device: HIDVirtualDevice?
    private var continuation: AsyncStream<Data>.Continuation?

    private let lock = NSLock()
    private var pendingError: String?

    // MARK: - Error plumbing

    private func record(_ message: String) {
        lock.lock()
        // Keep the first error; later ones are usually follow-on noise.
        if pendingError == nil { pendingError = message }
        lock.unlock()
    }

    func takeError() -> String? {
        lock.lock()
        defer { pendingError = nil; lock.unlock() }
        return pendingError
    }

    // MARK: - Lifecycle

    func start(descriptor: Data,
               vendorID: UInt32,
               productID: UInt32,
               version: UInt64,
               product: String) -> Bool {
        let properties = HIDVirtualDevice.Properties(descriptor: descriptor,
                                                     vendorID: vendorID,
                                                     productID: productID,
                                                     product: product,
                                                     versionNumber: version)

        // Failable, unlike the old IOHIDUserDevice SPI: if the system refuses
        // the device we find out right here instead of getting a silent NULL.
        guard let dev = HIDVirtualDevice(properties: properties) else {
            record("HIDVirtualDevice(properties:) returned nil - the system "
                   + "refused to create the virtual device")
            return false
        }

        device = dev

        let (stream, cont) = AsyncStream<Data>.makeStream(
            bufferingPolicy: .bufferingNewest(8))
        continuation = cont

        Task { [weak self] in
            await dev.activate(delegate: self ?? VirtualPad())

            let clock = SuspendingClock()

            for await data in stream {
                do {
                    try await dev.dispatchInputReport(data: data,
                                                      timestamp: clock.now)
                } catch {
                    self?.record("dispatchInputReport failed: \(error)")
                }
            }
        }

        return true
    }

    func send(_ data: Data) {
        continuation?.yield(data)
    }

    func stop() {
        continuation?.finish()
        continuation = nil
        device = nil
    }

    // MARK: - HIDVirtualDeviceDelegate
    // Output reports (rumble) are not wired up yet; acknowledge and ignore.

    func hidVirtualDevice(_ device: HIDVirtualDevice,
                          receivedSetReportRequestOfType type: HIDReportType,
                          id: HIDReportID?,
                          data: Data) async throws {
    }

    func hidVirtualDevice(_ device: HIDVirtualDevice,
                          receivedGetReportRequestOfType type: HIDReportType,
                          id: HIDReportID?,
                          maxSize: Int) async throws -> Data {
        return Data()
    }
}

nonisolated(unsafe) private var pad: VirtualPad?

// MARK: - C ABI consumed by output_corehid.cpp

@_cdecl("gpb_corehid_create")
public func gpb_corehid_create(_ descriptor: UnsafePointer<UInt8>,
                               _ descriptorLength: Int,
                               _ vendorID: UInt32,
                               _ productID: UInt32,
                               _ version: UInt64,
                               _ product: UnsafePointer<CChar>) -> Int32 {
    let data = Data(bytes: descriptor, count: descriptorLength)
    let name = String(cString: product)

    let instance = VirtualPad()

    guard instance.start(descriptor: data,
                         vendorID: vendorID,
                         productID: productID,
                         version: version,
                         product: name) else {
        pad = instance   // keep it so the C side can read the error message
        return -1
    }

    pad = instance

    return 0
}

@_cdecl("gpb_corehid_send")
public func gpb_corehid_send(_ bytes: UnsafePointer<UInt8>, _ length: Int) {
    pad?.send(Data(bytes: bytes, count: length))
}

// Returns 1 and fills `buffer` when an error is pending, 0 otherwise.
@_cdecl("gpb_corehid_take_error")
public func gpb_corehid_take_error(_ buffer: UnsafeMutablePointer<CChar>,
                                   _ capacity: Int) -> Int32 {
    guard let message = pad?.takeError() else { return 0 }

    message.withCString { source in
        strlcpy(buffer, source, capacity)
    }

    return 1
}

@_cdecl("gpb_corehid_destroy")
public func gpb_corehid_destroy() {
    pad?.stop()
    pad = nil
}
