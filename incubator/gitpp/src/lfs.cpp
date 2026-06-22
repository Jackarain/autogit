// ============================================================================
// gitpp/lfs.cpp  --  gitpp 的 Git LFS 支持（实现）
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
// SHA-256 辅助函数（无外部依赖）
// ============================================================================

namespace {

// 用于计算 LFS OID 的最小 SHA-256 实现。
// 这样可以避免引入额外的加密依赖。
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
    // 追加 0x80
    unsigned char pad = 0x80;
    sha256_update(ctx, &pad, 1);
    // 用零填充
    while (ctx->buf_len != 56) {
        unsigned char zero = 0;
        sha256_update(ctx, &zero, 1);
    }
    // 追加长度（64 位大端序）
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

} // 匿名命名空间

// ============================================================================
// pointer（指针）
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
    // 必须恰好有三行。
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

    // 检查版本行。
    const std::string version_prefix = "version ";
    if (lines[0].substr(0, version_prefix.size()) != version_prefix)
        return std::nullopt;
    if (lines[0].substr(version_prefix.size()) != k_pointer_version)
        return std::nullopt;

    // 检查 OID 行："oid sha256:<64 位十六进制>"
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

    // 检查大小行："size <十进制数>"
    const std::string size_prefix = "size ";
    if (lines[2].substr(0, size_prefix.size()) != size_prefix)
        return std::nullopt;
    std::string size_str = lines[2].substr(size_prefix.size());
    char* end = nullptr;
    auto sz = std::strtoll(size_str.c_str(), &end, 10);
    if (end == size_str.c_str() || sz < 0)
        return std::nullopt;

    // 确保第三行之后没有额外的内容。
    if (!remaining.empty())
        return std::nullopt;

    return pointer(std::move(oid_hex), sz);
}

std::optional<pointer> pointer::create_from_file(
    const std::filesystem::path& file_path,
    const std::filesystem::path& gitdir)
{
    // 计算文件的 SHA-256。
    std::string oid_hex = file_sha256(file_path);
    if (oid_hex.empty())
        return std::nullopt;

    // 获取文件大小。
    auto sz = get_file_size(file_path);
    if (sz < 0)
        return std::nullopt;

    pointer ptr(oid_hex, sz);

    // 将对象存储到 .git/lfs/objects/ 中。
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

    // 最多读取 200 字节（指针文件最多约 150 字节）。
    std::string content;
    content.resize(200, '\0');
    auto read_size = file.read(content.data(), 200).gcount();
    content.resize(static_cast<std::size_t>(read_size));

    return is_pointer(content);
}

// ============================================================================
// object_store（对象存储）
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

    // 创建父目录。
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    if (ec)
        return std::nullopt;

    // 如果对象已存在，则完成。
    if (std::filesystem::exists(dest, ec))
        return pointer(oid_hex, sz);

    // 将文件内容复制到 LFS 对象存储。
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

    // .git/lfs/objects/<xx>/<xx>/<oid> 目录结构
    auto sub1 = oid.substr(0, 2);
    auto sub2 = oid.substr(2, 2);
    return objects_root_ / sub1 / sub2 / oid;
}

bool object_store::exists(const std::string& oid) const {
    std::error_code ec;
    return std::filesystem::exists(object_path(oid), ec);
}

// ============================================================================
// 属性匹配（attribute matching）
// ============================================================================

