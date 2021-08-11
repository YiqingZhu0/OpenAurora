To build: `mkdir build ; cd build ; cmake ..`

This creates greeter_server and libgreeter.so

Run greeter_server. Currently, create the directories for each database manually. When running `create database test` in mysql for example, make sure to create a test directory in the working directory of the server.

It is very useful to have compiled mysql from source with debug symbols and assertions enabled. These are default when building mysql from source.

Initialize mysql if you haven't already
`mysqld --initialize`
Then run `mysqld` with libgreeter.so LD_PRELOADED
`LD_PRELOAD=<path to libgreeter.so> mysqld`

Then connect to it via `mysql` and run sql queries. Look at the servers output to make sure writes are happening as expected.

This hasn't been tested on Linux yet. The `stat` function in greeter_client looks for the macos symbol `stat$INODE64` it is probably called `stat64` in glibc. Run `echo -e "#include <sys/stat.h>\nint a() { stat(0, 0); }" | clang -x C - -c | nm` And find which symbol corresponds to stat. It may be a macro. (I don't know if gcc allows reading input files from stdin with "-" like clang, if not just put the above program into a file to find out).

Also after many insertions the current implementation fails. Not all syscalls are implemented yet, maybe one that I am missing is causing this to happen. It fails in pwrite with an offset of about USHRT_MAX.
