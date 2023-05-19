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

#include "autogit/logging.hpp"
#include "autogit/scoped_exit.hpp"
#include "autogit/strutil.hpp"

#include "watchman/watchman.hpp"

#include "gitpp/gitpp.hpp"

#include <signal.h>

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

namespace net = boost::asio;

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
# include <Windows.h>
// # define open _open
// # define read _read
// # define close _close
// # define ssize_t int
// # define sleep(a) Sleep(a * 1000)
#else
# include <unistd.h>
# include <sys/prctl.h>
# include <sys/resource.h>
#endif

#ifndef PRIuZ
/* Define the printf format specifier to use for size_t output */
#if defined(_MSC_VER) || defined(__MINGW32__)
#	define PRIuZ "Iu"
#else
#	define PRIuZ "zu"
#endif
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

int certificate_check_cb(git_cert *cert,
	int valid,
	const char *host,
	void *payload)
{
    return 1; // Always accept the certificate
}

const char* get_home_dir(void)
{
#ifdef _WIN32
	return getenv("USERPROFILE");
#else
	return getenv("HOME");
#endif
}

int cred_acquire_cb(git_cred** cred,
	const char* url,
	const char* username_from_url,
	unsigned int allowed_types,
	void* payload)
{
	if (allowed_types & GIT_CREDTYPE_SSH_KEY)
	{
		auto default_sshdir =
			std::string(get_home_dir()) +
#ifdef _MSC_VER
			"\\.ssh\\";
#else
			"/.ssh/";
#endif // _MSC_VER

		auto public_key = default_sshdir +
			(global_ssh_pubkey.empty() ?
				"id_rsa.pub" : global_ssh_pubkey);

		auto private_key = default_sshdir +
			(global_ssh_pubkey.empty() ?
				"id_rsa" : global_ssh_privkey);

		const char* passphrase =
			global_ssh_passphrase.empty() ?
				nullptr : global_ssh_passphrase.c_str();

		return git_cred_ssh_key_new(
			cred,
			username_from_url,
			public_key.c_str(),		// 这是公钥的路径.
			private_key.c_str(),	// 这是私钥的路径.
			passphrase				// 如果你的私钥有密码.
		);
	}
	else if (allowed_types & GIT_CREDTYPE_USERPASS_PLAINTEXT)
	{
		if (global_http_username.empty())
			return GIT_EAUTH;

		return git_cred_userpass_plaintext_new(cred,
			global_http_username.c_str(),
			global_http_password.c_str());
	}

	return GIT_EAUTH;
}

