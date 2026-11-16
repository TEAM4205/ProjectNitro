///////////////////////////////////////////////////////////////////
//   _   _ _ _                                //         __      //
//  | \ | (_) |_ _ __ ___   __ _  ___ _ __    //        / /_     //
//  |  \| | | __| '__/ _ \ / _` |/ _ \ '_  \  //       / __ \    //
//  | |\  | | |_| | | (_) | (_| |  __/ | | |  //    _ / / / /    //
//  |_| \_|_|\__|_|  \___/ \__, |\___|_| |_|  //   (_)_/ /_/     //
//                         |___/              //                 //
///////////////////////////////////////////////////////////////////
// Header de:      Encapsulado de Storage.cpp                    //
// Funcion:        Manejo de archivos                            //
// Declaraciones:  Script entero                                 //
///////////////////////////////////////////////////////////////////

#pragma once

// ═══════════════════════════════════════════════════════════════════════════════
//  Storage.hpp  –  File I/O utility library for VEX V5 + PROS (C++17)
// ───────────────────────────────────────────────────────────────────────────────
//      All paths must start with "/usd/" (SD card required in VEX Brain).
//      Example: "/usd/logs/app.log"
//
//  Timestamp format: [MMM:SS.cc] = minutes:seconds.centiseconds since program
//  start (pros::millis() — VEX Brain has no RTC).
//
//  NOTE: Directory *listing* (sdlistdir / sdlistfiles / sdlistdirs) is not
//        available because arm-none-eabi newlib does not implement dirent.h.
//        All other read / write / filesystem operations work normally.
//
//  WRITE      sdopen(path)                   Set default file path
//             sdwrite(text, file)            Append raw text
//             sdlog(text, file)              Append "[MMM:SS.cc] text\n"
//
//  READ       sdread(file)                   Read entire file as string
//             sdreadbylines(from, n, file)   Read n lines from line index 'from'
//             sdreadbychars(from, n, file)   Read n chars from offset 'from'
//             sdbuffer(file)                 Read entire file as byte vector
//             sdbufferbybytes(start, n, file)Read n bytes from offset 'start'
//
//  RUN ID     sdgetrunid(file)               Read current run ID (hex uint32)
//             sdnextrunid(file)              Increment, save and return new ID
//             sdresetrunid(file)             Reset run ID back to 0
//
//  FILESYSTEM sdexists(path)                 Check if path exists
//             sdisfile(path)                 Check if path is a regular file
//             sdisdir(path)                  Check if path is a directory
//             sdsize(path)                   File size in bytes (-1 if error)
//             sdmkdir(path)                  Create directory (and parents)
//             sdremove(path)                 Delete a file
//             sdremovedir(path)              Delete an empty directory
//             sdrename(from, to)             Rename / move a file
//             sdcopy(from, to, overwrite)    Copy a file
//             sdmove(from, to)               Move a file (rename or copy+delete)
//             sdcardok()                     True if SD card is inserted
// ═══════════════════════════════════════════════════════════════════════════════

#include "main.h"   // pros::millis(), pros::usd::is_installed()

#include <cstdint>
#include <cstdio>        // FILE*, fopen, fclose, fread, fwrite, fseek, ftell, rename
#include <stdexcept>
#include <string>
#include <sys/stat.h>    // stat, mkdir, S_ISREG, S_ISDIR
#include <unistd.h>      // rmdir, unlink
#include <vector>

namespace Storage {

// ─────────────────────────────────────────────────────────────────────────────
//  Internal helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace detail {

    inline std::string& defaultPath() {
        static std::string p;
        return p;
    }

    inline const std::string& resolve(const std::string& file) {
        const std::string& chosen = file.empty() ? defaultPath() : file;
        if (chosen.empty())
            throw std::runtime_error(
                "Storage: no target file. Call sdopen() or pass a path.");
        return chosen;
    }

