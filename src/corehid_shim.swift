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
//  One VirtualPad per controller. C++ holds each as an opaque handle, and
//  output reports (rumble) go back to it through a plain C callback.
//

import Foundation
import CoreHID

// A virtual device defaults to Transport "Virtual", and GameController.framework
// deliberately skips virtual HID devices (Apple DTS, developer forums thread
// 812774) so that synthesised input cannot be looped back into the OS. Claiming
// a physical transport is what lets us test whether that filter is the only
// thing standing between the gamepad and GCController.
private func parseTransport(_ name: String?) -> HIDDeviceTransport? {
    switch name?.lowercased() {
    case "usb":
        return .usb
    case "bluetooth", "bt":
        return .bluetooth
    case "ble", "bluetoothlowenergy":
        return .bluetoothLowEnergy
    case "virtual":
        return .virtual
    case nil, "", "none", "default":
        return nil
    case let other?:
        return .unknown(other)
    }
}

// Called with every output report a game sends to the pad, on whatever thread
// CoreHID delivers it. `id` is 0 when the report came without one.
public typealias OutputReportHandler = @convention(c) (
    UnsafeMutableRawPointer?, UInt8, UnsafePointer<UInt8>?, Int) -> Void

private final class VirtualPad: HIDVirtualDeviceDelegate, @unchecked Sendable {
    private var device: HIDVirtualDevice?
    private var continuation: AsyncStream<Data>.Continuation?

    private let lock = NSLock()
    private var pendingError: String?

    // Guarded by `lock`, and cleared by stop(): once stop() returns, the C++
    // side may be gone, so no report may reach it any more.
    private var onOutput: OutputReportHandler?
    private var context: UnsafeMutableRawPointer?

    init(onOutput: OutputReportHandler?, context: UnsafeMutableRawPointer?) {
        self.onOutput = onOutput
        self.context = context
    }

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
               product: String,
               manufacturer: String,
               transport: HIDDeviceTransport?,
               serial: String?) -> Bool {
        // Two pads of the same model are told apart by these. Without them
        // the second one looks like the first reconnecting.
        let properties = HIDVirtualDevice.Properties(descriptor: descriptor,
                                                     vendorID: vendorID,
                                                     productID: productID,
                                                     transport: transport,
                                                     product: product,
                                                     manufacturer: manufacturer,
                                                     versionNumber: version,
                                                     serialNumber: serial,
                                                     uniqueID: serial)

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
            await dev.activate(
                delegate: self ?? VirtualPad(onOutput: nil, context: nil))

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
        // Taking the lock waits for a report that is being handed over right
        // now; after this, none can start.
        lock.withLock {
            onOutput = nil
            context = nil
        }

        continuation?.finish()
        continuation = nil
        device = nil
    }

    // MARK: - HIDVirtualDeviceDelegate

    func hidVirtualDevice(_ device: HIDVirtualDevice,
                          receivedSetReportRequestOfType type: HIDReportType,
                          id: HIDReportID?,
                          data: Data) async throws {
        guard type == .output else { return }

        // Called with the lock held, so stop() cannot free the C++ side
        // halfway through. The handler only queues work and returns.
        lock.withLock {
            guard let handler = onOutput else { return }

            data.withUnsafeBytes { raw in
                handler(context,
                        id?.rawValue ?? 0,
                        raw.bindMemory(to: UInt8.self).baseAddress,
                        raw.count)
            }
        }
    }

    func hidVirtualDevice(_ device: HIDVirtualDevice,
                          receivedGetReportRequestOfType type: HIDReportType,
                          id: HIDReportID?,
                          maxSize: Int) async throws -> Data {
        return Data()
    }
}

// MARK: - C ABI consumed by output_corehid.cpp
//
// A pad is an opaque handle owned by the caller. gpb_corehid_create always
// returns one — on failure too, so the reason can still be read — and every
// handle must be released with gpb_corehid_destroy.

private func pad(_ handle: UnsafeMutableRawPointer) -> VirtualPad {
    Unmanaged<VirtualPad>.fromOpaque(handle).takeUnretainedValue()
}

@_cdecl("gpb_corehid_create")
public func gpb_corehid_create(_ descriptor: UnsafePointer<UInt8>,
                               _ descriptorLength: Int,
                               _ vendorID: UInt32,
                               _ productID: UInt32,
                               _ version: UInt64,
                               _ product: UnsafePointer<CChar>,
                               _ manufacturer: UnsafePointer<CChar>,
                               _ transport: UnsafePointer<CChar>?,
                               _ serial: UnsafePointer<CChar>?,
                               _ onOutput: OutputReportHandler?,
                               _ context: UnsafeMutableRawPointer?,
                               _ handle: UnsafeMutablePointer<UnsafeMutableRawPointer?>)
    -> Int32 {
    let data = Data(bytes: descriptor, count: descriptorLength)
    let name = String(cString: product)
    let vendor = String(cString: manufacturer)
    let link = transport.map { String(cString: $0) }
    let number = serial.map { String(cString: $0) }.flatMap { $0.isEmpty ? nil : $0 }

    let instance = VirtualPad(onOutput: onOutput, context: context)

    handle.pointee = Unmanaged.passRetained(instance).toOpaque()

    let started = instance.start(descriptor: data,
                                 vendorID: vendorID,
                                 productID: productID,
                                 version: version,
                                 product: name,
                                 manufacturer: vendor,
                                 transport: parseTransport(link),
                                 serial: number)

    return started ? 0 : -1
}

@_cdecl("gpb_corehid_send")
public func gpb_corehid_send(_ handle: UnsafeMutableRawPointer,
                             _ bytes: UnsafePointer<UInt8>,
                             _ length: Int) {
    pad(handle).send(Data(bytes: bytes, count: length))
}

// Returns 1 and fills `buffer` when an error is pending, 0 otherwise.
@_cdecl("gpb_corehid_take_error")
public func gpb_corehid_take_error(_ handle: UnsafeMutableRawPointer,
                                   _ buffer: UnsafeMutablePointer<CChar>,
                                   _ capacity: Int) -> Int32 {
    guard let message = pad(handle).takeError() else { return 0 }

    _ = message.withCString { source in
        strlcpy(buffer, source, capacity)
    }

    return 1
}

@_cdecl("gpb_corehid_destroy")
public func gpb_corehid_destroy(_ handle: UnsafeMutableRawPointer) {
    let instance = Unmanaged<VirtualPad>.fromOpaque(handle).takeRetainedValue()

    instance.stop()
}
