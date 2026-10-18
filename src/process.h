#pragma once
// Running external programs: the formatters of Reformat Code and the shell commands of
// Vim mode (":!", ":r !", ":w !", filters). The programs run in their own session without
// access to the editor's terminal, so they cannot disturb the screen or wait for input.

#include <string>

// Runs a shell command (/bin/sh -c) with input on stdin in the directory cwd ("" = the
// current one) and collects stdout and stderr. status is the exit status (-1 when the program
// was ended by a signal). Returns false when the command could not be started or was stopped
// after timeoutMs milliseconds (-1 = no limit).
bool runCommand(const std::string& cmd, const std::string& cwd, const std::string& input, int timeoutMs,
                std::string& out, std::string& err, int& status);

// Runs a shell command like Vim's ":!": stdout and stderr are both collected in out.
bool runShell(const std::string& cmd, const std::string& input, std::string& out, int& status);