    // Timestamp based on pros::millis() — no RTC on VEX Brain.
    // Format: [MMM:SS.cc]  (minutes : seconds . centiseconds)
    inline std::string timestamp() {
        uint32_t ms  = pros::millis();
        uint32_t cs  = (ms / 10)    % 100;
        uint32_t sec = (ms / 1000)  % 60;
        uint32_t min =  ms / 60000;
        char buf[16];
        snprintf(buf, sizeof(buf), "[%03u:%02u.%02u]", min, sec, cs);
        return std::string(buf);
    }

    // ── RAII wrapper around FILE* ─────────────────────────────────────────────
    struct File {
        FILE* fp = nullptr;
        File(const std::string& path, const char* mode) {
            fp = fopen(path.c_str(), mode);
        }
        ~File() { if (fp) fclose(fp); }
        bool     ok()  const { return fp != nullptr; }
        operator FILE*()     { return fp; }
        File(const File&)            = delete;
        File& operator=(const File&) = delete;
    };

    // ── mkdir -p (no dirent needed) ───────────────────────────────────────────
    inline void mkdirp(const std::string& path) {
        for (std::size_t i = 1; i < path.size(); ++i)
            if (path[i] == '/')
                ::mkdir(path.substr(0, i).c_str(), 0777); // ignore — may exist
        ::mkdir(path.c_str(), 0777);
    }

} // namespace detail


// ═══════════════════════════════════════════════════════════════════════════════
//  WRITE
// ═══════════════════════════════════════════════════════════════════════════════

// Set the default target file for all subsequent calls.
inline void sdopen(const std::string& path) {
    if (path.empty())
        throw std::invalid_argument("sdopen: path cannot be empty.");
    detail::defaultPath() = path;
}

// Append raw text to 'file' (or the default if omitted).
inline void sdwrite(const std::string& text,
                    const std::string& file = "") {
    const std::string& path = detail::resolve(file);
    detail::File f(path, "a");
    if (!f.ok())
        throw std::runtime_error("sdwrite: cannot open '" + path + "'");
    fwrite(text.c_str(), 1, text.size(), f);
}

