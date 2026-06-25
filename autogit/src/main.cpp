//
// main.cpp
// ~~~~~~~~
//
// Copyright (c) 2022 Jack (jack.arain at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
//

// autogit -- 自动监控 Git 工作目录变更并自动提交/推送的工具。
//
// 本程序利用操作系统文件系统事件通知机制（macOS FSEvents、Linux inotify、
// Windows 等）监视指定目录，检测到文件变更后自动执行 git add、commit、
// push 操作，并支持 Git LFS 大文件存储的自动处理。

#include <cstdio>
#include <cstdlib>

#include <string>
#include <tuple>
#include <atomic>

#include <boost/chrono.hpp>
#include <boost/thread.hpp>
#include <boost/program_options.hpp>
namespace po = boost::program_options;
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

// 当定义了 BOOST_ASIO_SEPARATE_COMPILATION 时，
// 需要在一个编译单元中包含如下文件以编译 Asio 源码。
#include <boost/asio/impl/src.hpp>

namespace net = boost::asio;

#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

#include <signal.h>


#include "autogit/logging.hpp"
#include "autogit/scoped_exit.hpp"
#include "autogit/strutil.hpp"

#include "watchman/watchman.hpp"

#include "gitpp/gitpp.hpp"
#include "gitpp/lfs.hpp"


//////////////////////////////////////////////////////////////////////////

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <git2.h>
#include <fcntl.h>

#ifdef _WIN32
# include <io.h>
# include <windows.h>
#endif

//////////////////////////////////////////////////////////////////////////
// 全局配置变量
// 这些变量通过命令行参数或配置文件设置，在程序各处使用。
//////////////////////////////////////////////////////////////////////////

/** 自定义提交消息前缀，为空时自动生成含时间戳的消息 */
std::string global_commit_message;
/** 是否启用强制推送（force push） */
bool global_force_push;

/** HTTP 认证用户名 */
std::string global_http_username;
/** HTTP 认证密码 */
std::string global_http_password;

/** SSH 公钥文件路径 */
std::string global_ssh_pubkey;
/** SSH 私钥文件路径 */
std::string global_ssh_privkey;
/** SSH 私钥加密口令 */
std::string global_ssh_passphrase;

/** Git 提交作者名 */
std::string global_git_author;
/** Git 提交作者邮箱 */
std::string global_git_email;

/** 远程仓库 URL */
std::string global_git_remote_url;

/**
 * @brief Git LFS 相关配置
 * @{
 */
bool global_enable_lfs = false;                  ///< 是否启用 Git LFS 支持
std::vector<std::string> global_lfs_patterns;    ///< 额外 LFS 文件匹配模式（glob）
std::string global_lfs_push_url;                 ///< LFS 对象推送 URL，为空则从 remote.origin.url 推导
/** @} */


/**
 * @brief libgit2 证书校验回调函数。
 *
 * libgit2 在建立 HTTPS/SSH 连接时调用此回调以验证服务器证书。
 * 本实现始终接受任何证书（返回 1），适用于开发环境或已知安全域。
 *
 * @param cert     证书结构指针。
 * @param valid    libgit2 的预校验结果（0=无效, 1=有效）。
 * @param host     目标主机名。
 * @param payload  用户自定义数据（未使用）。
 * @return int     返回 1 表示接受证书，返回 0 表示拒绝。
 */
int certificate_check_cb(git_cert *cert,
	int valid,
	const char *host,
	void *payload)
{
    return 1; // 始终接受证书
}

/**
 * @brief 获取当前用户的家目录路径。
 *
 * 跨平台兼容：Windows 下使用 USERPROFILE 环境变量，
 * Unix-like 系统（包括 Linux/macOS）使用 HOME 环境变量。
 *
 * @return const char* 家目录路径字符串，失败返回 NULL。
 */
const char* get_home_dir(void)
{
#ifdef _WIN32
	return getenv("USERPROFILE");
#else
	return getenv("HOME");
#endif
}

/**
 * @brief 构建 SSH 密钥认证凭证。
 *
 * 根据全局配置的 SSH 公钥/私钥路径构建 libgit2 凭证。
 * 如果未指定绝对路径，则默认在 ~/.ssh/ 目录下查找 id_rsa。
 * 支持带口令保护的私钥。
 *
 * @param cred               输出参数，指向新创建的 git_cred 结构。
 * @param username_from_url  从远程 URL 中提取的用户名。
 * @return int               0 表示成功，负值表示错误码。
 */
