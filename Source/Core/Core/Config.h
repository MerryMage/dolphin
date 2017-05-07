// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <vector>

#include "Common/Config/Enums.h"
#include "Common/Config/SettingInfo.h"

namespace Config
{

using StringSetting = SettingInfo<std::string>;
using BoolSetting = SettingInfo<bool>;
using IntSetting = SettingInfo<int>;

constexpr StringSetting LAST_FILENAME {System::Main, "General", "LastFilename", ""};
constexpr BoolSetting SHOW_LAG {System::Main, "General", "ShowLag", false};
constexpr BoolSetting SHOW_FRAME_COUNT {System::Main, "General", "ShowFrameCount", false};

// TODO: GDB STUB

constexpr IntSetting ISO_FOLDERS_COUNT {System::Main, "General", "ISOPaths", 0};
constexpr const char* ISO_FOLDERS_KEY_FORMAT = "ISOPath%i";
constexpr BoolSetting ISO_FOLDERS_RECURSIVE {System::Main, "General", "RecursiveISOPaths", false};

std::vector<std::string> GetISOFolders();
void SetISOFolders(Layer layer, const std::vector<std::string>& iso_folders);

constexpr StringSetting NAND_PATH {System::Main, "General", "NANDRootPath", ""};
/*File::SetUserPath(D_WIIROOT_IDX, m_NANDPath);*/
constexpr StringSetting DUMP_PATH {System::Main, "General", "DumpPath", ""};
/*CreateDumpPath(m_DumpPath);*/
constexpr StringSetting WIRELESS_MAC {System::Main, "General", "WirelessMac", ""};
constexpr StringSetting WII_SD_CARD_PATH {System::Main, "General", "WiiSDCardPath", /* TODO */};
/*File::SetUserPath(F_WIISDCARD_IDX, m_strWiiSDCardPath);*/

#ifdef USE_GDBSTUB
#ifndef _WIN32
constexpr StringSetting gdb_socket {System::Main, "General", "GDBSocket", ""};
#endif
constexpr IntSetting iGDBPort {System::Main, "General", "GDBPort", -1};
#endif

}