// Append "[MMM:SS.cc] text\n" to 'file' (or the default if omitted).
inline void sdlog(const std::string& text,
                  const std::string& file = "") {
    sdwrite(detail::timestamp() + ' ' + text + '\n', file);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  READ
// ═══════════════════════════════════════════════════════════════════════════════

// Read the entire file and return its content as a string.
inline std::string sdread(const std::string& file = "") {
    const std::string& path = detail::resolve(file);
    detail::File f(path, "r");
    if (!f.ok())
        throw std::runtime_error("sdread: cannot open '" + path + "'");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string result(static_cast<std::size_t>(size), '\0');
    fread(result.data(), 1, static_cast<std::size_t>(size), f);
    return result;
}

// Read 'lines' lines starting at line index 'from' (0-based).
// Pass lines = -1 to read until end of file.
inline std::vector<std::string> sdreadbylines(std::size_t    from  = 0,
                                              std::ptrdiff_t lines = -1,
                                              const std::string& file = "") {
    const std::string& path = detail::resolve(file);
    detail::File f(path, "r");
    if (!f.ok())
        throw std::runtime_error("sdreadbylines: cannot open '" + path + "'");

    std::vector<std::string> result;
    char buf[512];
    std::size_t    current   = 0;
    std::ptrdiff_t collected = 0;

    while (fgets(buf, sizeof(buf), f)) {
        if (current >= from) {
            if (lines != -1 && collected >= lines) break;
            std::string line(buf);
            if (!line.empty() && line.back() == '\n') line.pop_back();
            if (!line.empty() && line.back() == '\r') line.pop_back();
            result.push_back(std::move(line));
            ++collected;
        }
        ++current;
    }
    return result;
}

// Read 'chars' characters starting at byte offset 'from'.
// Pass chars = -1 to read until end of file.
inline std::string sdreadbychars(std::size_t    from  = 0,
                                 std::ptrdiff_t chars = -1,
                                 const std::string& file = "") {
    const std::string& path = detail::resolve(file);
    detail::File f(path, "r");
    if (!f.ok())
        throw std::runtime_error("sdreadbychars: cannot open '" + path + "'");

    if (fseek(f, static_cast<long>(from), SEEK_SET) != 0)
        throw std::runtime_error("sdreadbychars: seek failed.");

    if (chars == -1) {
        fseek(f, 0, SEEK_END);
        long end = ftell(f);
        fseek(f, static_cast<long>(from), SEEK_SET);
        chars = end - static_cast<long>(from);
        if (chars <= 0) return {};
    }

    std::string result(static_cast<std::size_t>(chars), '\0');
    std::size_t n = fread(result.data(), 1, static_cast<std::size_t>(chars), f);
    result.resize(n);
    return result;
}

// Read the entire file into a raw byte buffer.
inline std::vector<std::uint8_t> sdbuffer(const std::string& file = "") {
    const std::string& path = detail::resolve(file);
    detail::File f(path, "rb");
    if (!f.ok())
        throw std::runtime_error("sdbuffer: cannot open '" + path + "'");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
    fread(buf.data(), 1, static_cast<std::size_t>(size), f);
    return buf;
}

// Read 'count' bytes starting at byte offset 'start'.
// Pass count = -1 to read until end of file.
inline std::vector<std::uint8_t> sdbufferbybytes(std::size_t    start = 0,
                                                 std::ptrdiff_t count = -1,
                                                 const std::string& file = "") {
    const std::string& path = detail::resolve(file);
    detail::File f(path, "rb");
    if (!f.ok())
        throw std::runtime_error("sdbufferbybytes: cannot open '" + path + "'");

    if (fseek(f, static_cast<long>(start), SEEK_SET) != 0)
        throw std::runtime_error("sdbufferbybytes: seek failed.");

    if (count == -1) {
        fseek(f, 0, SEEK_END);
        long end = ftell(f);
        fseek(f, static_cast<long>(start), SEEK_SET);
        count = end - static_cast<long>(start);
        if (count <= 0) return {};
    }

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(count));
    std::size_t n = fread(buf.data(), 1, static_cast<std::size_t>(count), f);
    buf.resize(n);
    return buf;
}


// ═══════════════════════════════════════════════════════════════════════════════
//  FILESYSTEM UTILITIES
// ═══════════════════════════════════════════════════════════════════════════════

// True if SD card is physically inserted in the VEX Brain.
inline bool sdcardok() {
    return pros::usd::is_installed();
}

// Check if a path exists (file or directory).
inline bool sdexists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

