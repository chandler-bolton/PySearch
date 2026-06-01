# PySearch

## Overview
`PySearch` is an application that uses `Python` as the frontend and `C++` (_`CPP`_) as the backend.

The GUI form takes simple information and then searches the desired drives/folders looking for data that matches your parameters.

Currently, the C++ backend will walk the drives/folders once to gather all instances of files and or folders, and the walks through the findings to match against your given criteria.

__NOTE__: The current version is not the most effecient and changes will be coming to make it more effecient - it is, however, still more effecient than _Windows File Explorer_

## Building C++ (CPP)

To build CPP:
`g++ -std=c++17 -O2 -o search_engine.exe search_engine.cpp`