// ============================================================================
// gitpp/lfs.hpp  --  gitpp 的 Git LFS 支持
//
// 特性：
//   o  LFS 指针文件的创建和解析（SHA-256）
//   o  LFS 对象存储于 .git/lfs/objects/
//   o  从 .gitattributes 进行 LFS 模式匹配（filter=lfs）
//   o  Clean/smudge 过滤器集成
//   o  预推送的批量对象上传
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
// 常量（Constants）
// ---------------------------------------------------------------------------

constexpr std::string_view k_pointer_version =
    "https://git-lfs.github.com/spec/v1";

constexpr std::string_view k_lfs_objects_dir = "lfs/objects";

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

    // 默认构造一个零指针。
    pointer() noexcept = default;

    // 使用显式的 OID 和大小构造。
    pointer(std::string oid_, std::int64_t size_)
        : oid(std::move(oid_))
        , size(size_)
    {}

    GITPP_NODISCARD bool valid() const noexcept {
        return oid.size() == 64 && size >= 0;
    }

    // 序列化为三行字符串。
    GITPP_NODISCARD std::string encode() const;

    // 从三行字符串解析指针。
    GITPP_NODISCARD static std::optional<pointer> decode(
        std::string_view text) noexcept;

    // 从磁盘上的文件内容创建指针。
    // 读取文件，计算 SHA-256，将对象写入 LFS 存储，然后返回指针。
    GITPP_NODISCARD static std::optional<pointer> create_from_file(
        const std::filesystem::path& file_path,
        const std::filesystem::path& gitdir);

    // 检查缓冲区是否看起来像 LFS 指针文件。
    GITPP_NODISCARD static bool is_pointer(
        std::string_view text) noexcept;

    // 检查磁盘上的文件是否为 LFS 指针文件。
    GITPP_NODISCARD static bool is_pointer_file(
        const std::filesystem::path& path);
};

// ---------------------------------------------------------------------------
// object_store  --  管理 .git/lfs/objects/ 目录
// ---------------------------------------------------------------------------

class object_store {
public:
    explicit object_store(std::filesystem::path gitdir);

    // 将文件内容存储到 LFS 对象存储中。
    // 如果成功则返回指针。
    GITPP_NODISCARD std::optional<pointer> store(
        const std::filesystem::path& file_path);

    // 通过 OID 获取本地 LFS 对象的路径。
    GITPP_NODISCARD std::filesystem::path object_path(
        const std::string& oid) const;

    // 检查对象是否已存在于本地。
    GITPP_NODISCARD bool exists(const std::string& oid) const;

    // 返回 LFS 对象存储的根目录。
    GITPP_NODISCARD const std::filesystem::path& root() const noexcept {
        return objects_root_;
    }

private:
    std::filesystem::path objects_root_;
};

// ---------------------------------------------------------------------------
// 属性匹配（attribute matching） --  .gitattributes LFS 模式支持
// ---------------------------------------------------------------------------

// 从仓库的 `.gitattributes`（工作树根目录和 HEAD 中的版本）
// 加载所有标记为 `filter=lfs` 的模式。
// `gitdir` 是 `.git` 的路径。
GITPP_NODISCARD std::vector<std::string> load_lfs_patterns(
    const std::filesystem::path& gitdir,
    git_repository* repo);

// 检查给定的相对路径是否匹配任何 LFS 模式。
// 模式是 .gitattributes 中使用的简单 glob。
GITPP_NODISCARD bool path_matches_lfs(
    std::string_view path,
    const std::vector<std::string>& patterns);

// 便捷函数：先加载模式再进行匹配。
GITPP_NODISCARD bool is_lfs_tracked(
    std::string_view path,
    git_repository* repo,
    const std::filesystem::path& gitdir);

// ---------------------------------------------------------------------------
// 指针文件 I/O 辅助函数
// ---------------------------------------------------------------------------

// 将指针文件写入磁盘（覆盖原始大文件）。
// 成功时返回 true。
bool write_pointer_file(
    const std::filesystem::path& dest,
    const pointer& ptr);

// 从磁盘读取指针文件。
GITPP_NODISCARD std::optional<pointer> read_pointer_file(
    const std::filesystem::path& path);

// ---------------------------------------------------------------------------
// 批量上传支持（预推送）
// ---------------------------------------------------------------------------

struct lfs_object {
    std::string oid;
    std::int64_t size;
    bool exists = false;
};

// 收集一组提交中引用的 LFS 对象。
// 从给定的修订遍历器开始遍历，收集 LFS 指针数据对象。
GITPP_NODISCARD std::vector<lfs_object> collect_lfs_objects(
    git_repository* repo,
    const std::vector<git_oid>& commit_ids);

// 执行 LFS 对象到远程 LFS 服务器的批量上传。
// `remote_url` 是 Git 远程 URL（不含认证信息）。
// 成功时返回 0，失败时返回 -1。
int push_lfs_objects(
    git_repository* repo,
    const std::string& remote_url,
    const std::vector<lfs_object>& objects,
    const std::string& username = {},
    const std::string& password = {});

} // namespace lfs（LFS 命名空间）
} // namespace gitpp（gitpp 命名空间）
