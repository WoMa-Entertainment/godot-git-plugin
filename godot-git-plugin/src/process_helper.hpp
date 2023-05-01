#ifndef _GODOT_GIT_PLUGIN_PROCESS_HELPER_
#define _GODOT_GIT_PLUGIN_PROCESS_HELPER_ 1

godot::String run_command(const godot::String &cmd_file, const godot::String &stdin_values, const std::vector<godot::String> &args);

#endif