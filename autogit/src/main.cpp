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
#include "autogit/coroyield.hpp"
#include "autogit/time_clock.hpp"
#include "dirmon/dirmon.hpp"

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
# define open _open
# define read _read
# define close _close
# define ssize_t int
# define sleep(a) Sleep(a * 1000)
#else
# include <unistd.h>
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

std::string global_http_username;
std::string global_http_password;

std::string global_ssh_pubkey;
std::string global_ssh_privkey;
std::string global_ssh_passphrase;

std::string global_git_author;
std::string global_git_email;

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

int gitwork(git_repository* repo)
{
	git_index* index = nullptr;
	if (git_repository_index(&index, repo) != 0)
	{
		LOG_DBG << "git_repository_index, err: "
			<< git_error_last()->message;

		return EXIT_FAILURE;
	}
	scoped_exit gindex_free([&index]() mutable
		{
			git_index_free(index);
		});

	git_status_list* status = nullptr;
	if (git_status_list_new(&status, repo, nullptr) != 0)
	{
		LOG_DBG << "git_status_list_new, err: "
			<< git_error_last()->message;

		return EXIT_FAILURE;
	}
	scoped_exit gstatus_list_free([&status]() mutable
		{
			git_status_list_free(status);
		});

	size_t commit_obj = 0;
	size_t status_count = git_status_list_entrycount(status);
	for (size_t i = 0; i < status_count; ++i)
	{
		const git_status_entry* entry = git_status_byindex(status, i);
		int ret = 0;

		switch (entry->status & 0xfffffff0)
		{
		case GIT_STATUS_WT_NEW:
		{
			ret = git_index_add_bypath(index, entry->index_to_workdir->old_file.path);
			commit_obj++;
			LOG_DBG << "Untracked file: "
				<< entry->index_to_workdir->old_file.path;
		}
		break;
		case GIT_STATUS_WT_MODIFIED:
		{
			ret = git_index_add_bypath(index, entry->index_to_workdir->old_file.path);
			commit_obj++;
			LOG_DBG << "modify file: "
				<< entry->index_to_workdir->old_file.path;
		}
		break;
		case GIT_STATUS_WT_DELETED:
		{
			ret = git_index_add_bypath(index, entry->index_to_workdir->old_file.path);
			commit_obj++;
			LOG_DBG << "delete file: "
				<< entry->index_to_workdir->old_file.path;
		}
		break;
		case GIT_STATUS_WT_TYPECHANGE:
		{
			ret = git_index_add_bypath(index, entry->index_to_workdir->old_file.path);
			commit_obj++;
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
			ret = git_index_add_bypath(index, entry->index_to_workdir->old_file.path);
			commit_obj++;
		}
		break;
		default:
			break;
		}

		if (ret != 0)
		{
			LOG_DBG << "git_index_write, path: "
				<< entry->index_to_workdir->old_file.path
				<< ", status: " << entry->status
				<< ", err: "
				<< git_error_last()->message;
			return EXIT_FAILURE;
		}
	}

	if (commit_obj > 0)
	{
		if (git_index_write(index) != 0)
		{
			LOG_DBG << "git_index_write, err: "
				<< git_error_last()->message;

			return EXIT_FAILURE;
		}

		git_oid tree_id, parent_id, commit_id;
		if (git_index_write_tree(&tree_id, index) != 0)
		{
			LOG_DBG << "git_index_write_tree, err: "
				<< git_error_last()->message;

			return EXIT_FAILURE;
		}

		git_tree* tree = nullptr;
		if (git_tree_lookup(&tree, repo, &tree_id) != 0)
		{
			LOG_DBG << "git_tree_lookup, err: "
				<< git_error_last()->message;

			return EXIT_FAILURE;
		}
		scoped_exit gtree_free([&tree]() mutable
			{
				git_tree_free(tree);
			});

		// 获取当前的 HEAD 提交作为父提交
		if (git_reference_name_to_id(&parent_id, repo, "HEAD") != 0)
		{
			LOG_DBG << "git_reference_name_to_id, err: "
				<< git_error_last()->message;

			return EXIT_FAILURE;
		}

		git_commit* parent = nullptr;
		if (git_commit_lookup(&parent, repo, &parent_id) != 0)
		{
			LOG_DBG << "git_commit_lookup, err: "
				<< git_error_last()->message;

			return EXIT_FAILURE;
		}
		scoped_exit gcommit_free([&parent]() mutable
			{
				git_commit_free(parent);
			});

		git_signature* signature = nullptr;
		// 创建一个新的签名
		if (git_signature_now(&signature,
			global_git_author.c_str(),
			global_git_email.c_str()) != 0)
		{
			LOG_DBG << "git_signature_now, err: "
				<< git_error_last()->message;

			return EXIT_FAILURE;
		}
		scoped_exit gsignature_free([&signature]() mutable
			{
				git_signature_free(signature);
			});

		// 从树对象创建一个新的提交
		if (git_commit_create_v(
			&commit_id,
			repo,
			"HEAD",
			signature,
			signature,
			nullptr,
			global_commit_message.c_str(),
			tree,
			1,
			parent
		) != 0)
		{
			LOG_DBG << "git_commit_create_v, err: "
				<< git_error_last()->message;

			return EXIT_FAILURE;
		}
	}

	git_remote* remote = nullptr;
	if (git_remote_lookup(&remote, repo, "origin") != 0)
	{
		LOG_DBG << "git_remote_lookup, err: "
			<< git_error_last()->message;

		return EXIT_FAILURE;
	}
	scoped_exit gremote_free([&remote]() mutable
		{
			git_remote_free(remote);
		});

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
	const char* refspec = "refs/heads/master";
	git_strarray arr = {
		.strings = (char**)&refspec,
		.count = 1
	};
	if (git_remote_push(remote, &arr, &options) != 0)
	{
		LOG_DBG << "git_remote_push, err: "
			<< git_error_last()->message;

		// 处理错误
		return EXIT_FAILURE;
	}

	LOG_DBG << "Successfully push to remote";

	return EXIT_SUCCESS;
}

