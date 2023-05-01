#include <string>
#include <vector>
#include <stdio.h>
#include "godot_cpp/variant/string.hpp"
#include "git_wrappers.h"

#ifdef _WIN32

#include <windows.h>

godot::String windows_quote_command_line_argument(const godot::String &p_text) {
	for (int i = 0; i < p_text.length(); i++) {
		char32_t c = p_text[i];
		if (c == ' ' || c == '&' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == '^' || c == '=' || c == ';' || c == '!' || c == '\'' || c == '+' || c == ',' || c == '`' || c == '~') {
			return "\"" + p_text + "\"";
		}
	}
	return p_text;
}

godot::String run_command(const godot::String &cmd_file, const godot::String &stdin_values, const std::vector<godot::String> &args) {
	// Construct Command
	godot::String cmd_file_path = cmd_file.replace("/", "\\");
	godot::String command = windows_quote_command_line_argument(cmd_file_path);
	for (const godot::String &arg : args) {
		command += " " + windows_quote_command_line_argument(arg);
	}

	PROCESS_INFORMATION processInfo;
	STARTUPINFOW startupInfo;
	SECURITY_ATTRIBUTES saAttr;

	HANDLE stdoutReadHandle = NULL;
	HANDLE stdoutWriteHandle = NULL;

	HANDLE stdinReadHandle = NULL;
	HANDLE stdinWriteHandle = NULL;

	DWORD exitcode;

	memset(&saAttr, 0, sizeof(saAttr));
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;

	// Create a pipe for the child process's STDOUT.
	if (!CreatePipe(&stdoutReadHandle, &stdoutWriteHandle, &saAttr, 0)) {
		printf("CreatePipe: %u\n", GetLastError());
		return godot::String{};
	}

	// Ensure the read handle to the pipe for STDOUT is not inherited.
	if (!SetHandleInformation(stdoutReadHandle, HANDLE_FLAG_INHERIT, 0)) {
		printf("SetHandleInformation: %u\n", GetLastError());
		return godot::String{};
	}

	if (!CreatePipe(&stdinReadHandle, &stdinWriteHandle, &saAttr, 0)) {
		printf("CreatePipe: %u\n", GetLastError());
		return godot::String{};
	}

	if (!SetHandleInformation(stdinWriteHandle, HANDLE_FLAG_INHERIT, 0)) {
		printf("SetHandleInformation: %u\n", GetLastError());
		return godot::String{};
	}

	DWORD written = 0;
	godot::CharString utf8_stdin = stdin_values.utf8();
	if (!WriteFile(stdinWriteHandle, utf8_stdin.get_data(), utf8_stdin.length(), &written, nullptr)) {
		printf("WriteFile: %u\n", GetLastError());
		return godot::String{};
	}

	if (utf8_stdin.length() != written) {
		printf("Written Bytes are: %u (expected: %u)\n", written, utf8_stdin.length());
		return godot::String{};
	}

	CloseHandle(stdinWriteHandle);

	memset(&startupInfo, 0, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	startupInfo.hStdOutput = stdoutWriteHandle;
	startupInfo.hStdInput = stdinReadHandle;
	startupInfo.dwFlags |= STARTF_USESTDHANDLES;

	if (!CreateProcessW(nullptr, (LPWSTR)(command.wide_string().get_data()), nullptr, nullptr, true,
				NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startupInfo, &processInfo)) {
		printf("CreateProcessW: %u\n", GetLastError());
		return godot::String{};
	}

	CloseHandle(stdoutWriteHandle);

	DWORD total_bytes_read = 0;
	DWORD bytes_read = 0;
	std::vector<char> buffer(32768);
	buffer.resize(32768);

	for (;;) {
		if (!ReadFile(stdoutReadHandle, &buffer[0] + total_bytes_read, buffer.size() - total_bytes_read, &bytes_read, NULL)) {
			if (bytes_read == 0) {
				break;
			}
			printf("ReadFile: %u\n", GetLastError());
			break;
		}
		if (bytes_read > 0) {
			total_bytes_read += bytes_read;
			if (buffer.size() - total_bytes_read < 256) {
				buffer.resize(buffer.size() * 2);
			}
		}
	}
	buffer.resize(total_bytes_read);

	if (WaitForSingleObject(processInfo.hProcess, INFINITE) != WAIT_OBJECT_0) {
		printf("WaitForSingleObject: %u\n", GetLastError());
		return godot::String{};
	}

	if (!GetExitCodeProcess(processInfo.hProcess, &exitcode)) {
		printf("GetExitCodeProcess: %u\n", GetLastError());
		return godot::String{};
	}

	printf("'%s' exit code: %u\n", command.utf8(), exitcode);

	CloseHandle(processInfo.hProcess);
	CloseHandle(processInfo.hThread);

	return godot::String::utf8(&buffer[0], buffer.size());
}

#else

#include <unistd.h>

godot::String run_command(const godot::String &cmd_file, const godot::String &stdin_values, const std::vector<godot::String> &args) {
	int parentToChild[2];
	int childToParent[2];
	pid_t pid;

	if (pipe(parentToChild) != 0) {
		printf("Pipe creation STDIN failed");
		return godot::String{};
	}

	if (pipe(childToParent) != 0) {
		printf("Pipe creation STDOUT failed");
		return godot::String{};
	}

	switch (pid = fork()) {
		case -1: {
			printf("Fork failed");
			return godot::String{};
		}
		case 0: {
			if (dup2(parentToChild[0], STDIN_FILENO) == -1) {
				printf("dup2 STDIN failed");
			}
			if (dup2(childToParent[1], STDOUT_FILENO) == -1) {
				printf("dup2 STDOUT failed");
			}
			if (close(parentToChild[1]) != 0) {
				printf("close !STDIN failed");
			}
			if (close(childToParent[0]) != 0) {
				printf("close !STDOUT failed");
			}
			std::vector<godot::CharString> s_args;
			std::vector<const char *> c_args;
			// Program
			godot::CharString arg = cmd_file.utf8();
			c_args.push_back(arg.get_data());
			s_args.push_back(std::move(arg));
			// Args
			for (const godot::String &iterator_arg : args) {
				godot::CharString arg2 = iterator_arg.utf8();
				c_args.push_back(arg2.get_data());
				s_args.push_back(std::move(arg2));
			}
			c_args.push_back(nullptr);
			execvp(c_args[0], const_cast<char *const *>(c_args.data()));
			abort();
		}
		default: {
			abort();
		}
	}
	if (close(parentToChild[0]) != 0) {
		printf("close p!STDIN failed");
	}
	if (close(childToParent[1]) != 0) {
		printf("close p!STDOUT failed");
	}
	godot::CharString utf8_stdin = stdin_values.utf8();
	ssize_t written = 0;
	ssize_t last_written = 0;
	while (written < utf8_stdin.length()) {
		last_written = write(parentToChild[1], utf8_stdin.get_data() + written, utf8_stdin.length() - written);
		if (last_written >= 0) {
			written += last_written;
		} else {
			printf("Written Error: %l\n", last_written);
		}
	}
	std::vector<char> buffer(32768);
	buffer.resize(32768);
	ssize_t total_bytes_read = 0;
	ssize_t bytes_read = 0;
	for (;;) {
		bytes_read = read(childToParent[0], &buffer[0] + total_bytes_read, buffer.size() - total_bytes_read);

		if (bytes_read > 0) {
			total_bytes_read += bytes_read;
			if (buffer.size() - total_bytes_read < 256) {
				buffer.resize(buffer.size() * 2);
			}
		} else if (bytes_read == 0) {
			break;
		} else if (bytes_read == -1) {
			if ((errno == EINTR) || (errno == EAGAIN)) {
				continue;
			} else {
				printf("Read Error: %d\n", errno);
			}
		}
	}
	buffer.resize(total_bytes_read);
	int status;
	waitpid(pid, &status, 0);
	printf("'%s' exit code: %u\n", cmd_file.utf8().get_data(), status);
	return godot::String::utf8(&buffer[0], buffer.size());
}

#endif