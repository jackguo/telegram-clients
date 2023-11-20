# telegram-clients
Tools for interact with telegram

#### Build instructions:
* Generate build files:

```
rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DTd_DIR=<full path to TDLib>/lib/cmake/Td ..
```

To generate xcode build project, add `-G Xcode`

* Build:
```
cmake --build .
```

#### License

This software is licensed under the terms of the Boost Software License. See [LICENSE_1_0.txt](http://www.boost.org/LICENSE_1_0.txt) for more information.
