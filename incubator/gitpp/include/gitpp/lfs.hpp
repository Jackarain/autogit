// ============================================================================
// gitpp/lfs.hpp  --  Git LFS support for gitpp
//
// Features:
//   o  LFS pointer file creation and parsing (SHA-256)
//   o  LFS object storage in .git/lfs/objects/
//   o  LFS pattern matching from .gitattributes (filter=lfs)
//   o  Clean/smudge filter integration
//   o  Batch object upload for pre-push
// ============================================================================

#pragma once

#include "gitpp.hpp"

#include <git2.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gitpp {
namespace lfs {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr std::string_view k_pointer_version =
    "https://git-lfs.github.com/spec/v1";

constexpr std::string_view k_lfs_objects_dir = "lfs/objects";

// ---------------------------------------------------------------------------
// pointer  --  A Git LFS pointer file
//
// A pointer file is a small text file that stores the location of the
// actual content.  Format (exactly three lines):
//
//   version https://git-lfs.github.com/spec/v1\n
//   oid sha256:<64-hex-chars>\n
//   size <decimal>\n
// ---------------------------------------------------------------------------

struct pointer {
    std::string oid;       // SHA-256 hex string (64 chars)
    std::int64_t size = 0; // Original file size in bytes

    // Default-construct a zero pointer.
    pointer() noexcept = default;

    // Construct with explicit OID and size.
    pointer(std::string oid_, std::int64_t size_)
        : oid(std::move(oid_))
        , size(size_)
    {}

    GITPP_NODISCARD bool valid() const noexcept {
        return oid.size() == 64 && size >= 0;
    }

    // Serialise to a three-line string.
    GITPP_NODISCARD std::string encode() const;

    // Parse a pointer from a three-line string.
    GITPP_NODISCARD static std::optional<pointer> decode(
        std::string_view text) noexcept;

    // Create a pointer from the content of a file on disk.
    // Reads the file, computes SHA-256, writes the object into
    // the LFS store, and returns the pointer.
    GITPP_NODISCARD static std::optional<pointer> create_from_file(
        const std::filesystem::path& file_path,
        const std::filesystem::path& gitdir);

    // Check whether a buffer looks like an LFS pointer file.
    GITPP_NODISCARD static bool is_pointer(
        std::string_view text) noexcept;

    // Check whether a file on disk is an LFS pointer file.
    GITPP_NODISCARD static bool is_pointer_file(
        const std::filesystem::path& path);
};

// ---------------------------------------------------------------------------
// object_store  --  Manage .git/lfs/objects/ directory
// ---------------------------------------------------------------------------

class object_store {
public:
    explicit object_store(std::filesystem::path gitdir);

    // Store a file's content into the LFS object store.
    // Returns the pointer if successful.
    GITPP_NODISCARD std::optional<pointer> store(
        const std::filesystem::path& file_path);

    // Retrieve the path to a local LFS object by its OID.
    GITPP_NODISCARD std::filesystem::path object_path(
        const std::string& oid) const;

    // Check whether an object already exists locally.
    GITPP_NODISCARD bool exists(const std::string& oid) const;

    // Return the root of the LFS object store.
    GITPP_NODISCARD const std::filesystem::path& root() const noexcept {
        return objects_root_;
    }

private:
    std::filesystem::path objects_root_;
};

// ---------------------------------------------------------------------------
// attribute matching  --  .gitattributes LFS pattern support
// ---------------------------------------------------------------------------

// Load all patterns marked with `filter=lfs` from the repository's
// `.gitattributes` (both the root worktree version and HEAD).
// `gitdir` is the path to `.git`.
GITPP_NODISCARD std::vector<std::string> load_lfs_patterns(
    const std::filesystem::path& gitdir,
    git_repository* repo);

// Check whether a given relative path matches any of the LFS patterns.
// Patterns are simple globs as used by .gitattributes.
GITPP_NODISCARD bool path_matches_lfs(
    std::string_view path,
    const std::vector<std::string>& patterns);

// Convenience: load patterns then match.
GITPP_NODISCARD bool is_lfs_tracked(
    std::string_view path,
    git_repository* repo,
    const std::filesystem::path& gitdir);

// ---------------------------------------------------------------------------
// pointer file I/O helpers
// ---------------------------------------------------------------------------

// Write a pointer file to disk (overwriting the original large file).
// Returns true on success.
bool write_pointer_file(
    const std::filesystem::path& dest,
    const pointer& ptr);

// Read a pointer file from disk.
GITPP_NODISCARD std::optional<pointer> read_pointer_file(
    const std::filesystem::path& path);

// ---------------------------------------------------------------------------
// Batch-upload support  (pre-push)
// ---------------------------------------------------------------------------

struct lfs_object {
    std::string oid;
    std::int64_t size;
    bool exists = false;
};

// Collect LFS objects referenced in a set of commits.
// Walks from the given revwalk and collects LFS pointer blobs.
GITPP_NODISCARD std::vector<lfs_object> collect_lfs_objects(
    git_repository* repo,
    const std::vector<git_oid>& commit_ids);

// Perform a batch upload of LFS objects to the remote LFS server.
// `remote_url` is the Git remote URL (without auth).
// Returns 0 on success, -1 on error.
int push_lfs_objects(
    git_repository* repo,
    const std::string& remote_url,
    const std::vector<lfs_object>& objects,
    const std::string& username = {},
    const std::string& password = {});

} // namespace lfs
} // namespace gitpp