static int build_ssh_credential(git_cred** cred,
    const char* username_from_url)
{
    // 构建默认 SSH 密钥目录：~/.ssh/
    auto default_sshdir =
        std::string(get_home_dir()) +
#ifdef WIN32
        "\\.ssh\\";
#else
        "/.ssh/";
#endif // WIN32（Windows 平台）

    const char* private_key = nullptr;
    const char* public_key = nullptr;

    // 优先使用全局指定的私钥路径，否则在默认目录下查找。
    if (fs::exists(global_ssh_privkey)) {
        private_key = global_ssh_privkey.c_str();
    } else {
        default_sshdir += (global_ssh_privkey.empty() ?
            "idrsa" : global_ssh_privkey);
        private_key = default_sshdir.c_str();
    }

    // 公钥路径处理：优先使用全局指定路径，否则尝试附加到默认目录。
    if (fs::exists(global_ssh_pubkey)) {
        public_key = global_ssh_pubkey.c_str();
    } else {
        auto pubkey_dir = (global_ssh_pubkey.empty() ?
                "" : global_ssh_pubkey);

        default_sshdir = pubkey_dir.empty() ?
            "" : default_sshdir + pubkey_dir;

        public_key = default_sshdir.empty() ?
            nullptr : default_sshdir.c_str();
    }

    // 私钥加密口令，无口令时传 NULL。
    const char* passphrase =
        global_ssh_passphrase.empty() ?
            nullptr : global_ssh_passphrase.c_str();

    return git_cred_ssh_key_new(
        cred,
        username_from_url,
        public_key,             // 这是公钥的路径.
        private_key,            // 这是私钥的路径.
        passphrase              // 如果你的私钥有密码.
    );
}

/**
 * @brief 构建 HTTP 基本认证凭证。
 *
 * 使用全局配置的用户名和密码创建 HTTP Basic 认证凭证。
 * 如果用户名为空则返回 GIT_EAUTH 表示认证失败。
 *
 * @param cred  输出参数，指向新创建的 git_cred 结构。
 * @return int  0 表示成功，GIT_EAUTH 表示认证失败。
 */
static int build_http_credential(git_cred** cred)
{
    if (global_http_username.empty())
        return GIT_EAUTH;

    return git_cred_userpass_plaintext_new(cred,
        global_http_username.c_str(),
        global_http_password.c_str());
}

/**
 * @brief libgit2 凭证获取回调函数。
 *
 * libgit2 在需要身份认证时调用此回调。根据服务器支持的认证类型，
 * 自动选择 SSH 密钥认证或 HTTP 基本认证。
 *
 * @param cred               输出参数，指向新创建的 git_cred 结构。
 * @param url                远程仓库 URL。
 * @param username_from_url  从 URL 中提取的用户名。
 * @param allowed_types      服务器支持的认证类型位掩码。
 * @param payload            用户自定义数据（未使用）。
 * @return int               0 表示成功，GIT_EAUTH 表示认证失败。
 */
int cred_acquire_cb(git_cred** cred,
    const char* url,
    const char* username_from_url,
    unsigned int allowed_types,
    void* payload)
{
    if (allowed_types & GIT_CREDTYPE_SSH_KEY)
        return build_ssh_credential(cred, username_from_url);

    if (allowed_types & GIT_CREDTYPE_USERPASS_PLAINTEXT)
        return build_http_credential(cred);

    return GIT_EAUTH;
}

/**
 * @brief 处理 Git 工作目录状态条目，自动暂存或移除文件变更。
 *
 * 遍历 git_status_list 中的每个状态条目，根据文件变更类型执行对应操作：
 *   - 新增/修改/类型变更/重命名文件 → git_index_add_bypath 暂存到索引
 *   - 删除文件 → git_index_remove_bypath 从索引移除
 *
 * @param index         Git 索引对象，用于暂存变更。
 * @param status        Git 状态列表，包含工作目录所有变更。
 * @param commit_count  输出参数，记录本次处理的变更文件数。
 * @return int          EXIT_SUCCESS（0）表示成功，EXIT_FAILURE（1）表示失败。
 */