boost::asio::awaitable<int> git_work_loop(int check_interval, const std::string& git_dir)
{
	git_libgit2_init();
	scoped_exit glibgit2_shutdown([]() mutable
		{
			git_libgit2_shutdown();
		});

	git_repository* repo = nullptr;
	if (git_repository_open_ext(&repo,
		git_dir.c_str(),
		0,
		nullptr) != 0)
	{
		LOG_DBG << "git_repository_open_ext, dir: "
			<< git_dir
			<< " err: "
			<< git_error_last()->message;

		co_return EXIT_FAILURE;
	}
	scoped_exit grepository_free([&repo]() mutable
		{
			git_repository_free(repo);
		});

	try
	{
		while (true)
		{
			gitwork(repo);

			dirmon::dirmon repo_change_notify(co_await boost::asio::this_coro::executor, git_dir);

			co_await repo_change_notify.async_wait_dirchange();

			// 再等下一个周期继续.
			awaitable_timer timer(co_await boost::asio::this_coro::executor);
			timer.expires_from_now(std::chrono::seconds(check_interval));
			co_await timer.async_wait();
		}
	}
	catch (boost::thread_interrupted&)
	{
		LOG_DBG << "git loop thread stopped";
	}

	co_return EXIT_SUCCESS;
}

boost::asio::awaitable<int> co_main(int argc, char** argv)
{
	co_await this_coro::coro_yield();

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

		("http_username", po::value<std::string>(&global_http_username)->default_value(""), "Username for HTTP auth.")
		("http_password", po::value<std::string>(&global_http_password)->default_value(""), "Password for HTTP auth.")

		("ssh_pubkey", po::value<std::string>(&global_ssh_pubkey)->default_value(""), "Public key for SSH auth.")
		("ssh_privkey", po::value<std::string>(&global_ssh_privkey)->default_value(""), "Private key for SSH auth.")
		("ssh_passphrase", po::value<std::string>(&global_ssh_passphrase)->default_value(""), "SSH key passphrase.")

		("git_author", po::value<std::string>(&global_git_author)->default_value(""), "Author name for Git commit.")
		("git_email", po::value<std::string>(&global_git_email)->default_value(""), "Author email for Git commit.")

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

	boost::asio::signal_set terminator_signal(co_await boost::asio::this_coro::executor);
	terminator_signal.add(SIGINT);
	terminator_signal.add(SIGTERM);

#if defined(SIGQUIT)
	terminator_signal.add(SIGQUIT);
#endif // defined(SIGQUIT)

	LOG_DBG << "Running...";

	using namespace boost::asio::experimental::awaitable_operators;

	// 处理中止信号.
	co_await(
		git_work_loop(check_interval, git_dir)
			||
		terminator_signal.async_wait(boost::asio::use_awaitable)
	);

	terminator_signal.clear();

	co_return EXIT_SUCCESS;
}

static int platform_init()
{
#if defined(WIN32) || defined(_WIN32)
	/* Disable the "application crashed" popup. */
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
		SEM_NOOPENFILEERRORBOX);

#if defined(DEBUG) ||defined(_DEBUG)
	//	_CrtDumpMemoryLeaks();
	// 	int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
	// 	flags |= _CRTDBG_LEAK_CHECK_DF;
	// 	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
	// 	_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
	// 	_CrtSetDbgFlag(flags);
#endif

#if !defined(__MINGW32__)
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
#endif

	_setmode(0, _O_BINARY);
	_setmode(1, _O_BINARY);
	_setmode(2, _O_BINARY);

	/* Disable stdio output buffering. */
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	/* Enable minidump when application crashed. */
#elif defined(__linux__)
	rlimit of = { 50000, 100000 };
	if (setrlimit(RLIMIT_NOFILE, &of) < 0)
	{
		perror("setrlimit for nofile");
	}
	struct rlimit core_limit;
	core_limit.rlim_cur = RLIM_INFINITY;
	core_limit.rlim_max = RLIM_INFINITY;
	if (setrlimit(RLIMIT_CORE, &core_limit) < 0)
	{
		perror("setrlimit for coredump");
	}

	/* Set the stack size programmatically with setrlimit */
	rlimit rl;
	int result = getrlimit(RLIMIT_STACK, &rl);
	if (result == 0)
	{
		const rlim_t stack_size = 100 * 1024 * 1024;
		if (rl.rlim_cur < stack_size)
		{
			rl.rlim_cur = stack_size;
			result = setrlimit(RLIMIT_STACK, &rl);
			if (result != 0)
				perror("setrlimit for stack size");
		}
	}
#endif

	std::ios::sync_with_stdio(false);

#ifdef __linux__
	signal(SIGPIPE, SIG_IGN);
#else
	set_thread_name("mainthread");
#endif
	return 0;
}

int main(int argc, char** argv)
{
	platform_init();

	int main_return;

	boost::asio::io_context ios;

	boost::asio::co_spawn(ios, co_main(argc, argv), [&](std::exception_ptr e, int ret){
		if (e)
			std::rethrow_exception(e);
		main_return = ret;
		ios.stop();
	});
#if 0
	pthread_atfork([](){
		ios.notify_fork(boost::asio::execution_context::fork_prepare);
	}, [](){
		ios.notify_fork(boost::asio::execution_context::fork_parent);
	}, [](){
		ios.notify_fork(boost::asio::execution_context::fork_child);
	});
#endif
	ios.run();
	return main_return;
}
