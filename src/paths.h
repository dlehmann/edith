#pragma once
// File path helpers and base64 (for the OSC 52 clipboard).

#include <string>

std::string getCwd();
// Removes "." and ".." components and duplicate slashes of an absolute path.
std::string normPath(const std::string& p);
// Absolute, normalized path; a leading "~" or "~/" is the home directory.
std::string absPath(std::string p);
// Directory part ("/" for files in the root, the current directory without a slash).
std::string dirOf(const std::string& p);
// File name part.
std::string baseOf(const std::string& p);
bool fileExists(const std::string& p);
std::string base64(const std::string& in);
