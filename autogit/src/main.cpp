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

#ifdef _MSC_VER
# define popen _popen
# define pclose _pclose
#endif // _MSC_VER

std::tuple<std::string, bool> run_command(const std::string& cmd) noexcept
{
	auto pf = popen(cmd.c_str(), "r");
	if (!pf)
		return { "", false };

	std::string result;
	size_t total = 0;

	while (!feof(pf))
	{
		result.resize(total + 1024);
		auto nread = fread((char*)(result.data() + total), 1, 1024, pf);
		if (nread <= 0)
			break;
		total += nread;
	}
	result.resize(total);

	int exit_code = pclose(pf);
	return { result, exit_code == EXIT_SUCCESS ? true : false };
}


boost::thread global_gitwork_thrd;

void signal_callback_handler(int signum)
{
	global_gitwork_thrd.interrupt();
}

int cred_acquire_cb(git_cred** cred,
	const char* url,
	const char* username_from_url,
	unsigned int allowed_types, void* payload)
{
	std::string baseinfo((const char*)payload);
	auto result = strutil::split(baseinfo, ":");
	for (auto& r : result)
	{
		std::vector<uint8_t> str;
		if (!strutil::from_hexstring(r, str))
			return -1;

		r = std::string(str.begin(), str.end());
	}

	if (result.size() != 2)
		return -1;

	return git_cred_userpass_plaintext_new(cred,
		result[0].c_str(),
		result[1].c_str());
}

int gitwork(git_repository* repo,
	const std::string& commit_message,
	const std::string& git_username,
	const std::string& git_password,
	const std::string& git_user_name,
	const std::string& git_user_mail)
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
			LOG_DBG << "git_index_write, err: "
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
			git_user_name.c_str(),
			git_user_mail.c_str()) != 0)
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
			commit_message.c_str(),
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

	auto baseinfo =
	strutil::to_hex(git_username)
		+ ":" +
	strutil::to_hex(git_password);

	options.callbacks.credentials = cred_acquire_cb;
	options.callbacks.payload = (void*)baseinfo.c_str();

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

	return EXIT_SUCCESS;
}

int git_work_loop(int time,
	const std::string& git_dir,
	const std::string& commit_message,
	const std::string& git_username,
	const std::string& git_password,
	const std::string& git_user_name,
	const std::string& git_user_mail)
{
	git_libgit2_init();
	scoped_exit glibgit2_init([]() mutable
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

		return EXIT_FAILURE;
	}
	scoped_exit git_shutdown([&repo]() mutable
		{
			git_repository_free(repo);
		});

	try
	{
		while (true)
		{
			gitwork(repo,
				commit_message,
				git_username,
				git_password,
				git_user_name,
				git_user_mail);

			// 再等下一个周期继续.
			boost::this_thread::sleep_for(boost::chrono::seconds(time));
		}
	}
	catch (boost::thread_interrupted&)
	{
		LOG_DBG << "git loop thread stopped";
	}

	return EXIT_SUCCESS;
}

int main(int argc, char** argv)
{
	std::string git_dir;
	std::string log_directory;
	std::string commit_message;
	std::string repo_username;
	std::string repo_password;

	std::string git_user_name;
	std::string git_user_mail;

	int time;
	bool disable_logs = false;

	// 解析命令行.
	po::options_description desc("Options");
	desc.add_options()
		("help,h", "Help message.")
		("repository", po::value<std::string>(&git_dir)->value_name("repository"), "Git repository path")
		("commit_message", po::value<std::string>(&commit_message)->default_value("Commit by autogit"), "Git commit message")

		("repo_username", po::value<std::string>(&repo_username)->default_value(""), "Git http repo username")
		("repo_password", po::value<std::string>(&repo_password)->default_value(""), "Git http repo password")

		("git_user_name", po::value<std::string>(&git_user_name)->default_value(""), "Git repo user.name")
		("git_user_mail", po::value<std::string>(&git_user_mail)->default_value(""), "Git repo user.mail")

		("time", po::value<int>(&time)->default_value(60), "Wait time seconds until committing.")
		("disable_logs", po::value<bool>(&disable_logs)->default_value(false), "Disable logs.")
		("logs_path", po::value<std::string>(&log_directory)->value_name(""), "Logs dirctory.")
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

	if (disable_logs)
	{
		util::toggle_logging();
		util::toggle_write_logging(true);
	}

	// 帮助输出.
	if (vm.count("help") || argc == 1)
	{
		std::cerr << desc;
		return EXIT_SUCCESS;
	}

	util::init_logging(log_directory);
	signal(SIGINT, signal_callback_handler);
	signal(SIGTERM, signal_callback_handler);

	LOG_DBG << "Running...";

	global_gitwork_thrd = boost::thread([&]() {
		git_work_loop(time,
			git_dir,
			commit_message,
			repo_username,
			repo_password,
			git_user_name,
			git_user_mail);
		});

	global_gitwork_thrd.join();

	return EXIT_SUCCESS;
}
