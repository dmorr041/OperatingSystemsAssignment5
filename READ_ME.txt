README:

First, download the zip from moodle, extract all files to a designated directory.
Once all the files are in the same place, do the following:

1. Set Environment variable:

Type "setenv LD_LIBRARY_PATH ${LD_LIBRARY_PATH}:." at the command line	

2. Compile LibDisk, then compile LibFS, then compile all test cases:

Type "make -f Makefile.LibDisk" at the command line and hit enter
Type "make -f Makefile.LibFS" at the command line and hit enter
Type "make" at the command line and hit enter

3. Now that everything is compiled, to run each test case, the syntax is:

./nameOfTestCase.exe [file_system] [directory]

Refer to the usage for each test case to determine what goes in the [file_system] and [directory] fields.

4. Run "simple-test" first, then run the other test cases.
