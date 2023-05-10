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

#include <boost/program_options.hpp>
namespace po = boost::program_options;


int main(int argc, char** argv)
{
	std::string bot_token;
	std::string bot_server;
	std::string log_directory;
	bool disable_logs = false;

	// 解析命令行.
	po::options_description desc("Options");
	desc.add_options()
		("help,h", "Help message.")
		("bot-token", po::value<std::string>(&bot_token)->value_name("token"), "Telegram bot token id")
		("bot-server", po::value<std::string>(&bot_server)->default_value("https://api.telegram.org")->value_name("url"), "Telegram bot server url")
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

	LOG_DBG << "Running...";

	return EXIT_SUCCESS;
}
