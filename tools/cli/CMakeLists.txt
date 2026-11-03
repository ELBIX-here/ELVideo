add_executable(elv_cli main.cpp Probe.h Probe.cpp Record.h Record.cpp LeakCheck.h LeakCheck.cpp DumpInfo.h DumpInfo.cpp)
set_target_properties(elv_cli PROPERTIES OUTPUT_NAME elvideo-cli)
target_link_libraries(elv_cli PRIVATE elv::core_internal windowsapp mfplat mfreadwrite mfuuid ntdll dbghelp)
