// paths.h -- small filesystem helpers.
//
// Thin wrappers over <filesystem> that swallow the error_code overloads, so
// call sites read like the Delphi originals (FileExists, ForceDirectories) and
// a missing file is never an exception.

#pragma once

#include <string>

namespace swos {

// Directory holding the running executable. This is what the Delphi launcher
// called gCurDir; every relative path (ini/, dlc/, col_palettes/) hangs off it.
std::string executable_dir();

std::string path_join(const std::string &a, const std::string &b);
std::string parent_path(const std::string &p);

bool file_exists(const std::string &p);
bool directory_exists(const std::string &p);
bool ensure_directory(const std::string &p);
bool rename_file(const std::string &from, const std::string &to);

// Reads the first line of a text file, trimmed. Returns an empty string if the
// file is missing or empty -- used for swos-path.txt.
std::string read_first_line(const std::string &p);

// Where the game lives: the path in <base_dir>/swos-path.txt if present, else
// <base_dir>/SWOS. Ported from getSWOSDir in miniLauncher_u.pas.
std::string swos_dir(const std::string &base_dir);

// Platform name of the game binary inside swos_dir().
const char *game_executable_name();

} // namespace swos
