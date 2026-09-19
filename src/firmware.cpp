/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "firmware.h"
#include "utils/log.h"

#include <CommonCrypto/CommonDigest.h>

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <spawn.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

#ifndef GAMEPADBRIDGE_FIRMWARE
#define GAMEPADBRIDGE_FIRMWARE "/usr/local/share/gamepadbridge/xow_dongle.bin"
#endif

namespace
{
    // Microsoft's driver package for the Xbox Wireless Adapter — the same one
    // xow uses on Linux.
    const char *kUrl =
        "http://download.windowsupdate.com/c/msdownload/update/driver/drvs/"
        "2017/07/1cd6a87c-623f-4407-a52d-c31be49e925c_"
        "e19f60808bdcbfbd3c3df6be3e71ffc52e43261e.cab";

    const char *kMember = "FW_ACC_00U.bin";

    const char *kSha256 =
        "48084d9fa53b9bb04358f3bb127b7495dc8f7bb0b3ca1437bd24ef2b6eabdf66";

    /*
     * Runs a system tool with an argv array rather than a command string, so
     * a path containing spaces or quotes cannot turn into shell syntax.
     */
    int run(const char *tool,
            const std::vector<const char *> &args,
            const char *stdoutPath)
    {
        std::vector<char *> argv;

        for (const char *argument : args)
        {
            argv.push_back(const_cast<char *>(argument));
        }

        argv.push_back(nullptr);

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);

        if (stdoutPath)
        {
            posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, stdoutPath,
                                             O_WRONLY | O_CREAT | O_TRUNC, 0644);
        }

        pid_t pid = 0;
        int error = posix_spawn(&pid, tool, &actions, nullptr, argv.data(),
                                environ);

        posix_spawn_file_actions_destroy(&actions);

        if (error)
        {
            Log::error("Could not run %s: %s", tool, strerror(error));

            return -1;
        }

        int status = 0;

        if (waitpid(pid, &status, 0) < 0)
        {
            return -1;
        }

        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    std::string sha256(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary);

        if (!file)
        {
            return std::string();
        }

        CC_SHA256_CTX context;
        CC_SHA256_Init(&context);

        std::vector<char> chunk(64 * 1024);

        while (file.read(chunk.data(), static_cast<long>(chunk.size()))
               || file.gcount() > 0)
        {
            CC_SHA256_Update(&context, chunk.data(),
                             static_cast<CC_LONG>(file.gcount()));
        }

        unsigned char digest[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256_Final(digest, &context);

        std::ostringstream out;

        for (unsigned char byte : digest)
        {
            out << std::hex << (byte >> 4) << (byte & 0x0f);
        }

        return out.str();
    }

    std::string parentDirectory(const std::string &path)
    {
        const size_t slash = path.find_last_of('/');

        return slash == std::string::npos ? std::string(".")
                                          : path.substr(0, slash);
    }

    std::string userPath()
    {
        const char *home = std::getenv("HOME");

        if (!home || !*home)
        {
            return GAMEPADBRIDGE_FIRMWARE;
        }

        return std::string(home)
            + "/Library/Application Support/GamepadBridge/xow_dongle.bin";
    }
}

bool Firmware::isPresent(const std::string &path)
{
    struct stat info;

    return stat(path.c_str(), &info) == 0 && info.st_size > 0;
}

std::string Firmware::resolvePath()
{
    const char *override = std::getenv("XOW_FIRMWARE");

    if (override && *override)
    {
        return override;
    }

    const std::string user = userPath();

    if (isPresent(user))
    {
        return user;
    }

    // Only fall back to the system-wide location if something is actually
    // installed there; otherwise point at where we would download to.
    if (isPresent(GAMEPADBRIDGE_FIRMWARE))
    {
        return GAMEPADBRIDGE_FIRMWARE;
    }

    return user;
}

std::string Firmware::notice()
{
    return "The Xbox Wireless Adapter needs a firmware file that belongs to "
           "Microsoft and cannot be bundled with this app.\n\n"
           "GamepadBridge can download it directly from Microsoft's servers "
           "(about 200 KB) and verify it against a known checksum. It is "
           "covered by the Microsoft Terms of Use:\n"
           "https://www.microsoft.com/en-us/legal/terms-of-use";
}

bool Firmware::download(const std::string &destination)
{
    const std::string directory = parentDirectory(destination);

    // "Application Support" already exists; this creates our folder inside it.
    mkdir(directory.c_str(), 0755);

    const char *tmp = std::getenv("TMPDIR");
    const std::string archive =
        std::string(tmp && *tmp ? tmp : "/tmp") + "/gamepadbridge-firmware.cab";

    Log::info("Downloading firmware from Microsoft...");

    if (run("/usr/bin/curl",
            { "curl", "-fsSL", "--retry", "2", "-o", archive.c_str(), kUrl },
            nullptr) != 0)
    {
        Log::error("Firmware download failed - check your internet connection");

        return false;
    }

    // bsdtar ships with macOS and reads CAB archives, so unlike xow's script
    // this needs no cabextract and therefore no Homebrew.
    const std::string partial = destination + ".part";

    if (run("/usr/bin/bsdtar", { "bsdtar", "-xOf", archive.c_str(), kMember },
            partial.c_str()) != 0)
    {
        Log::error("Could not extract %s from the driver package", kMember);

        unlink(archive.c_str());
        unlink(partial.c_str());

        return false;
    }

    unlink(archive.c_str());

    const std::string digest = sha256(partial);

    if (digest != kSha256)
    {
        Log::error("Firmware checksum mismatch - refusing to use it");
        Log::error("  expected %s", kSha256);
        Log::error("  got      %s", digest.c_str());

        unlink(partial.c_str());

        return false;
    }

    if (rename(partial.c_str(), destination.c_str()) != 0)
    {
        Log::error("Could not write %s: %s", destination.c_str(),
                   strerror(errno));

        unlink(partial.c_str());

        return false;
    }

    Log::info("Firmware installed at %s", destination.c_str());

    return true;
}
