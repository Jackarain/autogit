// ============================================================================
// gitpp/lfs.cpp  --  Git LFS support for gitpp  (implementation)
// ============================================================================

#include "gitpp/lfs.hpp"

#include <git2.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <vector>

#ifndef _WIN32
#  include <unistd.h>
#endif

namespace gitpp {
namespace lfs {

// ============================================================================
// SHA-256 helper (without external dependency)
// ============================================================================

namespace {

// Minimal SHA-256 implementation for computing LFS OIDs.
// This avoids pulling in an extra crypto dependency.
struct sha256_ctx {
    uint32_t state[8];
    uint64_t count;
    unsigned char buffer[64];
    unsigned int buf_len;
};

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

static inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

static inline uint32_t sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

static inline uint32_t sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

static inline uint32_t gamma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

static inline uint32_t gamma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

static void sha256_init(sha256_ctx* ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
    ctx->buf_len = 0;
}

static void sha256_transform(sha256_ctx* ctx, const unsigned char* block) {
    uint32_t W[64];
    for (int t = 0; t < 16; ++t) {
        W[t] = ((uint32_t)block[t * 4]) << 24 |
               ((uint32_t)block[t * 4 + 1]) << 16 |
               ((uint32_t)block[t * 4 + 2]) << 8 |
               ((uint32_t)block[t * 4 + 3]);
    }
    for (int t = 16; t < 64; ++t) {
        W[t] = gamma1(W[t-2]) + W[t-7] + gamma0(W[t-15]) + W[t-16];
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (int t = 0; t < 64; ++t) {
        uint32_t T1 = h + sigma1(e) + ch(e, f, g) + K[t] + W[t];
        uint32_t T2 = sigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_update(sha256_ctx* ctx,
                          const unsigned char* data, size_t len) {
    ctx->count += len;
    while (len > 0) {
        size_t space = 64 - ctx->buf_len;
        size_t copy = (len < space) ? len : space;
        std::memcpy(ctx->buffer + ctx->buf_len, data, copy);
        ctx->buf_len += (unsigned int)copy;
        data += copy;
        len -= copy;
        if (ctx->buf_len == 64) {
            sha256_transform(ctx, ctx->buffer);
            ctx->buf_len = 0;
        }
    }
}

static void sha256_final(sha256_ctx* ctx, unsigned char* hash) {
    uint64_t bits = ctx->count * 8;
    // Append 0x80
    unsigned char pad = 0x80;
    sha256_update(ctx, &pad, 1);
    // Pad with zeros
    while (ctx->buf_len != 56) {
        unsigned char zero = 0;
        sha256_update(ctx, &zero, 1);
    }
    // Append length as big-endian 64-bit
    for (int i = 7; i >= 0; --i) {
        unsigned char b = (unsigned char)(bits >> (i * 8));
        sha256_update(ctx, &b, 1);
    }
    for (int i = 0; i < 8; ++i) {
        hash[i * 4]     = (unsigned char)(ctx->state[i] >> 24);
        hash[i * 4 + 1] = (unsigned char)(ctx->state[i] >> 16);
        hash[i * 4 + 2] = (unsigned char)(ctx->state[i] >> 8);
        hash[i * 4 + 3] = (unsigned char)(ctx->state[i]);
    }
}

static std::string sha256_hex(const unsigned char* hash) {
    static const char hex[] = "0123456789abcdef";
    std::string result(64, '\0');
    for (int i = 0; i < 32; ++i) {
        result[i * 2]     = hex[hash[i] >> 4];
        result[i * 2 + 1] = hex[hash[i] & 0xf];
    }
    return result;
}

static std::string file_sha256(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    sha256_ctx ctx;
    sha256_init(&ctx);

    unsigned char buf[8192];
    while (file.read(reinterpret_cast<char*>(buf), sizeof(buf)) ||
           file.gcount() > 0) {
        sha256_update(&ctx, buf, (size_t)file.gcount());
    }

    unsigned char hash[32];
    sha256_final(&ctx, hash);
    return sha256_hex(hash);
}

static std::int64_t get_file_size(const std::filesystem::path& path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    return ec ? -1 : static_cast<std::int64_t>(sz);
}

} // anonymous namespace

// ============================================================================
// pointer
// ============================================================================

std::string pointer::encode() const {
    std::string result;
    result.reserve(128);
    result += "version ";
    result += k_pointer_version;
    result += '\n';
    result += "oid sha256:";
    result += oid;
    result += '\n';
    result += "size ";
    result += std::to_string(size);
    result += '\n';
    return result;
}

std::optional<pointer> pointer::decode(std::string_view text) noexcept {
    // Must have exactly three lines.
    std::string_view remaining = text;
    std::string lines[3];
    int line_count = 0;

    while (!remaining.empty() && line_count < 3) {
        auto pos = remaining.find('\n');
        if (pos == std::string_view::npos)
            break;
        lines[line_count++] = std::string(remaining.substr(0, pos));
        remaining.remove_prefix(pos + 1);
    }

    if (line_count != 3)
        return std::nullopt;

    // Check version line.
    const std::string version_prefix = "version ";
    if (lines[0].substr(0, version_prefix.size()) != version_prefix)
        return std::nullopt;
    if (lines[0].substr(version_prefix.size()) != k_pointer_version)
        return std::nullopt;

    // Check oid line: "oid sha256:<64-hex>"
    const std::string oid_prefix = "oid sha256:";
    if (lines[1].substr(0, oid_prefix.size()) != oid_prefix)
        return std::nullopt;
    std::string oid_hex = lines[1].substr(oid_prefix.size());
    if (oid_hex.size() != 64)
        return std::nullopt;
    for (char c : oid_hex) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return std::nullopt;
    }

    // Check size line: "size <decimal>"
    const std::string size_prefix = "size ";
    if (lines[2].substr(0, size_prefix.size()) != size_prefix)
        return std::nullopt;
    std::string size_str = lines[2].substr(size_prefix.size());
    char* end = nullptr;
    auto sz = std::strtoll(size_str.c_str(), &end, 10);
    if (end == size_str.c_str() || sz < 0)
        return std::nullopt;

    // Ensure no extra content after the third line.
    if (!remaining.empty())
        return std::nullopt;

    return pointer(std::move(oid_hex), sz);
}

std::optional<pointer> pointer::create_from_file(
    const std::filesystem::path& file_path,
    const std::filesystem::path& gitdir)
{
    // Compute SHA-256 of the file.
    std::string oid_hex = file_sha256(file_path);
    if (oid_hex.empty())
        return std::nullopt;

    // Get file size.
    auto sz = get_file_size(file_path);
    if (sz < 0)
        return std::nullopt;

    pointer ptr(oid_hex, sz);

    // Store the object in .git/lfs/objects/.
    object_store store(gitdir);
    auto stored = store.store(file_path);
    if (!stored)
        return std::nullopt;

    return ptr;
}

bool pointer::is_pointer(std::string_view text) noexcept {
    auto ptr = decode(text);
    return ptr.has_value();
}

bool pointer::is_pointer_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file)
        return false;

