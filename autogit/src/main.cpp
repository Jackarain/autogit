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
#include <signal.h>

#include <cstdio>
#include <cstdlib>

#include <string>
#include <tuple>

#include <boost/program_options.hpp>
namespace po = boost::program_options;

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

void signal_callback_handler(int signum)
{
	exit(signum);
}

int main(int argc, char** argv)
{
	std::string git_repository;
	std::string log_directory;
	int time;
	bool disable_logs = false;

	// 解析命令行.
	po::options_description desc("Options");
	desc.add_options()
		("help,h", "Help message.")
		("repository", po::value<std::string>(&git_repository)->value_name("repository"), "Git repository path")
		("time", po::value<int>(&time)->default_value(false), "Wait time seconds until committing")
		("disable_logs", po::value<bool>(&disable_logs)->default_value(false), "Disable logs")
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

	const auto gitcmd = "git -C " + git_repository + " ";

	while (true)
	{
		auto cmd = gitcmd + "add -f .";
		{
			auto [result, ret] = run_command(cmd);
			LOG_DBG << result;
		}

		cmd = gitcmd + "commit -m 'Commit by autogit'";
		{
			auto [result, ret] = run_command(cmd);
			LOG_DBG << result;
		}

		cmd = gitcmd + "push";
		{
			auto [result, ret] = run_command(cmd);
			LOG_DBG << result;
		}

		std::this_thread::sleep_for(std::chrono::seconds(time));
	}

	return EXIT_SUCCESS;
}
