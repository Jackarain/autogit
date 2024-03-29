//
// Copyright (c) 2009-2011 Artyom Beilis (Tonkikh)
//
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt

#include <boost/locale.hpp>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace boost::locale;

int main(int argc, char** argv)
{
    if(argc != 2) {
        std::cerr << "Usage backend locale\n";
        return 1;
    }

    // Create a set that includes all strings sorted according to ABC order
    // std::locale can be used as object for comparison
    std::vector<std::string> all;
    std::set<std::string, std::locale> all_strings;

    // Read all strings into the set
    while(!std::cin.eof()) {
        std::string tmp;
        getline(std::cin, tmp);
        all.push_back(tmp);
    }

    {
        boost::locale::localization_backend_manager mgr = boost::locale::localization_backend_manager::global();
        mgr.select(argv[1]);
        generator gen(mgr);
        // Set global locale to requested
        std::locale::global(gen(argv[2]));

        for(int i = 0; i < 10000; i++) {
            for(const auto& str : all) {
                boost::locale::to_upper(str);
                boost::locale::to_lower(str);
                if(i == 0) {
                    std::cout << boost::locale::to_upper(str) << std::endl;
                    std::cout << boost::locale::to_lower(str) << std::endl;
                }
            }
        }
    }
}
