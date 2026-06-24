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

std::string global_commit_message;
bool global_force_push;

std::string global_http_username;
std::string global_http_password;

std::string global_ssh_pubkey;
std::string global_ssh_privkey;
std::string global_ssh_passphrase;

std::string global_git_author;
std::string global_git_email;

std::string global_git_remote_url;

// Git LFS 配置。
bool global_enable_lfs = false;
std::vector<std::string> global_lfs_patterns;
std::string global_lfs_push_url;

int certificate_check_cb(git_cert *cert,
	int valid,
	const char *host,
	void *payload)
{
    return 1; // 始终接受证书
}

const char* get_home_dir(void)
{
#ifdef _WIN32
	return getenv("USERPROFILE");
#else
	return getenv("HOME");
#endif
}

// 构建 SSH 密钥凭证
static int build_ssh_credential(git_cred** cred,
    const char* username_from_url)
{
    auto default_sshdir =
        std::string(get_home_dir()) +
#ifdef WIN32
        "\\.ssh\\";
#else
        "/.ssh/";
#endif // WIN32（Windows 平台）

    const char* private_key = nullptr;
    const char* public_key = nullptr;

    if (fs::exists(global_ssh_privkey)) {
        private_key = global_ssh_privkey.c_str();
    } else {
        default_sshdir += (global_ssh_privkey.empty() ?
            "idrsa" : global_ssh_privkey);
        private_key = default_sshdir.c_str();
    }

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

// 构建 HTTP 基本认证凭证
static int build_http_credential(git_cred** cred)
{
    if (global_http_username.empty())
        return GIT_EAUTH;

    return git_cred_userpass_plaintext_new(cred,
        global_http_username.c_str(),
        global_http_password.c_str());
}

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

// 处理工作目录状态条目，将变更添加到/从索引中移除
static int process_status_entries(
    gitpp::index& index,
    gitpp::status_list& status,
    size_t& commit_count)
{
    commit_count = 0;
    for (const git_status_entry* entry : status)
    {
        const char* old_file_path = entry->index_to_workdir->old_file.path;
        auto handle = index.native();
        int ret = 0;

        switch (entry->status & 0xfffffff0)
        {
        case GIT_STATUS_WT_NEW:
            ret = git_index_add_bypath(handle, old_file_path);
            commit_count++;
            LOG_DBG << "Untracked file: "
                << entry->index_to_workdir->old_file.path;
            break;

        case GIT_STATUS_WT_MODIFIED:
            ret = git_index_add_bypath(handle, old_file_path);
            commit_count++;
            LOG_DBG << "modify file: "
                << entry->index_to_workdir->old_file.path;
            break;

        case GIT_STATUS_WT_DELETED:
            ret = git_index_remove_bypath(handle, old_file_path);
            commit_count++;
            LOG_DBG << "delete file: "
                << entry->index_to_workdir->old_file.path;
            break;

        case GIT_STATUS_WT_TYPECHANGE:
            ret = git_index_add_bypath(handle, old_file_path);
            commit_count++;
            LOG_DBG << "typechg file: "
                << entry->index_to_workdir->old_file.path;
            break;

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

// 从索引构建树并创建提交
static int write_tree_and_commit(
    gitpp::repo& repo,
    gitpp::index& index,
    size_t commit_count)
{
    if (commit_count == 0)
        return EXIT_SUCCESS;

    if (git_index_write(index.native()) != 0)
    {
        LOG_DBG << "git_index_write, err: "
            << git_error_last()->message;
        return EXIT_FAILURE;
    }

    gitpp::tree tree = index.write_tree();

    // 构建提交信息，格式: [commit_msg] {date-time} {author} {files_count} files changed
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

// 推送 LFS 对象到远程服务器
static void push_lfs_objects(gitpp::repo& repo)
{
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

    int lfs_ret = gitpp::lfs::push_lfs_objects_http(
        lfs_url, repo.path());

    if (lfs_ret == 0)
    {
        LOG_DBG << "LFS objects pushed successfully via HTTP batch API.";
        return;
    }

    LOG_DBG << "LFS HTTP push failed (ret=" << lfs_ret
        << "), trying git-lfs fallback.";

    std::string lfs_cmd = "git -C \"" + repo.workdir()
        + "\" lfs push --all origin 2>/dev/null";
    int fallback_ret = std::system(lfs_cmd.c_str());
    if (fallback_ret != 0)
        LOG_DBG << "git-lfs fallback also failed, skipping LFS upload.";
}

// 执行推送到远程仓库
static int push_to_remote(gitpp::repo& repo)
{
    gitpp::remote remote = repo.get_remote("origin");

    git_push_options options;
    if (git_push_init_options(&options, GIT_PUSH_OPTIONS_VERSION) != 0)
    {
        LOG_DBG << "git_push_init_options, err: "
            << git_error_last()->message;
        return EXIT_FAILURE;
    }

    options.callbacks.credentials = cred_acquire_cb;
    options.callbacks.payload = nullptr;
    options.callbacks.certificate_check = certificate_check_cb;

    // 执行推送操作
    char* refspec[1] = { (char*)"refs/heads/master" };
    char* force_refspec[1] = { (char*)"+refs/heads/master:refs/heads/master" };
    git_strarray arr = {
        .strings = refspec,
        .count = 1
    };

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

int gitwork(gitpp::repo& repo)
{
    gitpp::index index = repo.get_index();
    gitpp::status_list status = repo.new_status_list();

    size_t commit_count = 0;
    if (process_status_entries(index, status, commit_count) != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (write_tree_and_commit(repo, index, commit_count) != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (commit_count == 0 && !global_force_push)
        return EXIT_FAILURE;

    push_lfs_objects(repo);

    return push_to_remote(repo);
}

// 检查路径是否为有效的 Git 仓库，如果不是则创建
static int ensure_git_repository(const std::string& git_dir)
{
    if (gitpp::is_git_repo(git_dir))
        return EXIT_SUCCESS;

    if (global_git_remote_url.empty())
        LOG_WARN << "git remote url is empty, please set a remote url";

    boost::system::error_code ec;

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

// 配置 Git LFS filter 和属性
static void setup_lfs_for_repository(gitpp::repo& repo)
{
    if (!global_enable_lfs)
        return;

    std::string git_dir_path = repo.path();
    auto lfs_patterns = gitpp::lfs::load_lfs_patterns(
        git_dir_path, repo.native());

    // 合并通过 --lfs_pattern 命令行参数指定的模式。
    for (const auto& pat : global_lfs_patterns)
    {
        if (std::find(lfs_patterns.begin(),
            lfs_patterns.end(), pat) == lfs_patterns.end())
            lfs_patterns.push_back(pat);
    }

    // 将模式写入 .git/info/attributes，使 filter=lfs 生效。
    int attr_ret = gitpp::lfs::write_lfs_attributes(
        git_dir_path, lfs_patterns);
    if (attr_ret != 0)
        LOG_WARN << "Failed to write LFS attributes to .git/info/attributes";

    // 注册 LFS filter。
    int filter_ret = gitpp::lfs::register_lfs_filter(git_dir_path);
    if (filter_ret != 0)
        LOG_WARN << "Failed to register LFS filter (ret=" << filter_ret << ")";
    else
        LOG_DBG << "LFS filter registered successfully";
}

// 输出 HEAD 提交信息
static void log_head_commit_info(gitpp::repo& repo)
{
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

net::awaitable<int> git_work_loop(int check_interval, const std::string& git_dir,
    net::cancellation_slot cancel_slot)
{
    auto executor = co_await net::this_coro::executor;

    if (ensure_git_repository(git_dir) != EXIT_SUCCESS)
        co_return EXIT_FAILURE;

    try
    {
        gitpp::repo repo(git_dir);

        LOG_DBG << "Open repo: " << git_dir
            << ", is_bare: " << repo.is_bare()
            << ", is_empty: " << repo.is_empty()
            << ", is_head_detached: " << repo.is_head_detached()
            << ", is_head_unborn: " << repo.is_head_unborn();

        setup_lfs_for_repository(repo);
        log_head_commit_info(repo);

        watchman::watcher monitor(executor, boost::filesystem::path(git_dir));

        while (true)
        {
            try
            {
                gitwork(repo);
            }
            catch (const std::exception& e)
            {
                LOG_ERR << "gitwork, exception: " << e.what();
            }

            {
                boost::system::error_code ec;
                auto result = co_await monitor.async_wait(
                    net::bind_cancellation_slot(cancel_slot,
                        net::redirect_error(net::use_awaitable, ec)));

                if (ec == boost::asio::error::operation_aborted)
                {
                    LOG_DBG << "git_work_loop cancelled normally";
                    break;
                }

                for (const auto& file : result)
                {
                    std::ostringstream oss;
                    oss << "CHG: " << (int)file.type_ << ", FILE: " << file.path_;
                    if (!file.new_path_.empty())
                        oss << " -> " << file.new_path_;
                    LOG_DBG << oss.str();
                }
            }

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

// 构建命令行选项描述
static po::options_description build_options_description(
    std::string& git_dir,
    std::string& log_dir,
    std::string& config,
    int& check_interval,
    bool& quiet)
{
    po::options_description desc("Options");

    // 通用选项
    desc.add_options()
        ("help,h", "Display help information.")
        ("config,c", po::value<std::string>(&config)->default_value("autogit.conf"), "Path to the configuration file.")
        ("quiet", po::value<bool>(&quiet)->default_value(false), "Mute all logging.")
        ("log_dir", po::value<std::string>(&log_dir)->value_name("path"), "Specify directory for log files.")
        ("check_interval", po::value<int>(&check_interval)->default_value(60), "Time interval (in seconds) between Git repository checks.");

    // Git 特定选项
    desc.add_options()
        ("repository", po::value<std::string>(&git_dir)->value_name("repository"), "Specify the Git repository location.")
        ("commit_msg", po::value<std::string>(&global_commit_message)->default_value(""), "Set a custom commit message, if empty will auto-generate.")
        ("force_push", po::value<bool>(&global_force_push)->default_value(false), "Enable force push for Git commits.")
        ("git_author", po::value<std::string>(&global_git_author)->default_value("autogit"), "Name to be used for Git commit authorship.")
        ("git_email", po::value<std::string>(&global_git_email)->default_value("autogit@localhost"), "Email to be associated with Git commit authorship.")
        ("git_remote_url", po::value<std::string>(&global_git_remote_url)->default_value(""), "URL for the remote Git repository.");

    // HTTP 认证选项
    desc.add_options()
        ("http_username", po::value<std::string>(&global_http_username)->default_value(""), "Username for HTTP authentication.")
        ("http_password", po::value<std::string>(&global_http_password)->default_value(""), "Password for HTTP authentication.");

    // SSH 认证选项
    desc.add_options()
        ("ssh_pubkey", po::value<std::string>(&global_ssh_pubkey)->default_value(""), "Path to the SSH public key for authentication.")
        ("ssh_privkey", po::value<std::string>(&global_ssh_privkey)->default_value(""), "Path to the SSH private key for authentication.")
        ("ssh_passphrase", po::value<std::string>(&global_ssh_passphrase)->default_value(""), "Passphrase for the SSH key.");

    // Git LFS 选项
    desc.add_options()
        ("lfs", po::value<bool>(&global_enable_lfs)->default_value(false), "Enable Git LFS support. When enabled, files matching LFS patterns in .gitattributes will be stored as LFS pointers.")
        ("lfs_pattern", po::value<std::vector<std::string>>(&global_lfs_patterns)->multitoken(), "Additional LFS file patterns (glob) to track, e.g. --lfs_pattern '*.psd' --lfs_pattern '*.zip'. These supplement patterns from .gitattributes.")
        ("lfs_push_url", po::value<std::string>(&global_lfs_push_url)->default_value(""), "LFS remote URL for pushing objects. Overrides the url setting in .lfsconfig. If empty, the repository remote origin URL is used.");

    return desc;
}

// 解析命令行和配置文件
static bool parse_command_line(
    int argc, char** argv,
    po::options_description& desc,
    po::variables_map& vm,
    const std::string& config)
{
    po::store(
        po::command_line_parser(argc, argv)
        .options(desc)
        .style(po::command_line_style::unix_style
            | po::command_line_style::allow_long_disguise)
        .run()
        , vm);
    po::notify(vm);

    // 使用 boost::program_options 库解析配置文件参数
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

// 设置终止信号处理
static void setup_termination_signals(net::signal_set& signal_set)
{
    signal_set.add(SIGINT);
    signal_set.add(SIGTERM);
#if defined(SIGQUIT)
    signal_set.add(SIGQUIT);
#endif
}

net::awaitable<int> co_main(int argc, char** argv)
{
    std::string git_dir;
    std::string log_dir;
    std::string config;
    int check_interval;
    bool quiet = false;

    // 构建并解析命令行选项
    po::options_description desc = build_options_description(
        git_dir, log_dir, config, check_interval, quiet);
    po::variables_map vm;

    if (!parse_command_line(argc, argv, desc, vm, config))
        co_return EXIT_FAILURE;

    if (quiet)
    {
        util::toggle_logging();
        util::toggle_write_logging(true);
    }

    // 帮助输出.
    if (vm.count("help") || argc == 1)
    {
        std::cerr << desc;
        co_return EXIT_SUCCESS;
    }

    util::init_logging(log_dir);

    net::signal_set terminator_signal(co_await net::this_coro::executor);
    setup_termination_signals(terminator_signal);

    LOG_DBG << "Running...";

    net::cancellation_signal cancel_signal;

    // 当接收到终止信号时，触发取消操作，让 git_work_loop 正常退出。
    terminator_signal.async_wait([&cancel_signal](boost::system::error_code ec, int) {
        if (!ec)
            cancel_signal.emit(net::cancellation_type::terminal);
    });

    co_await git_work_loop(check_interval, git_dir, cancel_signal.slot());

    terminator_signal.clear();

    // 注销 LFS filter。
    gitpp::lfs::unregister_lfs_filter();

    co_return EXIT_SUCCESS;
}

int main(int argc, char** argv)
{
	int main_return;

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