// Check if a path is a regular file.
inline bool sdisfile(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Check if a path is a directory.
inline bool sdisdir(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Return file size in bytes. Returns -1 if path doesn't exist or is a directory.
inline std::intmax_t sdsize(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return static_cast<std::intmax_t>(st.st_size);
}

// Create a directory and all missing parent directories (like mkdir -p).
inline void sdmkdir(const std::string& path) {
    detail::mkdirp(path);
    if (!sdisdir(path))
        throw std::runtime_error("sdmkdir: failed to create '" + path + "'");
}

// Delete a single file.
inline void sdremove(const std::string& path) {
    if (::unlink(path.c_str()) != 0)
        throw std::runtime_error("sdremove: cannot delete '" + path + "'");
}

// Delete an empty directory.
inline void sdremovedir(const std::string& path) {
    if (::rmdir(path.c_str()) != 0)
        throw std::runtime_error(
            "sdremovedir: cannot remove '" + path + "' (not empty or missing?)");
}

// Rename a file or directory.
inline void sdrename(const std::string& from, const std::string& to) {
    if (::rename(from.c_str(), to.c_str()) != 0)
        throw std::runtime_error(
            "sdrename: '" + from + "' -> '" + to + "' failed.");
}

// Copy a file. Set overwrite = true to replace an existing destination.
inline void sdcopy(const std::string& from,
                   const std::string& to,
                   bool overwrite = false) {
    if (!overwrite && sdexists(to))
        throw std::runtime_error("sdcopy: '" + to + "' already exists.");

    detail::File src(from, "rb");
    if (!src.ok())
        throw std::runtime_error("sdcopy: cannot open source '" + from + "'");
    detail::File dst(to, "wb");
    if (!dst.ok())
        throw std::runtime_error("sdcopy: cannot open dest '" + to + "'");

    std::uint8_t buf[512];
    std::size_t  n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
        fwrite(buf, 1, n, dst);
}

// Move a file (atomic rename when possible, otherwise copy + delete).
inline void sdmove(const std::string& from, const std::string& to) {
    if (::rename(from.c_str(), to.c_str()) == 0) return;
    sdcopy(from, to, true);
    sdremove(from);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  RUN ID
// ───────────────────────────────────────────────────────────────────────────────
//  Persists a hex run counter in a dot-file on the SD card.
//  Dot-files are the closest thing to "hidden" available on FAT32 via PROS
//  (the FAT hidden attribute requires ioctls that arm-none-eabi does not expose).
//
//  File format: plain ASCII hex, no prefix, uppercase, e.g.  "1A3F\n"
//  Wraps around at 0xFFFFFFFF → 0x00000000.
//
//  Default file: /usd/.runid
// ═══════════════════════════════════════════════════════════════════════════════

namespace detail {
    inline const std::string& runidDefaultPath() {
        static std::string p = "/usd/.runid";
        return p;
    }
} // namespace detail (reopened)

// Read the current run ID from 'file'.
// Creates the file with value 0x00000000 if it does not exist.
inline std::uint32_t sdgetrunid(const std::string& file = "") {
    const std::string& path = file.empty() ? detail::runidDefaultPath() : file;

    // Create with 0 if missing. No sdexists() aca: stat() falla siempre en la SD de PROS,
    // y el contador se reiniciaba a 0 en cada llamada (todas las grabaciones salian _00001).
    detail::File f(path, "r");
    if (!f.ok()) {
        detail::File c(path, "w");
        if (!c.ok())
            throw std::runtime_error("sdgetrunid: cannot create '" + path + "'");
        fputs("00000000\n", c);
        return 0u;
    }

    char buf[16]{};
    fgets(buf, sizeof(buf), f);

    // Strip whitespace / newline
    std::string raw(buf);
    while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r' ||
                             raw.back() == ' '))
        raw.pop_back();

    if (raw.empty()) return 0u;

    // Parse hex (no "0x" prefix expected, but tolerate it)
    return static_cast<std::uint32_t>(std::stoul(raw, nullptr, 16));
}

// Increment the run ID by 1, save it and return the NEW value.
// Wraps around from 0xFFFFFFFF back to 0x00000000.
inline std::uint32_t sdnextrunid(const std::string& file = "") {
    const std::string& path = file.empty() ? detail::runidDefaultPath() : file;

    std::uint32_t next = sdgetrunid(path) + 1u; // wraps naturally on overflow

    detail::File f(path, "w");
    if (!f.ok())
        throw std::runtime_error("sdnextrunid: cannot write '" + path + "'");

    char buf[16];
    snprintf(buf, sizeof(buf), "%08X\n", next);
    fputs(buf, f);

    return next;
}

// Reset the run ID to 0x00000000.
inline void sdresetrunid(const std::string& file = "") {
    const std::string& path = file.empty() ? detail::runidDefaultPath() : file;

    detail::File f(path, "w");
    if (!f.ok())
        throw std::runtime_error("sdresetrunid: cannot write '" + path + "'");
    fputs("00000000\n", f);
}

} // namespace Storage