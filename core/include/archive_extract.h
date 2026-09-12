// archive_extract.h -- read-only archive extraction (7z, zip, tar...).
//
// The Delphi DLC manager shelled out to a bundled temp/7z.exe:
//
//     param := ' x "' + dlcName + '.7z" -o"' + dlcName + '" -y';
//     TUtil.WinExecAndWait32(pchar(exe), pchar(param), 0);
//
// which is Windows-only and silent about failure -- WinExecAndWait32's exit
// code was discarded, so a corrupt download extracted to nothing and the
// manager carried on. libarchive replaces it in-process on all three
// platforms, and reports what went wrong.

#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace swos {

struct ExtractProgress {
    int files_done = 0;
    int files_total = 0;     // 0 until counted; see Extractor::count_entries
    std::string current;     // entry being written
};

struct ExtractResult {
    bool ok = false;
    int files = 0;
    std::string error;
};

// Extracts every entry of `archive_path` under `destination_dir`, creating it
// if needed. Existing files are overwritten, matching 7z's -y.
//
// Entry paths are sanitised: absolute paths and any component that would escape
// `destination_dir` are rejected, so a malicious or malformed archive from the
// DLC server cannot write outside the DLC folder. The Delphi version had no
// such guard.
ExtractResult extract_archive(const std::string &archive_path, const std::string &destination_dir,
                              const std::function<void(ExtractProgress)> &on_progress = {},
                              const std::atomic<bool> *cancel = nullptr);

// Lists entry names without extracting. Used to show what a pack contains.
std::vector<std::string> list_archive(const std::string &archive_path);

} // namespace swos