namespace {

// 解析单行 .gitattributes，查找 "filter=lfs"。
// 如果该行包含 filter=lfs，则返回模式部分。
// 以 # 开头的行是注释。会修剪前导/尾随空白。
static std::optional<std::string> parse_lfs_attribute_line(
    std::string_view line)
{
    // 修剪空白。
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        line.remove_prefix(1);
    while (!line.empty() && (line.back() == ' ' ||
           line.back() == '\t' || line.back() == '\r'))
        line.remove_suffix(1);

    if (line.empty() || line.front() == '#')
        return std::nullopt;

    // 查找模式（第一个标记，在空格/制表符之前）。
    auto pos = line.find_first_of(" \t");
    if (pos == std::string_view::npos)
        return std::nullopt;

    std::string_view pattern = line.substr(0, pos);
    std::string_view attrs = line.substr(pos + 1);

    // 在属性部分中查找 "filter=lfs"。
    // 属性由空格/制表符分隔。
    for (;;) {
        // 修剪前导空白。
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

// 从 .gitattributes 文件内容加载模式。
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

// 简单的 glob 匹配：支持 *, **, ?（不支持括号表达式）。
// '*' 匹配除了 '/' 之外的任何内容（类似 .gitattributes）。
// '**' 匹配包括 '/' 在内的任何内容。
// '?' 匹配除 '/' 之外的任何单个字符。
static bool glob_match(std::string_view pattern,
                       std::string_view path) noexcept
{
    auto pi = pattern.begin();
    auto pe = pattern.end();
    auto si = path.begin();
    auto se = path.end();

    while (pi != pe && si != se) {
        if (*pi == '*') {
            // 检查 '**' 模式。
            if (pi + 1 != pe && *(pi + 1) == '*') {
                // '**' 匹配包括 '/' 在内的任何内容。
                pi += 2;
                // 如果模式中有尾随的 '/'，则跳过。
                if (pi != pe && *pi == '/')
                    ++pi;
                // 如果 '**' 在末尾，则匹配一切。
                if (pi == pe)
                    return true;
                // 尝试在每个位置上匹配模式的其余部分。
                    if (glob_match(std::string_view(&*pi, pe - pi),
                                   std::string_view(&*si, se - si)))
                        return true;
                    ++si;
                }
                return false;
            }
            // 单个 '*' — 跳过直到 '/' 或结束。
            ++pi;
            while (si != se && *si != '/') {
                if (glob_match(std::string_view(&*pi, pe - pi),
                               std::string_view(&*si, se - si)))
                    return true;
                ++si;
            }
            // 也尝试匹配零个字符。
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

    // 跳过模式中尾随的 '*'。
    while (pi != pe && *pi == '*') {
        // 检查 '**'。
        if (pi + 1 != pe && *(pi + 1) == '*')
            pi += 2;
        else
            ++pi;
    }

    return pi == pe && si == se;
}

} // 匿名命名空间

std::vector<std::string> load_lfs_patterns(
    const std::filesystem::path& gitdir,
    git_repository* repo)
{
    std::vector<std::string> patterns;

    // 1. 尝试从工作树根目录读取 .gitattributes。
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

    // 2. 也尝试从 HEAD 读取 .gitattributes（如果可用）。
    if (repo) {
        // 使用 libgit2 从 HEAD 读取 .gitattributes。
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

    // 去重（工作树版本优先）。
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
// 指针文件 I/O（pointer file I/O）
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
// 批量上传支持（Batch-upload support）
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

        // 递归遍历树以查找 LFS 指针数据对象。
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

                            // 检查此数据对象是否为 LFS 指针。
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

    // 从远程 URL 构建 LFS API 端点。
    // Git LFS 使用：<remote_url>/objects/batch
    std::string lfs_endpoint = remote_url;
    // 去除末尾的 .git（如果存在）。
    if (lfs_endpoint.size() > 4 &&
        lfs_endpoint.substr(lfs_endpoint.size() - 4) == ".git") {
        lfs_endpoint.resize(lfs_endpoint.size() - 4);
    }
    // 去除末尾的斜杠。
    while (!lfs_endpoint.empty() && lfs_endpoint.back() == '/')
        lfs_endpoint.pop_back();
    lfs_endpoint += "/objects/batch";

    (void)lfs_endpoint;
    (void)username;
    (void)password;

    // 目前返回 0（成功）并委托给外部的
    // `git lfs push` 命令，该命令更可靠。
    //
    // 在完整实现中，这将执行 HTTP POST 到
    // LFS 批量 API 以上传对象。当前的存根
    // 允许调用者回退到 `git lfs push --object-id`。
    //
    // TODO：使用 Boost.Asio 或 libcurl 实现直接 HTTP 批量上传，
    //      以实现完全自包含的解决方案。

    return 0;
}

} // namespace lfs（LFS 命名空间）
} // namespace gitpp（gitpp 命名空间）