static int process_status_entries(
    gitpp::index& index,
    gitpp::status_list& status,
    size_t& commit_count)
{
    commit_count = 0;

    // 遍历状态列表中的每个条目，自动处理工作目录变更。
    for (const git_status_entry* entry : status)
    {
        const char* old_file_path = entry->index_to_workdir->old_file.path;
        auto handle = index.native();
        int ret = 0;

        // 高 28 位为状态标志位，用于区分不同变更类型。
        switch (entry->status & 0xfffffff0)
        {
        // 新增的未被追踪的文件 → 添加到暂存区。
        case GIT_STATUS_WT_NEW:
            ret = git_index_add_bypath(handle, old_file_path);
            commit_count++;
            LOG_DBG << "Untracked file: "
                << entry->index_to_workdir->old_file.path;
            break;

        // 已修改的文件 → 暂存变更。
        case GIT_STATUS_WT_MODIFIED:
            ret = git_index_add_bypath(handle, old_file_path);
            commit_count++;
            LOG_DBG << "modify file: "
                << entry->index_to_workdir->old_file.path;
            break;

        // 已删除的文件 → 从索引中移除。
        case GIT_STATUS_WT_DELETED:
            ret = git_index_remove_bypath(handle, old_file_path);
            commit_count++;
            LOG_DBG << "delete file: "
                << entry->index_to_workdir->old_file.path;
            break;

        // 文件类型变更（如普通文件→符号链接）→ 重新暂存。
        case GIT_STATUS_WT_TYPECHANGE:
            ret = git_index_add_bypath(handle, old_file_path);
            commit_count++;
            LOG_DBG << "typechg file: "
                << entry->index_to_workdir->old_file.path;
            break;

        // 重命名的文件 → 暂存新路径。
        case GIT_STATUS_WT_RENAMED:
            LOG_DBG << "rename file: "
                << entry->index_to_workdir->old_file.path
                << " to "
                << entry->index_to_workdir->new_file.path;
            ret = git_index_add_bypath(handle, old_file_path);
            commit_count++;
            break;

        default:
            break;
        }

        // 检查 libgit2 操作是否成功，失败时记录错误并退出。
        if (ret != 0)
        {
            LOG_DBG << "git_index_add_bypath, path: "
                << old_file_path
                << ", status: " << entry->status
                << ", err: "
                << git_error_last()->message;
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

/**
 * @brief 从 Git 索引构建树对象并创建提交。
 *
 * 将暂存区内容写入磁盘，然后构建一个 tree 对象，
 * 最后根据 HEAD 状态（unborn 或已有提交）创建对应的提交。
 * 提交信息包含自定义前缀、时间戳、作者名和变更文件数。
 *
 * @param repo          Git 仓库对象。
 * @param index         已暂存变更的索引对象。
 * @param commit_count  本次提交包含的文件变更数。
 * @return int           EXIT_SUCCESS（0）表示成功，EXIT_FAILURE（1）表示失败。
 */
static int write_tree_and_commit(
    gitpp::repo& repo,
    gitpp::index& index,
    size_t commit_count)
{
    // 无变更时跳过提交。
    if (commit_count == 0)
        return EXIT_SUCCESS;

    // 将索引写入磁盘（.git/index）。
    if (git_index_write(index.native()) != 0)
    {
        LOG_DBG << "git_index_write, err: "
            << git_error_last()->message;
        return EXIT_FAILURE;
    }

    // 从索引构建 tree 对象。
    gitpp::tree tree = index.write_tree();

    // 构建提交信息，格式:
    //   [自定义前缀] 2024-01-15 10:30:00 author_name N files changed
    auto now = boost::posix_time::second_clock::local_time();
    std::string dt_str = boost::posix_time::to_iso_extended_string(now);
    dt_str[10] = ' ';  // 将 'T' 替换为空格, 得到 "2012-12-08 17:29:32" 格式

    std::ostringstream msg_oss;
    if (!global_commit_message.empty())
        msg_oss << global_commit_message << " ";
    msg_oss << dt_str << " "
        << global_git_author << " "
        << commit_count << " files changed";
    std::string commit_message = msg_oss.str();

    // 当一个新仓库刚创建时 HEAD 并没有指向一个有效的 Commit, 这时
    // 强行创建一个 Commit 将 HEAD 指向这个 Initial Commit.
    // 注意: 不能直接用 repo.head() 来判断，因为新仓库 HEAD 未出生
    // (unborn) 时 git_repository_head() 会返回错误并抛出异常。
    if (repo.is_head_unborn())
    {
        // 初始提交：无父提交，直接创建并更新 HEAD。
        git_oid commit_id;
        gitpp::signature signature(global_git_author, global_git_email);
        git_commit_create_v(&commit_id,
            repo.native(),
            "HEAD",
            signature.native(),
            signature.native(),
            nullptr,
            commit_message.c_str(),
            tree.native_tree(),
            0);
    }
    else
    {
        // 常规提交：以 HEAD 指向的提交作为父提交创建新提交。
        auto head = repo.head();
        gitpp::oid parent_id = head.target();
        gitpp::commit parent = repo.lookup_commit(parent_id);
        gitpp::signature signature(global_git_author, global_git_email);
        (void)repo.create_commit("HEAD",
            signature,
            signature,
            commit_message,
            tree,
            parent);
    }

    return EXIT_SUCCESS;
}

/**
 * @brief 推送 Git LFS 对象到远程服务器。
 *
 * 如果启用了 LFS，首先尝试通过 HTTP Batch API 推送 LFS 对象，
 * 失败时回退到调用系统 git-lfs 命令行工具。
 * 推送 URL 优先使用 --lfs_push_url 参数，否则从 remote.origin.url 推导。
 *
 * @param repo  Git 仓库对象。
 */
static void push_lfs_objects(gitpp::repo& repo)
{
    // 未启用 LFS 时直接返回。
    if (!global_enable_lfs)
        return;

    // 确定 LFS 推送 URL：优先使用 --lfs_push_url，否则从 remote.origin.url 推导。
    std::string lfs_url = global_lfs_push_url;
    if (lfs_url.empty())
    {
        try {
            auto origin = repo.get_remote("origin");
            lfs_url = origin.url();
        } catch (...) {}
    }

    if (lfs_url.empty())
    {
        LOG_DBG << "LFS push URL not available, skipping LFS object upload.";
        return;
    }

    LOG_DBG << "Pushing LFS objects to: " << lfs_url;

    // 尝试通过 HTTP Batch API 推送 LFS 对象。
    auto lfs_ret = gitpp::lfs::push_lfs_objects_http(
        lfs_url, repo.path());

    if (!lfs_ret)
    {
        LOG_DBG << "LFS objects pushed successfully via HTTP batch API.";
        return;
    }

    LOG_DBG << "LFS HTTP push failed: " << *lfs_ret;
}

/**
 * @brief 执行推送到远程 Git 仓库。
 *
 * 获取 "origin" 远程仓库引用，配置推送选项（包括认证回调和证书校验），
 * 将本地 master 分支推送到远程。支持普通推送和强制推送（force push）两种模式。
 *
 * @param repo  Git 仓库对象。
 * @return int  EXIT_SUCCESS（0）表示推送成功，EXIT_FAILURE（1）表示失败。
 */
static int push_to_remote(gitpp::repo& repo)
{
    gitpp::remote remote = repo.get_remote("origin");

    // 初始化推送选项结构。
    git_push_options options;
    if (git_push_init_options(&options, GIT_PUSH_OPTIONS_VERSION) != 0)
    {
        LOG_DBG << "git_push_init_options, err: "
            << git_error_last()->message;
        return EXIT_FAILURE;
    }

    // 配置推送回调：凭证获取与证书校验。
    options.callbacks.credentials = cred_acquire_cb;
    options.callbacks.payload = nullptr;
    options.callbacks.certificate_check = certificate_check_cb;

    // 执行推送操作
    // 默认 refspec: refs/heads/master
    // 强制推送 refspec: +refs/heads/master:refs/heads/master（前导 '+' 表示 force）
    char* refspec[1] = { (char*)"refs/heads/master" };
    char* force_refspec[1] = { (char*)"+refs/heads/master:refs/heads/master" };
    git_strarray arr = {
        .strings = refspec,
        .count = 1
    };

    // 启用强制推送时，使用带 '+' 前缀的 refspec。
    if (global_force_push)
        arr.strings = force_refspec;

    if (git_remote_push(remote.native(), &arr, &options) != 0)
    {
        LOG_DBG << "git_remote_push, err: "
            << git_error_last()->message;
        return EXIT_FAILURE;
    }

    LOG_DBG << "Successfully pushed to remote repository";
    return EXIT_SUCCESS;
}

/**
 * @brief 执行完整的 Git 自动工作流程：暂存 → 提交 → 推送。
 *
 * 按顺序依次执行：
 * 1. 获取仓库索引（index）和工作目录状态列表（status_list）。
 * 2. 调用 process_status_entries() 自动暂存或移除文件变更。
 * 3. 调用 write_tree_and_commit() 从暂存区创建提交。
 * 4. 若无文件变更且非强制推送模式，则提前返回。
 * 5. 推送 LFS 对象（如果启用 LFS 支持）。
 * 6. 推送到远程 origin 仓库。
 *
 * @param repo  Git 仓库对象。
 * @return int  EXIT_SUCCESS（0）表示成功，EXIT_FAILURE（1）表示失败。
 */
int gitwork(gitpp::repo& repo)
{
    // 获取仓库索引和工作目录状态。
    gitpp::index index = repo.get_index();
    gitpp::status_list status = repo.new_status_list();

    // 步骤1：处理所有工作目录变更，暂存到索引。
    size_t commit_count = 0;
    if (process_status_entries(index, status, commit_count) != EXIT_SUCCESS)
        return EXIT_FAILURE;

    // 步骤2：从索引构建 tree 并创建提交。
    if (write_tree_and_commit(repo, index, commit_count) != EXIT_SUCCESS)
        return EXIT_FAILURE;

    // 步骤3：无变更且非强制推送时，无需继续。
    if (commit_count == 0 && !global_force_push)
        return EXIT_FAILURE;

    // 步骤4：推送 LFS 对象（如启用）。
    push_lfs_objects(repo);

    // 步骤5：推送到远程仓库。
    return push_to_remote(repo);
}

/**
 * @brief 确保指定路径是一个有效的 Git 仓库，如果不是则初始化创建。
 *
 * 如果目录尚不存在则自动创建，然后调用 gitpp::init_repo() 初始化
 * 一个新的 Git 仓库并配置远程仓库 URL。
 *
 * @param git_dir  目标目录路径。
 * @return int     EXIT_SUCCESS（0）表示仓库就绪，EXIT_FAILURE（1）表示失败。
 */
static int ensure_git_repository(const std::string& git_dir)
{
    // 已经是 Git 仓库，无需初始化。
    if (gitpp::is_git_repo(git_dir))
        return EXIT_SUCCESS;

    // 无远程 URL 时发出警告，但仍继续初始化本地仓库。
    if (global_git_remote_url.empty())
        LOG_WARN << "git remote url is empty, please set a remote url";

    boost::system::error_code ec;

    // 如果目录不存在则递归创建。
    if (!fs::exists(git_dir, ec))
    {
        fs::create_directories(git_dir, ec);
        if (ec)
        {
            LOG_ERR << "create git dir: " << git_dir
                << ", err: " << ec.message();
            return EXIT_FAILURE;
        }
    }

    // 初始化 Git 仓库并配置远程 URL。
    try
    {
        (void)gitpp::init_repo(git_dir, global_git_remote_url);
    }
    catch (const std::exception& e)
    {
        LOG_ERR << "init git repo: '" << git_dir
            << "' failure: " << e.what();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief 配置 Git LFS filter 和属性。
 *
 * 加载 .gitattributes 中已有的 LFS 模式，合并命令行指定的额外模式，
 * 然后将所有模式写入 .git/info/attributes，最后注册 LFS clean/smudge filter。
 *
 * @param repo  Git 仓库对象。
 */
static void setup_lfs_for_repository(gitpp::repo& repo)
{
    // 未启用 LFS 时直接返回。
    if (!global_enable_lfs)
        return;

    std::string git_dir_path = repo.path();

    // 从 .gitattributes 加载已有的 LFS 模式。
    auto lfs_patterns = gitpp::lfs::load_lfs_patterns(
        git_dir_path, repo.native());

    // 合并通过 --lfs_pattern 命令行参数指定的额外模式，避免重复。
    for (const auto& pat : global_lfs_patterns)
    {
        if (std::find(lfs_patterns.begin(),
            lfs_patterns.end(), pat) == lfs_patterns.end())
            lfs_patterns.push_back(pat);
    }

    // 将合并后的 LFS 模式写入 .git/info/attributes，使 filter=lfs 生效。
    int attr_ret = gitpp::lfs::write_lfs_attributes(
        git_dir_path, lfs_patterns);
    if (attr_ret != 0)
        LOG_WARN << "Failed to write LFS attributes to .git/info/attributes";

    // 注册 LFS clean/smudge filter 到 libgit2。
    int filter_ret = gitpp::lfs::register_lfs_filter(git_dir_path);
    if (filter_ret != 0)
        LOG_WARN << "Failed to register LFS filter (ret=" << filter_ret << ")";
    else
        LOG_DBG << "LFS filter registered successfully";
}

/**
 * @brief 输出当前 HEAD 提交的日志信息。
 *
 * 读取 HEAD 引用指向的最新提交，将其提交信息输出到调试日志。
 * 如果 HEAD 尚未指向任何提交（unborn），则跳过。
 *
 * @param repo  Git 仓库对象。
 */
static void log_head_commit_info(gitpp::repo& repo)
{
    // HEAD 未指向任何提交（新仓库），跳过。
    if (repo.is_head_unborn())
        return;

    try
    {
        gitpp::reference head = repo.head();
        gitpp::commit commit = repo.lookup_commit(head.target());
        LOG_DBG << "Commit: " << commit.message();
    }
    catch (const std::exception& e)
    {
        LOG_WARN << "lookup_commit, exception: " << e.what();
    }
}

/**
 * @brief Git 自动工作主循环（Boost.Asio 协程）。
 *
 * 此协程是 autogit 的核心逻辑，按以下流程循环执行：
 * 1. 确保 Git 仓库已初始化。
 * 2. 打开仓库，配置 LFS，记录 HEAD 信息。
 * 3. 创建文件系统监控器（watchman），监听工作目录变更。
 * 4. 循环：
 *    a. 执行 gitwork() 完成暂存→提交→推送。
 *    b. 等待文件系统变更事件（可取消）。
 *    c. 如有 check_interval，等待间隔时间（可取消）。
 * 5. 收到取消信号（SIGINT/SIGTERM）时正常退出。
 *
 * @param check_interval  两次仓库检查之间的间隔秒数（≤0 表示不等待）。
 * @param git_dir         被监控的 Git 仓库路径。
 * @param cancel_slot     Asio 取消槽，用于外部触发取消操作。
 * @return net::awaitable<int> 协程返回值，EXIT_SUCCESS 或 EXIT_FAILURE。
 */
net::awaitable<int> git_work_loop(int check_interval, const std::string& git_dir,
    net::cancellation_slot cancel_slot)
{
    auto executor = co_await net::this_coro::executor;

    // 步骤1：确保 Git 仓库已初始化。
    if (ensure_git_repository(git_dir) != EXIT_SUCCESS)
        co_return EXIT_FAILURE;

    try
    {
        // 步骤2：打开仓库并进行初始化配置。
        gitpp::repo repo(git_dir);

        LOG_DBG << "Open repo: " << git_dir
            << ", is_bare: " << repo.is_bare()
            << ", is_empty: " << repo.is_empty()
            << ", is_head_detached: " << repo.is_head_detached()
            << ", is_head_unborn: " << repo.is_head_unborn();

        setup_lfs_for_repository(repo);
        log_head_commit_info(repo);

        // 步骤3：创建文件系统监控器，监听目录变更。
        watchman::watcher monitor(executor, boost::filesystem::path(git_dir));

        // 步骤4：主循环 — 监听变更 → 执行 Git 操作。
        while (true)
        {
            // 4a. 执行一次 Git 工作流程（暂存/提交/推送）。
            try
            {
                gitwork(repo);
            }
            catch (const std::exception& e)
            {
                LOG_ERR << "gitwork, exception: " << e.what();
            }

            // 4b. 等待文件系统变更通知（可被取消信号中断）。
            {
                boost::system::error_code ec;
                auto result = co_await monitor.async_wait(
                    net::bind_cancellation_slot(cancel_slot,
                        net::redirect_error(net::use_awaitable, ec)));

                // 操作被取消（收到终止信号），正常退出循环。
                if (ec == boost::asio::error::operation_aborted)
                {
                    LOG_DBG << "git_work_loop cancelled normally";
                    break;
                }

                // 记录文件系统变更事件到调试日志。
                for (const auto& file : result)
                {
                    std::ostringstream oss;
                    oss << "CHG: " << (int)file.type_ << ", FILE: " << file.path_;
                    if (!file.new_path_.empty())
                        oss << " -> " << file.new_path_;
                    LOG_DBG << oss.str();
                }
            }

            // 4c. 如果配置了检查间隔，等待指定时间后再进入下一轮。
            if (check_interval > 0)
            {
                boost::system::error_code ec;
                net::steady_timer timer(executor);
                timer.expires_after(std::chrono::seconds(check_interval));
                co_await timer.async_wait(
                    net::bind_cancellation_slot(cancel_slot,
                        net::redirect_error(net::use_awaitable, ec)));

                if (ec == boost::asio::error::operation_aborted)
                {
                    LOG_DBG << "git_work_loop cancelled normally";
                    break;
                }
            }
        }

        LOG_DBG << "git_work_loop exited normally";
    }
    catch (std::exception& e)
    {
        LOG_DBG << "git loop thread stopped, exception: " << e.what();
    }

    co_return EXIT_SUCCESS;
}

/**
 * @brief 构建命令行选项描述器。
 *
 * 使用 Boost.ProgramOptions 定义所有支持的命令行参数和配置文件选项，
 * 包括通用选项、Git 选项、HTTP/SSH 认证选项以及 Git LFS 选项。
 *
 * @param git_dir          输出参数，Git 仓库路径。
 * @param log_dir          输出参数，日志文件目录。
 * @param config           输出参数，配置文件路径。
 * @param check_interval   输出参数，仓库检查间隔（秒）。
 * @param quiet            输出参数，是否静默模式。
 * @return po::options_description  配置好的选项描述对象。
 */
static po::options_description build_options_description(
    std::string& git_dir,
    std::string& log_dir,
    std::string& config,
    int& check_interval,
    bool& quiet)
{
    po::options_description desc("Options");

    // ═══════════════════════════════════════════
    // 通用选项
    // ═══════════════════════════════════════════
    desc.add_options()
        ("help,h", "Display help information.")
        ("config,c", po::value<std::string>(&config)->default_value("autogit.conf"), "Path to the configuration file.")
        ("quiet", po::value<bool>(&quiet)->default_value(false), "Mute all logging.")
        ("log_dir", po::value<std::string>(&log_dir)->value_name("path"), "Specify directory for log files.")
        ("check_interval", po::value<int>(&check_interval)->default_value(60), "Time interval (in seconds) between Git repository checks.");

    // ═══════════════════════════════════════════
    // Git 特定选项
    // ═══════════════════════════════════════════
    desc.add_options()
        ("repository", po::value<std::string>(&git_dir)->value_name("repository"), "Specify the Git repository location.")
        ("commit_msg", po::value<std::string>(&global_commit_message)->default_value(""), "Set a custom commit message, if empty will auto-generate.")
        ("force_push", po::value<bool>(&global_force_push)->default_value(false), "Enable force push for Git commits.")
        ("git_author", po::value<std::string>(&global_git_author)->default_value("autogit"), "Name to be used for Git commit authorship.")
        ("git_email", po::value<std::string>(&global_git_email)->default_value("autogit@localhost"), "Email to be associated with Git commit authorship.")
        ("git_remote_url", po::value<std::string>(&global_git_remote_url)->default_value(""), "URL for the remote Git repository.");

    // ═══════════════════════════════════════════
    // HTTP 认证选项
    // ═══════════════════════════════════════════
    desc.add_options()
        ("http_username", po::value<std::string>(&global_http_username)->default_value(""), "Username for HTTP authentication.")
        ("http_password", po::value<std::string>(&global_http_password)->default_value(""), "Password for HTTP authentication.");

    // ═══════════════════════════════════════════
    // SSH 认证选项
    // ═══════════════════════════════════════════
    desc.add_options()
        ("ssh_pubkey", po::value<std::string>(&global_ssh_pubkey)->default_value(""), "Path to the SSH public key for authentication.")
        ("ssh_privkey", po::value<std::string>(&global_ssh_privkey)->default_value(""), "Path to the SSH private key for authentication.")
        ("ssh_passphrase", po::value<std::string>(&global_ssh_passphrase)->default_value(""), "Passphrase for the SSH key.");

    // ═══════════════════════════════════════════
    // Git LFS 选项
    // ═══════════════════════════════════════════
    desc.add_options()
        ("lfs", po::value<bool>(&global_enable_lfs)->default_value(false), "Enable Git LFS support. When enabled, files matching LFS patterns in .gitattributes will be stored as LFS pointers.")
        ("lfs_pattern", po::value<std::vector<std::string>>(&global_lfs_patterns)->multitoken(), "Additional LFS file patterns (glob) to track, e.g. --lfs_pattern '*.psd' --lfs_pattern '*.zip'. These supplement patterns from .gitattributes.")
        ("lfs_push_url", po::value<std::string>(&global_lfs_push_url)->default_value(""), "LFS remote URL for pushing objects. Overrides the url setting in .lfsconfig. If empty, the repository remote origin URL is used.");

    return desc;
}

/**
 * @brief 解析命令行参数和配置文件。
 *
 * 首先使用 Boost.ProgramOptions 解析命令行参数，
 * 然后如果配置文件存在，则从中加载并合并配置项。
 * 配置文件格式与命令行参数兼容（每行一个选项）。
 *
 * @param argc  命令行参数个数。
 * @param argv  命令行参数数组。
 * @param desc  选项描述器（定义了所有合法选项）。
 * @param vm    输出参数，解析后的变量映射表。
 * @param config  配置文件路径。
 * @return true  解析成功。
 * @return false 解析失败（如配置文件无法打开）。
 */
static bool parse_command_line(
    int argc, char** argv,
    po::options_description& desc,
    po::variables_map& vm,
    const std::string& config)
{
    // 解析命令行参数。
    po::store(
        po::command_line_parser(argc, argv)
        .options(desc)
        .style(po::command_line_style::unix_style
            | po::command_line_style::allow_long_disguise)
        .run()
        , vm);
    po::notify(vm);

    // 如果配置文件存在，解析并合并配置项。
    if (!config.empty() && boost::filesystem::exists(config))
    {
        std::ifstream ifs(config);
        if (!ifs)
        {
            LOG_ERR << "can not open config file: " << config;
            return false;
        }
        po::store(po::parse_config_file(ifs, desc), vm);
        po::notify(vm);
    }

    return true;
}

/**
 * @brief 注册需要捕获的终止信号。
 *
 * 将 SIGINT（Ctrl+C）、SIGTERM（kill）和 SIGQUIT 注册到
 * Boost.Asio signal_set，以便优雅关闭程序。
 *
 * @param signal_set  Asio 信号集对象（已绑定到 io_context）。
 */
static void setup_termination_signals(net::signal_set& signal_set)
{
    signal_set.add(SIGINT);     // Ctrl+C 中断信号。
    signal_set.add(SIGTERM);    // 终止信号（kill 默认发送）。
#if defined(SIGQUIT)
    signal_set.add(SIGQUIT);    // 退出信号（Ctrl+\）。
#endif
}

/**
 * @brief 程序入口协程。
 *
 * 负责完整的初始化流程：
 * 1. 构建并解析命令行参数和配置文件。
 * 2. 初始化日志系统。
 * 3. 设置终止信号处理（SIGINT/SIGTERM/SIGQUIT）。
 * 4. 进入 git_work_loop 主循环。
 * 5. 退出时清理 LFS filter 注册。
 *
 * @param argc  命令行参数个数。
 * @param argv  命令行参数数组。
 * @return net::awaitable<int> 协程返回值，EXIT_SUCCESS 或 EXIT_FAILURE。
 */
net::awaitable<int> co_main(int argc, char** argv)
{
    std::string git_dir;
    std::string log_dir;
    std::string config;
    int check_interval;
    bool quiet = false;

    // 步骤1：构建并解析命令行选项。
    po::options_description desc = build_options_description(
        git_dir, log_dir, config, check_interval, quiet);
    po::variables_map vm;

    if (!parse_command_line(argc, argv, desc, vm, config))
        co_return EXIT_FAILURE;

    // 静默模式下关闭控制台日志，仅保留文件日志。
    if (quiet)
    {
        util::toggle_logging();
        util::toggle_write_logging(true);
    }

    // 帮助输出：打印选项说明并退出。
    if (vm.count("help") || argc == 1)
    {
        std::cerr << desc;
        co_return EXIT_SUCCESS;
    }

    // 步骤2：初始化日志系统。
    util::init_logging(log_dir);

    // 步骤3：设置终止信号处理。
    net::signal_set terminator_signal(co_await net::this_coro::executor);
    setup_termination_signals(terminator_signal);

    LOG_DBG << "Running...";

    net::cancellation_signal cancel_signal;

    // 当接收到终止信号时，触发 Asio 取消操作，
    // 让 git_work_loop 中的异步操作被中断并正常退出。
    terminator_signal.async_wait([&cancel_signal](boost::system::error_code ec, int) {
        if (!ec)
            cancel_signal.emit(net::cancellation_type::terminal);
    });

    // 步骤4：进入 Git 自动工作主循环。
    co_await git_work_loop(check_interval, git_dir, cancel_signal.slot());

    // 清理信号注册。
    terminator_signal.clear();

    // 步骤5：注销 LFS filter 以释放 libgit2 资源。
    gitpp::lfs::unregister_lfs_filter();

    co_return EXIT_SUCCESS;
}

/**
 * @brief 程序主入口。
 *
 * 创建 Boost.Asio io_context，通过 co_spawn 启动 co_main 协程，
 * 然后运行事件循环直到协程完成。
 *
 * @param argc  命令行参数个数。
 * @param argv  命令行参数数组。
 * @return int  程序退出码，EXIT_SUCCESS（0）或 EXIT_FAILURE（1）。
 */
int main(int argc, char** argv)
{
	int main_return;

	// 创建 io_context 并派发 co_main 协程。
	net::io_context ioc;
	net::co_spawn(ioc,
		co_main(argc, argv),
		[&](std::exception_ptr e, int ret)
		{
			if (e)
				std::rethrow_exception(e);
			main_return = ret;
			ioc.stop();
		});
	ioc.run();

	return main_return;
}
