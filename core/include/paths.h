// paths.h -- small filesystem helpers.
//
// Thin wrappers over <filesystem> that swallow the error_code overloads, so
// call sites read like the Delphi originals (FileExists, ForceDirectories) and
// a missing file is never an exception.

#pragma once

#include <string>
#include <vector>

namespace swos {

// Directory holding the running executable.
//
// On a macOS .app this is Contents/Resources, because that is what
// SDL_GetBasePath reports for a bundle. It is an install location, not a place
// to write -- use user_dir() for anything writable.
std::string executable_dir();

// The per-user SWOS folder, shared with SWOS-United so the launcher, the DLC
// manager and the game all agree on where a user's files live. Mirrors
// swos_io.c in that project:
//
//   Windows  %USERPROFILE%\Documents\SWOSUnited
//   macOS    ~/Library/Application Support/SWOSUnited
//   Linux    ~/.swosunited
// (SWOS_USER_DIR / SWOS_USER_DOT_DIR, CMake options: OpenSWOS's own is SWOS / .swos)
//
// SWOS-United keeps SWOS.ini and SWOS.log directly in here (career saves go in
// a save/ subfolder, which this port has no use for); the launcher and the DLC
// manager add ini/, dlc/ and temp/ alongside them.
//
// $HOME (or %USERPROFILE%) is tried first because that is what the docs say and
// what a terminal launch uses, but a .app started from Finder gets a trimmed
// environment with HOME unset -- SWOS-United hit exactly that, and the symptom
// was baffling: no settings file was ever created and one placed in the
// documented location was never read. SDL_GetPrefPath asks the OS rather than
// the environment, so it is used as the fallback.
//
// The directory is created if missing. Returns an empty string only if it
// cannot be determined at all, in which case callers should fall back to the
// executable directory.
std::string user_dir();

// True when this process is running from a macOS .app bundle. Always false
// elsewhere.
bool running_in_bundle();

std::string path_join(const std::string &a, const std::string &b);
std::string parent_path(const std::string &p);

bool file_exists(const std::string &p);
bool directory_exists(const std::string &p);
bool ensure_directory(const std::string &p);
bool rename_file(const std::string &from, const std::string &to);
bool remove_file(const std::string &p);
// Recursively deletes a directory. Returns true if it is gone afterwards,
// including when it was never there.
bool remove_directory(const std::string &p);

// Entries directly inside `dir`, names only, sorted. Empty if `dir` is missing.
std::vector<std::string> list_directory(const std::string &dir, bool directories_only = false);

int64_t file_size(const std::string &p);

// Reads the first line of a text file, trimmed. Returns an empty string if the
// file is missing or empty -- used for swos-path.txt.
std::string read_first_line(const std::string &p);

// Where the game lives: the path in <base_dir>/swos-path.txt if present, else
// <base_dir>/SWOS. Ported from getSWOSDir in miniLauncher_u.pas.
std::string swos_dir(const std::string &base_dir);

// Platform name of the game binary inside swos_dir().
const char *game_executable_name();

} // namespace swos