int gitwork(gitpp::repo& repo)
{
	gitpp::index index = repo.get_index();
	gitpp::status_list status = repo.new_status_list();

	size_t commit_count = 0;
	for (const git_status_entry* entry : status)
	{
		auto& old_file_path = entry->index_to_workdir->old_file.path;
		auto handle = index.native_handle();
		int ret = 0;

		switch (entry->status & 0xfffffff0)
		{
		case GIT_STATUS_WT_NEW:
		{
			ret = git_index_add_bypath(handle, old_file_path);

			commit_count++;
			LOG_DBG << "Untracked file: "
				<< entry->index_to_workdir->old_file.path;
		}
		break;
		case GIT_STATUS_WT_MODIFIED:
		{
			ret = git_index_add_bypath(handle, old_file_path);

			commit_count++;
			LOG_DBG << "modify file: "
				<< entry->index_to_workdir->old_file.path;
		}
		break;
		case GIT_STATUS_WT_DELETED:
		{
			ret = git_index_remove_bypath(handle, old_file_path);

			commit_count++;
			LOG_DBG << "delete file: "
				<< entry->index_to_workdir->old_file.path;
		}
		break;
		case GIT_STATUS_WT_TYPECHANGE:
		{
			ret = git_index_add_bypath(handle, old_file_path);

			commit_count++;
			LOG_DBG << "typechg file: "
				<< entry->index_to_workdir->old_file.path;
		}
		break;
		case GIT_STATUS_WT_RENAMED:
		{
			LOG_DBG << "rename file: "
				<< entry->index_to_workdir->old_file.path
				<< " to "
				<< entry->index_to_workdir->new_file.path;

			ret = git_index_add_bypath(handle, old_file_path);
			commit_count++;
		}
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

	if (commit_count > 0)
	{
		if (git_index_write(index.native_handle()) != 0)
		{
			LOG_DBG << "git_index_write, err: "
				<< git_error_last()->message;

			return EXIT_FAILURE;
		}

		gitpp::tree tree = index.write_tree();

		// 当一个新仓库刚创建时 HEAD 并没有指向一个有效的 Commit, 这时
		// 强行创建一个 Commit 交将 HEAD 指向这个 Initial Commit.
		auto head = repo.head();
		if (head == gitpp::reference((git_reference*)nullptr))
		{
			git_oid commit_id;
			gitpp::signature signature(global_git_author, global_git_email);
			git_commit_create_v(&commit_id,
				repo.native_handle(),
				"HEAD",
				signature.native_handle(),
				signature.native_handle(),
				nullptr,
				global_commit_message.c_str(),
				tree.native_handle(),
				0);
		}
		else
		{
			// 获取当前的 HEAD 提交作为父提交.
			gitpp::oid parent_id = head.target();
			gitpp::commit parent = repo.lookup_commit(parent_id);

			// 创建一个新的签名.
			gitpp::signature signature(global_git_author, global_git_email);

			// 从树对象创建一个新的提交.
			repo.create_commit("HEAD",
				signature,
				signature,
				global_commit_message,
				tree,
				parent);
		}
	}
	else if (!global_force_push)
	{
		return EXIT_FAILURE;
	}

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

	if (git_remote_push(remote.native_handle(), &arr, &options) != 0)
	{
		LOG_DBG << "git_remote_push, err: "
			<< git_error_last()->message;

		// 处理错误
		return EXIT_FAILURE;
	}

	LOG_DBG << "Successfully pushed to remote repository";

	return EXIT_SUCCESS;
}

net::awaitable<int> git_work_loop(int check_interval, const std::string& git_dir)
{
	auto executor = co_await net::this_coro::executor;

	// 判断给的路径是否是一个已经存在的仓库
	// 如果不是则创建 git 仓库, 并设置仓库
	// 的 remote url.
	if (!gitpp::is_git_repo(git_dir))
	{
		if (global_git_remote_url.empty())
			LOG_WARN << "git remote url is empty, please set a remote url";

		gitpp::init_git_repo(git_dir, global_git_remote_url);
	}

	gitpp::repo repo(git_dir);
	watchman::watcher monitor(executor, boost::filesystem::path(git_dir));

	try
	{
		while (true)
		{
			try
			{
				gitwork(repo);
			}
			catch (const std::exception& e)
			{
				LOG_ERR << "gitwork: " << e.what();
			}

			auto result = co_await monitor.async_wait(net::use_awaitable);
			for (const auto& file : result)
			{
				std::ostringstream oss;

				oss << "CHG: " << (int)file.type_ << ", FILE: " << file.path_;
				if (!file.new_path_.empty())
					oss << " -> " << file.new_path_;

				LOG_DBG << oss.str();
			}

			if (check_interval > 0)
			{
				net::steady_timer timer(executor);

				timer.expires_from_now(std::chrono::seconds(check_interval));
				co_await timer.async_wait(net::use_awaitable);
			}
		}
	}
	catch (std::exception&)
	{
		LOG_DBG << "git loop thread stopped";
	}

	co_return EXIT_SUCCESS;
}

net::awaitable<int> co_main(int argc, char** argv)
{
	std::string git_dir;
	std::string log_dir;

	int check_interval;
	bool quiet = false;

	// 解析命令行.
	po::options_description desc("Options");
	desc.add_options()
		("help,h", "Help message.")
		("repository", po::value<std::string>(&git_dir)->value_name("repository"), "Git repository path.")
		("commit_msg", po::value<std::string>(&global_commit_message)->default_value("Commit by autogit"), "Git commit message.")
		("force_push", po::value<bool>(&global_force_push)->default_value(false), "Git force push.")

		("http_username", po::value<std::string>(&global_http_username)->default_value(""), "Username for HTTP auth.")
		("http_password", po::value<std::string>(&global_http_password)->default_value(""), "Password for HTTP auth.")

		("ssh_pubkey", po::value<std::string>(&global_ssh_pubkey)->default_value(""), "Public key for SSH auth.")
		("ssh_privkey", po::value<std::string>(&global_ssh_privkey)->default_value(""), "Private key for SSH auth.")
		("ssh_passphrase", po::value<std::string>(&global_ssh_passphrase)->default_value(""), "SSH key passphrase.")

		("git_author", po::value<std::string>(&global_git_author)->default_value(""), "Author name for Git commit.")
		("git_email", po::value<std::string>(&global_git_email)->default_value(""), "Author email for Git commit.")

		("git_remote_url", po::value<std::string>(&global_git_remote_url)->default_value(""), "Git remote url.")

		("check_interval", po::value<int>(&check_interval)->default_value(60), "Interval for Git repo checks.")
		("quiet", po::value<bool>(&quiet)->default_value(false), "Turn off logging.")
		("log_dir", po::value<std::string>(&log_dir)->value_name("path"), "Path for log files.")
		;

	po::variables_map vm;
	po::store(
		po::command_line_parser(argc, argv)
		.options(desc)
		.style(po::command_line_style::unix_style
			| po::command_line_style::allow_long_disguise)
		.run()
		, vm);
	po::notify(vm);

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
	terminator_signal.add(SIGINT);
	terminator_signal.add(SIGTERM);

#if defined(SIGQUIT)
	terminator_signal.add(SIGQUIT);
#endif // defined(SIGQUIT)

	LOG_DBG << "Running...";

	using namespace net::experimental::awaitable_operators;

	// 处理中止信号.
	co_await(
		git_work_loop(check_interval, git_dir)
			|| terminator_signal.async_wait(net::use_awaitable)
	);

	terminator_signal.clear();

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
