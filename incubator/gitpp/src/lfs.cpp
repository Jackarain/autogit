//
// lfs.cpp
// ~~~~~~~
//
// Copyright (c) 2026 Jack (jack dot wgm at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "gitpp/lfs.hpp"

#include "httpc/httpc.hpp"

#include <git2.h>
#include <git2/sys/filter.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <unordered_set>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/json.hpp>

#include <openssl/evp.h>

#ifdef _WIN32
#  include <process.h>
#else
#  include <unistd.h>
#endif


namespace gitpp {
namespace lfs {

    // -----------------------------------------------------------------------
    // 辅助函数 (匿名命名空间)
    // -----------------------------------------------------------------------

    namespace {

        static std::string sha256_hex(const unsigned char hash[32])
        {
            static const char hex[] = "0123456789abcdef";
            std::string result(64, '\0');
            for (int i = 0; i < 32; ++i)
            {
                result[i * 2] = hex[hash[i] >> 4];
                result[i * 2 + 1] = hex[hash[i] & 0xf];
            }
            return result;
        }

        static std::string file_sha256(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return {};

            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            if (!ctx)
                return {};

            if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1)
            {
                EVP_MD_CTX_free(ctx);
                return {};
            }

            unsigned char buf[8192];
            while (file.read(reinterpret_cast<char*>(buf), sizeof(buf)) || file.gcount() > 0)
            {
                if (EVP_DigestUpdate(ctx, buf, static_cast<size_t>(file.gcount())) != 1)
                {
                    EVP_MD_CTX_free(ctx);
                    return {};
                }
            }

            unsigned char hash[32];
            unsigned int len = 0;
            if (EVP_DigestFinal_ex(ctx, hash, &len) != 1)
            {
                EVP_MD_CTX_free(ctx);
                return {};
            }
            EVP_MD_CTX_free(ctx);

            return sha256_hex(hash);
        }

        static std::int64_t get_file_size(const std::filesystem::path& path)
        {
            std::error_code ec;
            auto sz = std::filesystem::file_size(path, ec);
            return ec ? -1 : static_cast<std::int64_t>(sz);
        }

    } // anonymous namespace

    // -----------------------------------------------------------------------
    // pointer（指针）
    // -----------------------------------------------------------------------

