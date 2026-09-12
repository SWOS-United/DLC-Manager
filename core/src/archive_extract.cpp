// archive_extract.cpp -- see archive_extract.h.

#include "archive_extract.h"

#include "paths.h"

#include <archive.h>
#include <archive_entry.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace swos {

namespace {

// Rejects entry paths that would write outside `root`: absolute paths, drive
// letters, and any ".." that climbs past the root once normalised. Returns an
// empty path when the entry must be skipped.
fs::path safe_destination(const fs::path &root, const char *entry_name)
{
    if (!entry_name || !*entry_name) return {};

    // Archives use '/' regardless of platform; fs::path handles both, but a
    // Windows-style "C:\" prefix has to be caught explicitly on POSIX where it
    // would otherwise look like an ordinary relative name.
    const std::string name(entry_name);
    if (name.size() >= 2 && name[1] == ':') return {};

    fs::path relative(name);
    if (relative.is_absolute()) return {};

    fs::path resolved = root;
    for (const auto &part : relative) {
        if (part == "..") return {};       // never climb, even if it would stay inside
        if (part == "." || part.empty()) continue;
        resolved /= part;
    }
    if (resolved == root) return {};
    return resolved;
}

int copy_entry_data(archive *reader, archive *writer)
{
    const void *block = nullptr;
    size_t size = 0;
    la_int64_t offset = 0;

    for (;;) {
        int r = archive_read_data_block(reader, &block, &size, &offset);
        if (r == ARCHIVE_EOF) return ARCHIVE_OK;
        if (r < ARCHIVE_WARN) return r;

        r = static_cast<int>(archive_write_data_block(writer, block, size, offset));
        if (r < ARCHIVE_WARN) return r;
    }
}

} // namespace

ExtractResult extract_archive(const std::string &archive_path, const std::string &destination_dir,
                              const std::function<void(ExtractProgress)> &on_progress,
                              const std::atomic<bool> *cancel)
{
    ExtractResult result;

    if (!file_exists(archive_path)) {
        result.error = "Archive not found: " + archive_path;
        return result;
    }
    if (!ensure_directory(destination_dir)) {
        result.error = "Cannot create " + destination_dir;
        return result;
    }

    archive *reader = archive_read_new();
    // support_filter_all / support_format_all covers 7z, zip and the tarballs;
    // the DLC server ships .7z but the extra formats cost nothing.
    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);

    archive *writer = archive_write_disk_new();
    archive_write_disk_set_options(writer, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
                                                   ARCHIVE_EXTRACT_SECURE_NODOTDOT |
                                                   ARCHIVE_EXTRACT_SECURE_SYMLINKS);
    archive_write_disk_set_standard_lookup(writer);

    const int open_status = archive_read_open_filename(reader, archive_path.c_str(), 65536);
    if (open_status != ARCHIVE_OK) {
        result.error = archive_error_string(reader) ? archive_error_string(reader)
                                                    : "Cannot open archive";
        archive_read_free(reader);
        archive_write_free(writer);
        return result;
    }

    const fs::path root = fs::absolute(destination_dir).lexically_normal();
    ExtractProgress progress;
    result.ok = true;

    for (;;) {
        archive_entry *entry = nullptr;
        const int status = archive_read_next_header(reader, &entry);
        if (status == ARCHIVE_EOF) break;
        if (status < ARCHIVE_WARN) {
            result.ok = false;
            result.error = archive_error_string(reader) ? archive_error_string(reader)
                                                        : "Corrupt archive";
            break;
        }

        if (cancel && cancel->load()) {
            result.ok = false;
            result.error = "Cancelled";
            break;
        }

        const fs::path destination = safe_destination(root, archive_entry_pathname(entry));
        if (destination.empty()) continue;  // unsafe or empty entry name -- skip it

        archive_entry_set_pathname(entry, destination.string().c_str());

        const int write_status = archive_write_header(writer, entry);
        if (write_status < ARCHIVE_WARN) {
            result.ok = false;
            result.error = archive_error_string(writer) ? archive_error_string(writer)
                                                        : "Cannot write entry";
            break;
        }

        if (archive_entry_size(entry) > 0 && copy_entry_data(reader, writer) < ARCHIVE_WARN) {
            result.ok = false;
            result.error = archive_error_string(writer) ? archive_error_string(writer)
                                                        : "Cannot write entry data";
            break;
        }
        archive_write_finish_entry(writer);

        ++result.files;
        if (on_progress) {
            progress.files_done = result.files;
            progress.current = destination.filename().string();
            on_progress(progress);
        }
    }

    archive_read_close(reader);
    archive_read_free(reader);
    archive_write_close(writer);
    archive_write_free(writer);

    if (result.ok && result.files == 0) {
        result.ok = false;
        result.error = "Archive contained no usable entries";
    }
    return result;
}

std::vector<std::string> list_archive(const std::string &archive_path)
{
    std::vector<std::string> names;

    archive *reader = archive_read_new();
    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);

    if (archive_read_open_filename(reader, archive_path.c_str(), 65536) == ARCHIVE_OK) {
        archive_entry *entry = nullptr;
        while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
            if (const char *name = archive_entry_pathname(entry)) names.emplace_back(name);
        }
    }
    archive_read_close(reader);
    archive_read_free(reader);
    return names;
}

} // namespace swos