    // Read up to 200 bytes (a pointer file is at most ~150 bytes).
    std::string content;
    content.resize(200, '\0');
    auto read_size = file.read(content.data(), 200).gcount();
    content.resize(static_cast<std::size_t>(read_size));

    return is_pointer(content);
}

// ============================================================================
// object_store
// ============================================================================

object_store::object_store(std::filesystem::path gitdir)
    : objects_root_(std::move(gitdir))
{
    objects_root_ /= k_lfs_objects_dir;
}

std::optional<pointer> object_store::store(
    const std::filesystem::path& file_path)
{
    auto oid_hex = file_sha256(file_path);
    if (oid_hex.empty())
        return std::nullopt;

    auto sz = get_file_size(file_path);
    if (sz < 0)
        return std::nullopt;

    auto dest = object_path(oid_hex);

    // Create parent directories.
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    if (ec)
        return std::nullopt;

    // If the object already exists, we're done.
    if (std::filesystem::exists(dest, ec))
        return pointer(oid_hex, sz);

    // Copy the file content to the LFS object store.
    std::error_code copy_ec;
    std::filesystem::copy_file(file_path, dest,
        std::filesystem::copy_options::none, copy_ec);
    if (copy_ec)
        return std::nullopt;

    return pointer(oid_hex, sz);
}

std::filesystem::path object_store::object_path(
    const std::string& oid) const
{
    if (oid.size() < 4)
        return objects_root_;

    // .git/lfs/objects/<xx>/<xx>/<oid>
    auto sub1 = oid.substr(0, 2);
    auto sub2 = oid.substr(2, 2);
    return objects_root_ / sub1 / sub2 / oid;
}

bool object_store::exists(const std::string& oid) const {
    std::error_code ec;
    return std::filesystem::exists(object_path(oid), ec);
}

// ============================================================================
// attribute matching
// ============================================================================

namespace {

// Parse a single .gitattributes line looking for "filter=lfs".
// Returns the pattern part of the line if the line has filter=lfs.
// Lines starting with # are comments.  Leading/trailing whitespace is trimmed.
static std::optional<std::string> parse_lfs_attribute_line(
    std::string_view line)
{
    // Trim whitespace.
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        line.remove_prefix(1);
    while (!line.empty() && (line.back() == ' ' ||
           line.back() == '\t' || line.back() == '\r'))
        line.remove_suffix(1);

    if (line.empty() || line.front() == '#')
        return std::nullopt;

    // Find the pattern (first token, before any space/tab).
    auto pos = line.find_first_of(" \t");
    if (pos == std::string_view::npos)
        return std::nullopt;

    std::string_view pattern = line.substr(0, pos);
    std::string_view attrs = line.substr(pos + 1);

    // Look for "filter=lfs" in the attributes portion.
    // Attributes are separated by spaces/tabs.
    for (;;) {
        // Trim leading whitespace.
        while (!attrs.empty() &&
               (attrs.front() == ' ' || attrs.front() == '\t'))
            attrs.remove_prefix(1);
        if (attrs.empty())
            break;

        auto attr_end = attrs.find_first_of(" \t");
        std::string_view attr;
        if (attr_end == std::string_view::npos) {
            attr = attrs;
            attrs = {};
        } else {
            attr = attrs.substr(0, attr_end);
            attrs = attrs.substr(attr_end + 1);
        }

        if (attr == "filter=lfs")
            return std::string(pattern);
    }

    return std::nullopt;
}

// Load patterns from a .gitattributes file content.
static std::vector<std::string> load_patterns_from_content(
    std::string_view content)
{
    std::vector<std::string> patterns;
    for (;;) {
        auto nl = content.find('\n');
        std::string_view line;
        if (nl == std::string_view::npos) {
            line = content;
            content = {};
        } else {
            line = content.substr(0, nl);
            content.remove_prefix(nl + 1);
        }

        auto pat = parse_lfs_attribute_line(line);
        if (pat.has_value())
            patterns.push_back(std::move(*pat));

        if (content.empty())
            break;
    }
    return patterns;
}

// Simple glob matching: supports *, **, ?  (no bracket expressions).
// '*' matches anything except '/'  (like .gitattributes).
// '**' matches anything including '/'.
// '?' matches any single character except '/'.
static bool glob_match(std::string_view pattern,
                       std::string_view path) noexcept
{
    auto pi = pattern.begin();
    auto pe = pattern.end();
    auto si = path.begin();
    auto se = path.end();

    while (pi != pe && si != se) {
        if (*pi == '*') {
            // Check for '**' pattern.
            if (pi + 1 != pe && *(pi + 1) == '*') {
                // '**' matches everything including '/'.
                pi += 2;
                // Skip trailing '/' in pattern if present.
                if (pi != pe && *pi == '/')
                    ++pi;
                // If '**' is at the end, it matches everything.
                if (pi == pe)
                    return true;
                // Try to match the rest of pattern at each position.
                while (si != se) {
                    if (glob_match(std::string_view(&*pi, pe - pi),
                                   std::string_view(&*si, se - si)))
                        return true;
                    ++si;
                }
                return false;
            }
            // Single '*' — skip until '/' or end.
            ++pi;
            while (si != se && *si != '/') {
                if (glob_match(std::string_view(&*pi, pe - pi),
                               std::string_view(&*si, se - si)))
                    return true;
                ++si;
            }
            // Also try matching zero characters.
            if (glob_match(std::string_view(&*pi, pe - pi),
                           std::string_view(&*si, se - si)))
                return true;
            continue;
        }
        if (*pi == '?') {
            if (*si == '/')
                return false;
            ++pi; ++si;
            continue;
        }
        if (*pi != *si) {
            return false;
        }
        ++pi; ++si;
    }

    // Skip trailing '*' in pattern.
    while (pi != pe && *pi == '*') {
        // Check for '**'.
        if (pi + 1 != pe && *(pi + 1) == '*')
            pi += 2;
        else
            ++pi;
    }

    return pi == pe && si == se;
}

} // anonymous namespace

std::vector<std::string> load_lfs_patterns(
    const std::filesystem::path& gitdir,
    git_repository* repo)
{
    std::vector<std::string> patterns;

    // 1. Try to read .gitattributes from the worktree root.
    if (repo) {
        const char* workdir = git_repository_workdir(repo);
        if (workdir) {
            std::filesystem::path attr_path =
                std::filesystem::path(workdir) / ".gitattributes";
            std::ifstream file(attr_path);
            if (file) {
                std::string content((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
                auto pats = load_patterns_from_content(content);
                patterns.insert(patterns.end(),
                                std::make_move_iterator(pats.begin()),
                                std::make_move_iterator(pats.end()));
            }
        }
    }

    // 2. Also try to read .gitattributes from HEAD (if available).
    if (repo) {
        // Use libgit2 to read .gitattributes from HEAD.
        git_oid head_oid;
        if (git_reference_name_to_id(&head_oid, repo, "HEAD") == 0) {
            git_commit* commit = nullptr;
            if (git_commit_lookup(&commit, repo, &head_oid) == 0) {
                git_tree* tree = nullptr;
                if (git_commit_tree(&tree, commit) == 0) {
                    git_tree_entry* entry = nullptr;
                    if (git_tree_entry_bypath(&entry, tree,
                            ".gitattributes") == 0) {
                        git_blob* blob = nullptr;
                        if (git_blob_lookup(&blob, repo,
                                git_tree_entry_id(entry)) == 0) {
                            const char* content =
                                (const char*)git_blob_rawcontent(blob);
                            auto sz = git_blob_rawsize(blob);
                            auto pats = load_patterns_from_content(
                                std::string_view(content, sz));
                            patterns.insert(patterns.end(),
                                std::make_move_iterator(pats.begin()),
                                std::make_move_iterator(pats.end()));
                            git_blob_free(blob);
                        }
                        git_tree_entry_free(entry);
                    }
                    git_tree_free(tree);
                }
                git_commit_free(commit);
            }
        }
    }

    // Deduplicate (worktree version takes precedence).
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    for (auto& pat : patterns) {
        if (seen.insert(pat).second)
            result.push_back(std::move(pat));
    }

    return result;
}

bool path_matches_lfs(std::string_view path,
                      const std::vector<std::string>& patterns)
{
    for (const auto& pattern : patterns) {
        if (glob_match(pattern, path))
            return true;
    }
    return false;
}

bool is_lfs_tracked(std::string_view path,
                    git_repository* repo,
                    const std::filesystem::path& gitdir)
{
    auto patterns = load_lfs_patterns(gitdir, repo);
    return path_matches_lfs(path, patterns);
}

// ============================================================================
// pointer file I/O
// ============================================================================

bool write_pointer_file(const std::filesystem::path& dest,
                        const pointer& ptr)
{
    std::ofstream file(dest, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    auto content = ptr.encode();
    file.write(content.data(), content.size());
    return file.good();
}

std::optional<pointer> read_pointer_file(
    const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::nullopt;

    std::string content;
    file.seekg(0, std::ios::end);
    auto sz = file.tellg();
    if (sz <= 0 || sz > 512)
        return std::nullopt;
    content.resize(static_cast<std::size_t>(sz));
    file.seekg(0, std::ios::beg);
    file.read(content.data(), content.size());
    if (!file.good())
        return std::nullopt;

    return pointer::decode(content);
}

// ============================================================================
// Batch-upload support
// ============================================================================

std::vector<lfs_object> collect_lfs_objects(
    git_repository* repo,
    const std::vector<git_oid>& commit_ids)
{
    std::vector<lfs_object> objects;
    std::unordered_set<std::string> seen_oids;

    for (const auto& commit_oid : commit_ids) {
        git_commit* commit = nullptr;
        if (git_commit_lookup(&commit, repo, &commit_oid) != 0)
            continue;

        git_tree* tree = nullptr;
        if (git_commit_tree(&tree, commit) != 0) {
            git_commit_free(commit);
            continue;
        }

        // Walk the tree recursively to find LFS pointer blobs.
        std::function<void(git_tree*)> walk_tree =
            [&](git_tree* t) {
                size_t count = git_tree_entrycount(t);
                for (size_t i = 0; i < count; ++i) {
                    auto* entry = git_tree_entry_byindex(t, i);
                    if (!entry) continue;

                    auto entry_type = git_tree_entry_type(entry);
                    if (entry_type == GIT_OBJECT_TREE) {
                        git_tree* subtree = nullptr;
                        if (git_tree_entry_to_object(
                                reinterpret_cast<git_object**>(&subtree),
                                repo, entry) == 0) {
                            walk_tree(subtree);
                            git_tree_free(subtree);
                        }
                    } else if (entry_type == GIT_OBJECT_BLOB) {
                        git_blob* blob = nullptr;
                        if (git_blob_lookup(&blob, repo,
                                git_tree_entry_id(entry)) == 0) {
                            const char* content =
                                (const char*)git_blob_rawcontent(blob);
                            auto blob_sz = git_blob_rawsize(blob);

                            // Check if this blob is an LFS pointer.
                            auto ptr = pointer::decode(
                                std::string_view(content, blob_sz));
                            if (ptr.has_value()) {
                                if (seen_oids.insert(ptr->oid).second) {
                                    lfs_object obj;
                                    obj.oid = ptr->oid;
                                    obj.size = ptr->size;
                                    obj.exists = true;
                                    objects.push_back(std::move(obj));
                                }
                            }
                            git_blob_free(blob);
                        }
                    }
                }
            };

        walk_tree(tree);
        git_tree_free(tree);
        git_commit_free(commit);
    }

    return objects;
}

int push_lfs_objects(
    git_repository* repo,
    const std::string& remote_url,
    const std::vector<lfs_object>& objects,
    const std::string& username,
    const std::string& password)
{
    if (objects.empty())
        return 0;

    // Build the LFS API endpoint from the remote URL.
    // Git LFS uses: <remote_url>/objects/batch
    std::string lfs_endpoint = remote_url;
    // Strip trailing .git if present.
    if (lfs_endpoint.size() > 4 &&
        lfs_endpoint.substr(lfs_endpoint.size() - 4) == ".git") {
        lfs_endpoint.resize(lfs_endpoint.size() - 4);
    }
    // Remove trailing slash.
    while (!lfs_endpoint.empty() && lfs_endpoint.back() == '/')
        lfs_endpoint.pop_back();
    lfs_endpoint += "/objects/batch";

    (void)lfs_endpoint;
    (void)username;
    (void)password;

    // For now, we return 0 (success) and defer to the external
    // `git lfs push` command which is more reliable.
    //
    // In a full implementation, this would perform an HTTP POST to
    // the LFS batch API to upload objects.  The current stub
    // allows the caller to fall back to `git lfs push --object-id`.
    //
    // TODO: Implement direct HTTP batch upload using Boost.Asio
    //       or libcurl for a fully self-contained solution.

    return 0;
}

} // namespace lfs
} // namespace gitpp