    std::string pointer::encode() const
    {
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

    std::optional<pointer> pointer::decode(std::string_view text) noexcept
    {
        // 必须恰好有三行。
        std::string_view remaining = text;
        std::string lines[3];
        int line_count = 0;

        while (!remaining.empty() && line_count < 3)
        {
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
        for (char c : oid_hex)
        {
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

    std::optional<pointer> pointer::create_from_file(const std::filesystem::path& file_path,
        const std::filesystem::path& gitdir)
    {
        auto oid_hex = file_sha256(file_path);
        if (oid_hex.empty())
            return std::nullopt;

        auto sz = get_file_size(file_path);
        if (sz < 0)
            return std::nullopt;

        object_store store(gitdir);
        auto stored = store.store(oid_hex, sz, file_path);
        if (!stored)
            return std::nullopt;

        return pointer(oid_hex, sz);
    }

    bool pointer::is_pointer(std::string_view text) noexcept
    {
        auto ptr = decode(text);
        return ptr.has_value();
    }

    bool pointer::is_pointer_file(const std::filesystem::path& path)
    {
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

    // -----------------------------------------------------------------------
    // object_store（对象存储）
    // -----------------------------------------------------------------------

    object_store::object_store(std::filesystem::path gitdir)
        : objects_root_(std::move(gitdir))
    {
        objects_root_ /= k_lfs_objects_dir;
    }

    std::optional<pointer> object_store::store(const std::filesystem::path& file_path)
    {
        auto oid_hex = file_sha256(file_path);
        if (oid_hex.empty())
            return std::nullopt;

        auto sz = get_file_size(file_path);
        if (sz < 0)
            return std::nullopt;

        return store(oid_hex, sz, file_path);
    }

    std::optional<pointer> object_store::store(
        const std::string& oid, std::int64_t size, const std::filesystem::path& file_path)
    {
        auto dest = object_path(oid);

        std::error_code ec;
        std::filesystem::create_directories(dest.parent_path(), ec);
        if (ec)
            return std::nullopt;

        if (!std::filesystem::exists(dest, ec))
        {
            std::filesystem::copy_file(file_path, dest, std::filesystem::copy_options::none, ec);
            if (ec)
                return std::nullopt;
        }

        return pointer(oid, size);
    }

    std::optional<pointer> object_store::store_temp(const std::filesystem::path& tmp_path)
    {
        // 计算临时文件的 SHA-256。
        auto oid_hex = file_sha256(tmp_path);
        if (oid_hex.empty())
            return std::nullopt;

        auto sz = get_file_size(tmp_path);
        if (sz < 0)
            return std::nullopt;

        auto dest = object_path(oid_hex);

        // 创建父目录。
        std::error_code ec;
        std::filesystem::create_directories(dest.parent_path(), ec);

        // 如果目标已存在，则直接删除临时文件并返回。
        if (std::filesystem::exists(dest, ec))
        {
            std::filesystem::remove(tmp_path, ec);
            return pointer(oid_hex, sz);
        }

        // 将临时文件移动到 LFS 对象存储。
        std::filesystem::rename(tmp_path, dest, ec);
        if (ec)
            return std::nullopt;

        return pointer(oid_hex, sz);
    }

    std::filesystem::path object_store::object_path(const std::string& oid) const
    {
        if (oid.size() < 4)
            return objects_root_;

        // .git/lfs/objects/<xx>/<xx>/<oid> 目录结构
        auto sub1 = oid.substr(0, 2);
        auto sub2 = oid.substr(2, 2);
        return objects_root_ / sub1 / sub2 / oid;
    }

    bool object_store::exists(const std::string& oid) const
    {
        std::error_code ec;
        return std::filesystem::exists(object_path(oid), ec);
    }

    // -----------------------------------------------------------------------
    // 属性匹配（attribute matching）
    // -----------------------------------------------------------------------

    namespace {

        // 解析单行 .gitattributes，查找 "filter=lfs"。
        // 如果该行包含 filter=lfs，则返回模式部分。
        // 以 # 开头的行是注释。会修剪前导/尾随空白。
        std::optional<std::string> parse_lfs_attribute_line(std::string_view line)
        {
            // 修剪空白。
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
                line.remove_prefix(1);
            while (
                !line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
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
            for (;;)
            {
                // 修剪前导空白。
                while (!attrs.empty() && (attrs.front() == ' ' || attrs.front() == '\t'))
                    attrs.remove_prefix(1);
                if (attrs.empty())
                    break;

                auto attr_end = attrs.find_first_of(" \t");
                std::string_view attr;
                if (attr_end == std::string_view::npos)
                {
                    attr = attrs;
                    attrs = {};
                }
                else
                {
                    attr = attrs.substr(0, attr_end);
                    attrs = attrs.substr(attr_end + 1);
                }

                if (attr == "filter=lfs")
                    return std::string(pattern);
            }

            return std::nullopt;
        }

        // 从 .gitattributes 文件内容加载模式。
        static std::vector<std::string> load_patterns_from_content(std::string_view content)
        {
            std::vector<std::string> patterns;
            for (;;)
            {
                auto nl = content.find('\n');
                std::string_view line;
                if (nl == std::string_view::npos)
                {
                    line = content;
                    content = {};
                }
                else
                {
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
        static bool glob_match(std::string_view pattern, std::string_view path) noexcept
        {
            auto pi = pattern.begin();
            auto pe = pattern.end();
            auto si = path.begin();
            auto se = path.end();

            while (pi != pe && si != se)
            {
                if (*pi == '*')
                {
                    // 检查 '**' 模式。
                    if (pi + 1 != pe && *(pi + 1) == '*')
                    {
                        // '**' 匹配包括 '/' 在内的任何内容。
                        pi += 2;
                        // 如果模式中有尾随的 '/'，则跳过。
                        if (pi != pe && *pi == '/')
                            ++pi;
                        // 如果 '**' 在末尾，则匹配一切。
                        if (pi == pe)
                            return true;
                        // 尝试在每个位置上匹配模式的其余部分。
                        while (si != se)
                        {
                            if (glob_match(std::string_view(&*pi, pe - pi),
                                    std::string_view(&*si, se - si)))
                                return true;
                            ++si;
                        }
                        return false;
                    }
                    // 单个 '*' — 跳过直到 '/' 或结束。
                    ++pi;
                    while (si != se && *si != '/')
                    {
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
                if (*pi == '?')
                {
                    if (*si == '/')
                        return false;
                    ++pi;
                    ++si;
                    continue;
                }
                if (*pi != *si)
                {
                    return false;
                }
                ++pi;
                ++si;
            }

            // 跳过模式中尾随的 '*'。
            while (pi != pe && *pi == '*')
            {
                // 检查 '**'。
                if (pi + 1 != pe && *(pi + 1) == '*')
                    pi += 2;
                else
                    ++pi;
            }

            return pi == pe && si == se;
        }

    } // anonymous namespace

    std::vector<std::string> load_lfs_patterns(const std::filesystem::path& gitdir,
        git_repository* repo)
    {
        std::vector<std::string> patterns;

        // 1. 尝试从工作树根目录读取 .gitattributes。
        if (repo)
        {
            const char* workdir = git_repository_workdir(repo);
            if (workdir)
            {
                std::filesystem::path attr_path = std::filesystem::path(workdir) / ".gitattributes";
                std::ifstream file(attr_path);
                if (file)
                {
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
        if (repo)
        {
            // 使用 libgit2 从 HEAD 读取 .gitattributes。
            git_oid head_oid;
            if (git_reference_name_to_id(&head_oid, repo, "HEAD") == 0)
            {
                git_commit* commit = nullptr;
                if (git_commit_lookup(&commit, repo, &head_oid) == 0)
                {
                    git_tree* tree = nullptr;
                    if (git_commit_tree(&tree, commit) == 0)
                    {
                        git_tree_entry* entry = nullptr;
                        if (git_tree_entry_bypath(&entry, tree, ".gitattributes") == 0)
                        {
                            git_blob* blob = nullptr;
                            if (git_blob_lookup(&blob, repo, git_tree_entry_id(entry)) == 0)
                            {
                                const char* content = (const char*)git_blob_rawcontent(blob);
                                auto sz = git_blob_rawsize(blob);
                                auto pats =
                                    load_patterns_from_content(std::string_view(content, sz));
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
        for (auto& pat : patterns)
        {
            if (seen.insert(pat).second)
                result.push_back(std::move(pat));
        }

        return result;
    }

    bool path_matches_lfs(std::string_view path, const std::vector<std::string>& patterns)
    {
        for (const auto& pattern : patterns)
        {
            if (glob_match(pattern, path))
                return true;
        }
        return false;
    }

    bool is_lfs_tracked(
        std::string_view path, git_repository* repo, const std::filesystem::path& gitdir)
    {
        auto patterns = load_lfs_patterns(gitdir, repo);
        return path_matches_lfs(path, patterns);
    }

    // -----------------------------------------------------------------------
    // pointer::write_to / read_from
    // -----------------------------------------------------------------------

    bool pointer::write_to(const std::filesystem::path& dest) const
    {
        std::ofstream file(dest, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;
        auto content = encode();
        file.write(content.data(), content.size());
        return file.good();
    }

    std::optional<pointer> pointer::read_from(const std::filesystem::path& path)
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

        return decode(content);
    }

    // -----------------------------------------------------------------------
    // LFS 基于 git_filter_register 的过滤器实现
    // -----------------------------------------------------------------------

    namespace {

        // 过滤器全局数据（git_filter_register 是全局的）。
        struct filter_global_data
        {
            std::string gitdir;
        };

        static std::unique_ptr<filter_global_data> g_filter_data;

        // -----------------------------------------------------------------------
        // 自定义 git_writestream —— 处理 LFS clean/smudge 的流式转换
        // -----------------------------------------------------------------------

        // 在 .git/lfs/tmp/ 下生成唯一的临时文件名。
        std::filesystem::path make_tmp_path(const std::filesystem::path& gitdir)
        {
            auto tmp_dir = gitdir / k_lfs_tmp_dir;
            std::error_code ec;
            std::filesystem::create_directories(tmp_dir, ec);

            // 使用随机数和 PID 生成唯一名称。
            std::random_device rd;
#ifdef _WIN32
            auto pid = static_cast<long>(::_getpid());
#else
            auto pid = static_cast<long>(::getpid());
#endif
            auto name = "lfs-tmp-" + std::to_string(rd()) + "-" + std::to_string(pid);

            return tmp_dir / name;
        }

        struct lfs_writestream
        {
            git_writestream base;

            // 下游（下一个过滤器或最终输出）。
            git_writestream* next = nullptr;

            // 方向：true = clean（工作树 → ODB），false = smudge（ODB → 工作树）。
            bool clean_mode = true;

            // --- clean 模式的状态 ---
            EVP_MD_CTX* hash_ctx = nullptr; // SHA-256 哈希上下文（OpenSSL EVP）
            std::filesystem::path tmp_path; // 临时文件路径
            std::ofstream tmp_file;         // 临时文件流
            std::int64_t file_size = 0;     // 原始文件大小
            std::string gitdir;             // .git 目录路径

            // --- smudge 模式的状态 ---
            std::string pointer_data; // 指针文件内容缓冲区
        };

        // clean 模式：write 回调 — 哈希数据并写入临时文件。
        static int lfs_stream_write_clean(lfs_writestream* stream, const char* data, size_t len)
        {
            // 更新 SHA-256 哈希。
            if (stream->hash_ctx)
            {
                EVP_DigestUpdate(stream->hash_ctx, data, len);
            }

            // 写入临时文件。
            if (stream->tmp_file.is_open())
            {
                stream->tmp_file.write(data, static_cast<std::streamsize>(len));
                if (!stream->tmp_file.good())
                    return -1;
            }

            stream->file_size += static_cast<std::int64_t>(len);
            return 0;
        }

        // smudge 模式：write 回调 — 收集指针文件内容。
        static int lfs_stream_write_smudge(lfs_writestream* stream, const char* data, size_t len)
        {
            stream->pointer_data.append(data, len);
            return 0;
        }

        // write 回调分发。
        static int lfs_stream_write(git_writestream* base, const char* data, size_t len)
        {
            auto* stream = reinterpret_cast<lfs_writestream*>(base);
            if (stream->clean_mode)
                return lfs_stream_write_clean(stream, data, len);
            else
                return lfs_stream_write_smudge(stream, data, len);
        }

        // clean 模式：close 回调 — 完成哈希，存储对象，写入指针到下游。
        static int lfs_stream_close_clean(lfs_writestream* stream)
        {
            // 完成 SHA-256 哈希。
            unsigned char hash[32];
            unsigned int hash_len = 0;
            if (stream->hash_ctx)
            {
                EVP_DigestFinal_ex(stream->hash_ctx, hash, &hash_len);
                EVP_MD_CTX_free(stream->hash_ctx);
                stream->hash_ctx = nullptr;
            }
            auto oid_hex = sha256_hex(hash);

            // 关闭临时文件。
            if (stream->tmp_file.is_open())
                stream->tmp_file.close();

            // 将临时文件移动到 LFS 对象存储。
            object_store store(stream->gitdir);
            auto ptr = store.store_temp(stream->tmp_path);
            if (!ptr)
            {
                // 存储失败，清理临时文件。
                std::error_code ec;
                std::filesystem::remove(stream->tmp_path, ec);
                return -1;
            }

            // 将指针文件内容写入下游（最终进入索引）。
            auto pointer_text = ptr->encode();
            int ret = stream->next->write(stream->next, pointer_text.data(), pointer_text.size());
            if (ret != 0)
                return ret;

            // 关闭下游流。
            return stream->next->close(stream->next);
        }

        // smudge 模式：close 回调 — 解析指针，读取实际内容并写入下游。
        static int lfs_stream_close_smudge(lfs_writestream* stream)
        {
            // 尝试解析指针文件。
            auto ptr = pointer::decode(stream->pointer_data);
            if (!ptr)
            {
                // 不是有效的指针文件 — 直接透传原始内容。
                int ret = stream->next->write(stream->next,
                    stream->pointer_data.data(),
                    stream->pointer_data.size());
                if (ret != 0)
                    return ret;
                return stream->next->close(stream->next);
            }

            // 从 LFS 对象存储读取实际内容。
            object_store store(stream->gitdir);
            auto obj_path = store.object_path(ptr->oid);

            std::ifstream obj_file(obj_path, std::ios::binary);
            if (!obj_file)
            {
                // LFS 对象不在本地 — 回退：透传指针文件内容。
                int ret = stream->next->write(stream->next,
                    stream->pointer_data.data(),
                    stream->pointer_data.size());
                if (ret != 0)
                    return ret;
                return stream->next->close(stream->next);
            }

            // 分块读取并写入下游。
            char buf[65536];
            while (obj_file.read(buf, sizeof(buf)) || obj_file.gcount() > 0)
            {
                auto count = static_cast<size_t>(obj_file.gcount());
                int ret = stream->next->write(stream->next, buf, count);
                if (ret != 0)
                    return ret;
            }

            return stream->next->close(stream->next);
        }

        // close 回调分发。
        static int lfs_stream_close(git_writestream* base)
        {
            auto* stream = reinterpret_cast<lfs_writestream*>(base);
            if (stream->clean_mode)
                return lfs_stream_close_clean(stream);
            else
                return lfs_stream_close_smudge(stream);
        }

        // free 回调。
        static void lfs_stream_free(git_writestream* base)
        {
            auto* stream = reinterpret_cast<lfs_writestream*>(base);
            if (stream->hash_ctx)
            {
                EVP_MD_CTX_free(stream->hash_ctx);
                stream->hash_ctx = nullptr;
            }
            delete stream;
        }

        // -----------------------------------------------------------------------
        // 过滤器回调
        // -----------------------------------------------------------------------

        // check 回调：决定是否为给定文件调用过滤器。
        static int lfs_filter_check(git_filter* /*self*/,
            void** /*payload*/,
            const git_filter_source* src,
            const char** /*attr_values*/)
        {
            // 对于 clean 模式，如果文件大小为 0，则不使用 LFS
            auto mode = git_filter_source_mode(src);
            if (mode == GIT_FILTER_CLEAN)
            {
                auto repo = git_filter_source_repo(src);
                const char* workdir = repo ? git_repository_workdir(repo) : nullptr;
                const char* path = git_filter_source_path(src);
                if (workdir && path)
                {
                    std::filesystem::path full_path = std::filesystem::path(workdir) / path;
                    std::error_code ec;
                    auto sz = std::filesystem::file_size(full_path, ec);
                    if (!ec && sz == 0)
                    {
                        return GIT_PASSTHROUGH;
                    }
                }
            }

            // 对于 smudge 或非零大小的 clean，始终接受。
            // clean 模式下 stream 会将大文件转换为指针；
            // smudge 模式下如果内容不是指针文件，stream 会透传。
            return 0;
        }

        // stream 回调：创建自定义的 git_writestream。
        static int lfs_filter_stream(git_writestream** out,
            git_filter* /*self*/,
            void** /*payload*/,
            const git_filter_source* src,
            git_writestream* next)
        {
            if (!g_filter_data)
            {
                // 没有全局数据，无法运行。
                return GIT_PASSTHROUGH;
            }

            auto mode = git_filter_source_mode(src);
            auto* stream = new lfs_writestream();

            stream->base.write = lfs_stream_write;
            stream->base.close = lfs_stream_close;
            stream->base.free = lfs_stream_free;
            stream->next = next;
            stream->gitdir = g_filter_data->gitdir;

            if (mode == GIT_FILTER_CLEAN)
            {
                stream->clean_mode = true;

                // 初始化哈希上下文（OpenSSL EVP）。
                stream->hash_ctx = EVP_MD_CTX_new();
                if (!stream->hash_ctx
                    || EVP_DigestInit_ex(stream->hash_ctx, EVP_sha256(), nullptr) != 1)
                {
                    if (stream->hash_ctx)
                    {
                        EVP_MD_CTX_free(stream->hash_ctx);
                        stream->hash_ctx = nullptr;
                    }
                    delete stream;
                    return -1;
                }
                stream->file_size = 0;

                // 创建临时文件路径。
                stream->tmp_path = make_tmp_path(g_filter_data->gitdir);
                stream->tmp_file.open(stream->tmp_path, std::ios::binary);
                if (!stream->tmp_file.is_open())
                {
                    delete stream;
                    return -1;
                }
            }
            else
            {
                stream->clean_mode = false;
                stream->pointer_data.clear();
            }

            *out = &stream->base;
            return 0;
        }

        // cleanup 回调。
        static void lfs_filter_cleanup(git_filter* /*self*/, void* /*payload*/)
        {
            // 每个流的清理在 free 回调中处理。
        }

        // 全局 filter 实例。
        static git_filter g_lfs_filter = {
            GIT_FILTER_VERSION,
            "filter=lfs",      // attributes：当 filter=lfs 时触发
            nullptr,           // initialize
            nullptr,           // shutdown
            lfs_filter_check,  // check
            nullptr,           // apply (deprecated)
            lfs_filter_stream, // stream
            lfs_filter_cleanup // cleanup
        };

    } // anonymous namespace

    int write_lfs_attributes(const std::filesystem::path& gitdir,
        const std::vector<std::string>& patterns)
    {
        if (patterns.empty())
            return 0;

        auto info_attr_path = gitdir / "info" / "attributes";

        // 读取现有的 attributes 内容。
        std::vector<std::string> existing_lines;
        {
            std::ifstream existing(info_attr_path);
            std::string line;
            while (std::getline(existing, line))
            {
                // 去除行尾的 \r。
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                existing_lines.push_back(std::move(line));
            }
        }

        // 收集现有的 LFS filter 模式（去重）。
        std::unordered_set<std::string> existing_patterns;
        for (const auto& line : existing_lines)
        {
            // 查找 " filter=lfs" 或 "filter=lfs"。
            auto pos = line.find("filter=lfs");
            if (pos != std::string::npos)
            {
                // 提取模式部分（第一个空格前的部分）。
                auto pat_end = line.find_first_of(" \t");
                if (pat_end != std::string::npos)
                {
                    existing_patterns.insert(line.substr(0, pat_end));
                }
            }
        }

        // 写入文件。
        std::ofstream out(info_attr_path, std::ios::app);
        if (!out)
            return -1;

        for (const auto& pat : patterns)
        {
            if (existing_patterns.find(pat) == existing_patterns.end())
            {
                out << pat << " filter=lfs\n";
                existing_patterns.insert(pat);
            }
        }

        return out.good() ? 0 : -1;
    }

    int register_lfs_filter(const std::filesystem::path& gitdir)
    {
        if (g_filter_data)
        {
            return 0;
        }

        auto data = std::make_unique<filter_global_data>();
        data->gitdir = gitdir.string();

        int ret = git_filter_register("lfs", &g_lfs_filter, GIT_FILTER_DRIVER_PRIORITY);
        if (ret < 0)
        {
            return ret;
        }

        g_filter_data = std::move(data);
        return 0;
    }

    void unregister_lfs_filter()
    {
        if (g_filter_data)
        {
            git_filter_unregister("lfs");
            g_filter_data.reset();
        }
    }

    // -----------------------------------------------------------------------
    // 收集新的 LFS 对象（用于推送）
    // -----------------------------------------------------------------------

    std::vector<lfs_object> collect_new_lfs_objects(const std::filesystem::path& gitdir)
    {
        std::vector<lfs_object> objects;

        // 扫描 .git/lfs/objects/ 目录以查找所有本地对象。
        auto objects_root = gitdir / k_lfs_objects_dir;

        std::error_code ec;
        if (!std::filesystem::exists(objects_root, ec))
            return objects;

        // 遍历 xx/xx/oid 层级结构。
        for (auto& d1 : std::filesystem::directory_iterator(objects_root, ec))
        {
            if (!d1.is_directory())
                continue;
            for (auto& d2 : std::filesystem::directory_iterator(d1.path(), ec))
            {
                if (!d2.is_directory())
                    continue;
                for (auto& file : std::filesystem::directory_iterator(d2.path(), ec))
                {
                    if (!file.is_regular_file())
                        continue;
                    auto oid = file.path().filename().string();
                    if (oid.size() != 64)
                        continue;

                    lfs_object obj;
                    obj.oid = oid;
                    obj.size = static_cast<std::int64_t>(file.file_size(ec));
                    obj.exists = true;
                    objects.push_back(std::move(obj));
                }
            }
        }

        return objects;
    }

    // -----------------------------------------------------------------------
    // LFS HTTP 推送 — 使用 httpc 库
    // -----------------------------------------------------------------------

    std::optional<std::string> push_lfs_objects_http(const std::string& lfs_push_url,
        const std::filesystem::path& gitdir,
        const std::string& auth_header)
    {
        if (lfs_push_url.empty())
        {
            return "lfs_push_url.empty()";
        }

        // 收集所有本地 LFS 对象。
        auto objects = collect_new_lfs_objects(gitdir);
        if (objects.empty())
        {
            return "objects.empty()";
        }

        // 构建 LFS 批量 API 请求的 JSON (使用 boost.json)。
        namespace json = boost::json;

        json::value body = {{"operation", "upload"},
            {"transfers", {"basic"}},
            {"objects", json::array()},
            {"ref", {{"name", "refs/heads/master"}}}};
        auto& objects_arr = body.as_object()["objects"].as_array();
        for (const auto& obj : objects)
        {
            objects_arr.push_back({{"oid", obj.oid}, {"size", obj.size}});
        }
        std::string json_body = json::serialize(body);

        // 构建批量 API URL。
        std::string base_url = lfs_push_url;
        // 移除末尾的 .git（如有）。
        if (base_url.size() > 4 && base_url.substr(base_url.size() - 4) == ".git")
        {
            base_url.resize(base_url.size() - 4);
        }
        // 移除末尾的斜杠。
        while (!base_url.empty() && base_url.back() == '/')
            base_url.pop_back();
        std::string batch_url = base_url + "/objects/batch";

        // 使用 httpc 发送批量 API 请求（POST）。
        boost::asio::io_context ioc;
        std::string response_body;
        httpc::http_result batch_result;

        // 通过协程异步执行，使用 use_future 同步等待结果。
        boost::asio::co_spawn(
            ioc,
            [&]() mutable -> boost::asio::awaitable<void>
            {
                httpc::http_client client(ioc.get_executor());
                client.connect_timeout(std::chrono::seconds(30));
                client.check_certificate(false);
                client.follow_redirect(false);

                client.set_transfer_handler(
                    [&](auto data, auto size) mutable
                    {
                        response_body.append((const char*)data, size);
                        return 0;
                    });

                httpc::http_request batch_req;
                batch_req.method(httpc::verb::post);
                batch_req.set(httpc::http::field::content_type, "application/vnd.git-lfs+json");
                batch_req.set(httpc::http::field::accept, "application/vnd.git-lfs+json");
                if (!auth_header.empty())
                    batch_req.set(httpc::http::field::authorization, auth_header);
                batch_req.body() = json_body;
                batch_req.prepare_payload();

                batch_result = co_await client.async_perform(batch_url, batch_req);
                co_return;
            },
            boost::asio::detached);
        ioc.run();

        if (!batch_result)
        {
            return "!batch_result";
        }

        auto& response = *batch_result;
        auto http_status = response.result_int();
        if (http_status != 200)
        {
            return "http_status != 200";
        }

        // 使用 boost.json 解析响应。
        json::value jv;
        try
        {
            jv = json::parse(response_body);
        }
        catch (const std::exception& e)
        {
            return std::string("json exception: ") + e.what();
        }

        auto& resp_obj = jv.as_object();
        if (!resp_obj.contains("objects"))
        {
            return "!resp_obj.contains(\"objects\")";
        }

        auto& resp_objects = resp_obj["objects"].as_array();
        int error_count = 0;
        int upload_count = 0;
        int success_count = 0;
        std::string fail_file;

        for (auto& elem : resp_objects)
        {
            auto& obj = elem.as_object();

            // 提取 OID.
            if (!obj.contains("oid") || !obj["oid"].is_string())
            {
                ++error_count;
                continue;
            }

            std::string oid = json::value_to<std::string>(obj["oid"]);

            // 尝试从 actions.upload 或直接 upload 中获取 href.
            std::string upload_url;
            if (obj.contains("actions") && obj["actions"].is_object())
            {
                auto& actions = obj["actions"].as_object();
                if (actions.contains("upload") && actions["upload"].is_object())
                {
                    auto& upload = actions["upload"].as_object();
                    if (upload.contains("href") && upload["href"].is_string())
                        upload_url = json::value_to<std::string>(upload["href"]);
                }
            }
            else if (obj.contains("upload") && obj["upload"].is_object())
            {
                auto& upload = obj["upload"].as_object();
                if (upload.contains("href") && upload["href"].is_string())
                    upload_url = json::value_to<std::string>(upload["href"]);
            }

            if (upload_url.empty())
            {
                ++error_count;
                continue;
            }

            // 找到本地对象文件。
            object_store store(gitdir);
            auto obj_path = store.object_path(oid);

            std::error_code ec;
            if (!std::filesystem::exists(obj_path, ec))
            {
                ++error_count;
                continue;
            }

            ++upload_count;

            // 使用 httpc 的 async_upload_file 流式上传文件。
            ioc.restart();

            httpc::http_result upload_result;
            auto obj_path_str = obj_path.string();

            boost::asio::co_spawn(
                ioc,
                [&]() mutable -> boost::asio::awaitable<void>
                {
                    httpc::http_client client(ioc.get_executor());
                    client.connect_timeout(std::chrono::seconds(30));
                    client.check_certificate(false);
                    client.follow_redirect(false);

                    // 构建上传请求
                    httpc::http_request upload_req;
                    upload_req.method(httpc::verb::put);
                    upload_req.set(httpc::http::field::content_type, "application/octet-stream");

                    // 使用 async_upload_file 直接上传文件
                    upload_result =
                        co_await client.async_upload_file(upload_url, obj_path_str, upload_req);
                },
                boost::asio::detached);
            ioc.run();

            if (!upload_result)
            {
                ++error_count;
                fail_file = obj_path_str;
            }
            else
            {
                auto status = upload_result->result_int();
                if (status >= 200 && status < 300)
                {
                    ++success_count;
                    fail_file.clear();
                }
                else
                {
                    ++error_count;
                    fail_file = obj_path_str;
                }
            }
        }

        if (!fail_file.empty())
            return fail_file;

        return {};
    }

} // namespace lfs
} // namespace gitpp