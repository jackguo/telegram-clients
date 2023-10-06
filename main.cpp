//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "TdExample.hpp"
#include <iostream>

using namespace std;

int main(int argc, char *argv[]) {

    if ((argc < 3) || (argc >3)) {
        cout << "usage: ngram <api_id> <api_hash>";
    } else {
      int api_id = stoi(argv[1]);
      string api_hash = argv[2];
      cout << "API_ID entered "<<api_id<<endl;
      cout<< "API_HASH entered "<<api_hash<<endl;  
      TdExample example;
      example.setCredentials(api_id, api_hash);
      example.loop();
    }
  
    return 0;
  
}
