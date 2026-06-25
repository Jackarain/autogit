// ============================================================================
// gitpp/lfs.hpp  --  gitpp 的 Git LFS 支持
//
// 特性：
//   o  LFS 指针文件的创建和解析（SHA-256）
//   o  LFS 对象存储于 .git/lfs/objects/
//   o  从 .gitattributes 进行 LFS 模式匹配（filter=lfs）
//   o  基于 git_filter_register 的 Clean/smudge 过滤器
//   o  通过 LFS 批量 API 的 HTTP 预推送对象上传
// ============================================================================

#pragma once

#include "gitpp.hpp"

#include <git2.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gitpp {
namespace lfs {

// ---------------------------------------------------------------------------
// 常量
// ---------------------------------------------------------------------------

constexpr std::string_view k_pointer_version =
    "https://git-lfs.github.com/spec/v1";

constexpr std::string_view k_lfs_objects_dir = "lfs/objects";

constexpr std::string_view k_lfs_tmp_dir = "lfs/tmp";

// ---------------------------------------------------------------------------
// pointer  --  Git LFS 指针文件
//
// 指针文件是一个小文本文件，存储实际内容的位置。格式（恰好三行）：
//
//   version https://git-lfs.github.com/spec/v1
//   oid sha256:<64 位十六进制字符>
//   size <十进制数>
// ---------------------------------------------------------------------------

struct pointer {
    std::string oid;       // SHA-256 十六进制字符串（64 字符）
    std::int64_t size = 0; // 原始文件大小（字节）

    pointer() noexcept = default;
    pointer(std::string oid_, std::int64_t size_)
        : oid(std::move(oid_))
        , size(size_)
    {}

    GITPP_NODISCARD bool valid() const noexcept {
        return oid.size() == 64 && size >= 0;
    }

    GITPP_NODISCARD std::string encode() const;
    GITPP_NODISCARD static std::optional<pointer> decode(
        std::string_view text) noexcept;
    GITPP_NODISCARD static std::optional<pointer> create_from_file(
        const std::filesystem::path& file_path,
        const std::filesystem::path& gitdir);
    GITPP_NODISCARD static bool is_pointer(
        std::string_view text) noexcept;
    GITPP_NODISCARD static bool is_pointer_file(
        const std::filesystem::path& path);

    // 将指针写入文件
    GITPP_NODISCARD bool write_to(const std::filesystem::path& dest) const;
    // 从文件读取指针
    GITPP_NODISCARD static std::optional<pointer> read_from(
        const std::filesystem::path& path);
};

// ---------------------------------------------------------------------------
// object_store  --  管理 .git/lfs/objects/ 目录
// ---------------------------------------------------------------------------

class object_store {
public:
    explicit object_store(std::filesystem::path gitdir);

    // 将文件复制到 LFS 对象存储（自动计算 SHA-256）。
    GITPP_NODISCARD std::optional<pointer> store(
        const std::filesystem::path& file_path);

    // 使用已计算的 OID 和大小，将文件复制到 LFS 对象存储。
    GITPP_NODISCARD std::optional<pointer> store(
        const std::string& oid,
        std::int64_t size,
        const std::filesystem::path& file_path);

    // 将临时文件作为 LFS 对象存储（计算 SHA-256 后重命名）。
    // 由 filter stream 的 close 回调使用。
    GITPP_NODISCARD std::optional<pointer> store_temp(
        const std::filesystem::path& tmp_path);

    GITPP_NODISCARD std::filesystem::path object_path(
        const std::string& oid) const;
    GITPP_NODISCARD bool exists(const std::string& oid) const;
    GITPP_NODISCARD const std::filesystem::path& root() const noexcept {
        return objects_root_;
    }

private:
    std::filesystem::path objects_root_;
};

// ---------------------------------------------------------------------------
// 属性匹配
// ---------------------------------------------------------------------------

GITPP_NODISCARD std::vector<std::string> load_lfs_patterns(
    const std::filesystem::path& gitdir,
    git_repository* repo);

GITPP_NODISCARD bool path_matches_lfs(
    std::string_view path,
    const std::vector<std::string>& patterns);

GITPP_NODISCARD bool is_lfs_tracked(
    std::string_view path,
    git_repository* repo,
    const std::filesystem::path& gitdir);

// ---------------------------------------------------------------------------
// 基于 git_filter_register 的 LFS 过滤器
//
// 使用 libgit2 的 git_filter_register 注册一个自定义的 "lfs" 过滤器。
// 当文件通过 git_index_add_bypath 暂存（clean）时，该过滤器会：
//   1. 计算文件的 SHA-256 哈希
//   2. 将原始内容存储到 .git/lfs/objects/ 中
//   3. 将指针文件写入索引（而非原始大文件）
//
// 检出（smudge）时，过滤器会将指针文件还原为原始内容。
//
// 使用前必须通过 register_lfs_filter() 注册过滤器，
// 并在完成后调用 unregister_lfs_filter() 注销。
// ---------------------------------------------------------------------------

// 将 LFS 模式写入仓库的 `.git/info/attributes`，
// 以便 libgit2 能够在暂存/检出时自动为匹配的文件应用 `filter=lfs`。
// 现有的 attributes 内容会被保留（去重后追加）。
// 成功时返回 0，失败时返回 -1。
int write_lfs_attributes(
    const std::filesystem::path& gitdir,
    const std::vector<std::string>& patterns);

// 注册 LFS filter。`gitdir` 是 `.git` 目录的路径。
// 调用此函数前，应先将 LFS 模式写入 `.git/info/attributes`。
// 成功时返回 0，失败时返回负值。
// 该过滤器注册为 "lfs" 名称，优先级为 GIT_FILTER_DRIVER_PRIORITY。
GITPP_NODISCARD int register_lfs_filter(
    const std::filesystem::path& gitdir);

// 注销之前注册的 LFS filter。
void unregister_lfs_filter();

// ---------------------------------------------------------------------------
// LFS HTTP 批量推送
//
// 在调用 git_remote_push 之前，通过 LFS 批量 API 将本地 LFS 对象
// 推送到由 `lfs_push_url` 指定的 LFS 服务器。
// ---------------------------------------------------------------------------

// 状态对象（用于 LFS 对象收集和推送）。
struct lfs_object {
    std::string oid;
    std::int64_t size = 0;
    bool exists = false;
};

// 扫描 .git/lfs/objects/ 目录，收集所有本地 LFS 对象。
GITPP_NODISCARD std::vector<lfs_object> collect_new_lfs_objects(
    const std::filesystem::path& gitdir);

// 通过 LFS 批量 API 将对象推送到远程服务器。
// `lfs_push_url` 是 LFS 服务器的 URL（类似 https://lfs-server.com）。
// 成功时返回空串，失败时返回失败的文件名。

GITPP_NODISCARD std::optional<std::string> push_lfs_objects_http(
    const std::string& lfs_push_url,
    const std::filesystem::path& gitdir,
    const std::string& auth_header = {});

} // namespace lfs
} // namespace gitpp
