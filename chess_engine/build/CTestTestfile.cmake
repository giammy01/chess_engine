# CMake generated Testfile for 
# Source directory: /home/giammy/progetti/PythonProject/chess_engine2
# Build directory: /home/giammy/progetti/PythonProject/chess_engine2/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(perft "/home/giammy/progetti/PythonProject/chess_engine2/build/nitro" "perft" "5")
set_tests_properties(perft PROPERTIES  _BACKTRACE_TRIPLES "/home/giammy/progetti/PythonProject/chess_engine2/CMakeLists.txt;32;add_test;/home/giammy/progetti/PythonProject/chess_engine2/CMakeLists.txt;0;")
add_test(tactics "/home/giammy/progetti/PythonProject/chess_engine2/build/nitro" "selftest" "1000")
set_tests_properties(tactics PROPERTIES  _BACKTRACE_TRIPLES "/home/giammy/progetti/PythonProject/chess_engine2/CMakeLists.txt;33;add_test;/home/giammy/progetti/PythonProject/chess_engine2/CMakeLists.txt;0;")
