// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/StringUtil.h"

#include "Core/Config.h"
#include "Core/ConfigLoaders/GameConfigLoader.h"
#include "Core/ConfigLoaders/IsSettingSaveable.h"

namespace ConfigLoaders
{
using ConfigLocation = Config::ConfigLocation;

// Returns all possible filenames in ascending order of priority
static std::vector<std::string> GetGameIniFilenames(const std::string& id, u16 revision)
{
  std::vector<std::string> filenames;

  // INIs that match all regions
  if (id.size() >= 4)
    filenames.push_back(id.substr(0, 3) + ".ini");

  // Regular INIs
  filenames.push_back(id + ".ini");

  // INIs with specific revisions
  filenames.push_back(id + StringFromFormat("r%d", revision) + ".ini");

  return filenames;
}

using INIToLocationMap = std::map<std::pair<std::string, std::string>, ConfigLocation>;

static const INIToLocationMap& GetINIToLocationMap()
{
  static const INIToLocationMap ini_to_location{};
  return ini_to_location;
}

static ConfigLocation MapINIToRealLocation(const std::string& section, const std::string& key)
{
  static const INIToLocationMap& ini_to_location = GetINIToLocationMap();

  auto it = ini_to_location.find({section, key});
  if (it == ini_to_location.end())
  {
    // Try again, but this time with an empty key
    // Certain sections like 'Speedhacks' has keys that are variable
    it = ini_to_location.find({section, ""});
    if (it != ini_to_location.end())
      return {it->second.system, it->second.section, key};

    // Attempt to load it as a configuration option
    // It will be in the format of '<System>.<Section>'
    std::istringstream buffer(section);
    std::string system_str, config_section;

    bool fail = false;
    std::getline(buffer, system_str, '.');
    fail |= buffer.fail();
    std::getline(buffer, config_section, '.');
    fail |= buffer.fail();

    if (!fail)
      return {Config::GetSystemFromName(system_str), config_section, key};

    WARN_LOG(CORE, "Unknown game INI option in section %s: %s", section.c_str(), key.c_str());
    return {Config::System::Main, "", ""};
  }

  return ini_to_location.at({section, key});
}

static std::pair<std::string, std::string> GetINILocationFromConfig(const ConfigLocation& location)
{
  static const INIToLocationMap& ini_to_location = GetINIToLocationMap();

  auto it = std::find_if(ini_to_location.begin(), ini_to_location.end(),
                         [&location](const auto& entry) { return entry.second == location; });

  if (it != ini_to_location.end())
    return it->first;

  // Try again, but this time with an empty key
  // Certain sections like 'Speedhacks' have keys that are variable
  it = std::find_if(ini_to_location.begin(), ini_to_location.end(), [&location](const auto& entry) {
    return std::tie(entry.second.system, entry.second.section) ==
           std::tie(location.system, location.section);
  });
  if (it != ini_to_location.end())
    return {it->first.first, location.key};

  WARN_LOG(CORE, "Unknown option: %s.%s", location.section.c_str(), location.key.c_str());
  return {"", ""};
}

// INI Game layer configuration loader
class INIGameConfigLayerLoader final : public Config::ConfigLayerLoader
{
public:
  INIGameConfigLayerLoader(const std::string& id, u16 revision, bool global)
      : ConfigLayerLoader(global ? Config::LayerType::GlobalGame : Config::LayerType::LocalGame),
        m_id(id), m_revision(revision)
  {
  }

  void Load(Config::Layer* config_layer) override
  {
    IniFile ini;
    if (config_layer->GetLayer() == Config::LayerType::GlobalGame)
    {
      for (const std::string& filename : GetGameIniFilenames(m_id, m_revision))
        ini.Load(File::GetSysDirectory() + GAMESETTINGS_DIR DIR_SEP + filename, true);
    }
    else
    {
      for (const std::string& filename : GetGameIniFilenames(m_id, m_revision))
        ini.Load(File::GetUserPath(D_GAMESETTINGS_IDX) + filename, true);
    }

    const std::list<IniFile::Section>& system_sections = ini.GetSections();

    for (const auto& section : system_sections)
    {
      LoadFromSystemSection(config_layer, section);
    }

    LoadControllerConfig(config_layer);
  }

  void Save(Config::Layer* config_layer) override;

private:
  void LoadControllerConfig(Config::Layer* config_layer) const
  {
    // Game INIs can have controller profiles embedded in to them
    static const std::array<char, 4> nums = {{'1', '2', '3', '4'}};

    if (m_id == "00000000")
      return;

    const std::array<std::tuple<std::string, std::string, Config::System>, 2> profile_info = {{
        std::make_tuple("Pad", "GCPad", Config::System::GCPad),
        std::make_tuple("Wiimote", "Wiimote", Config::System::WiiPad),
    }};

    for (const auto& use_data : profile_info)
    {
      std::string type = std::get<0>(use_data);
      std::string path = "Profiles/" + std::get<1>(use_data) + "/";

      Config::Section* control_section =
          config_layer->GetOrCreateSection(std::get<2>(use_data), "Controls");

      for (const char num : nums)
      {
        bool use_profile = false;
        std::string profile;
        if (control_section->Exists(type + "Profile" + num))
        {
          if (control_section->Get(type + "Profile" + num, &profile))
          {
            if (File::Exists(File::GetUserPath(D_CONFIG_IDX) + path + profile + ".ini"))
            {
              use_profile = true;
            }
            else
            {
              // TODO: PanicAlert shouldn't be used for this.
              PanicAlertT("Selected controller profile does not exist");
            }
          }
        }

        if (use_profile)
        {
          IniFile profile_ini;
          profile_ini.Load(File::GetUserPath(D_CONFIG_IDX) + path + profile + ".ini");

          const IniFile::Section* ini_section = profile_ini.GetOrCreateSection("Profile");
          const IniFile::Section::SectionMap& section_map = ini_section->GetValues();
          for (const auto& value : section_map)
          {
            Config::Section* section = config_layer->GetOrCreateSection(
                std::get<2>(use_data), std::get<1>(use_data) + num);
            section->Set(value.first, value.second);
          }
        }
      }
    }
  }

  void LoadFromSystemSection(Config::Layer* config_layer, const IniFile::Section& section) const
  {
    const std::string section_name = section.GetName();
    if (section.HasLines())
    {
      // Trash INI File chunks
      std::vector<std::string> chunk;
      section.GetLines(&chunk, true);

      if (chunk.size())
      {
        const auto mapped_config = MapINIToRealLocation(section_name, "");

        if (mapped_config.section.empty() && mapped_config.key.empty())
          return;

        auto* config_section =
            config_layer->GetOrCreateSection(mapped_config.system, mapped_config.section);
        config_section->SetLines(chunk);
      }
    }

    // Regular key,value pairs
    const IniFile::Section::SectionMap& section_map = section.GetValues();

    for (const auto& value : section_map)
    {
      const auto mapped_config = MapINIToRealLocation(section_name, value.first);

      if (mapped_config.section.empty() && mapped_config.key.empty())
        continue;

      auto* config_section =
          config_layer->GetOrCreateSection(mapped_config.system, mapped_config.section);
      config_section->Set(mapped_config.key, value.second);
    }
  }

  const std::string m_id;
  const u16 m_revision;
};

void INIGameConfigLayerLoader::Save(Config::Layer* config_layer)
{
  if (config_layer->GetLayer() != Config::LayerType::LocalGame)
    return;

  IniFile ini;
  for (const std::string& file_name : GetGameIniFilenames(m_id, m_revision))
    ini.Load(File::GetUserPath(D_GAMESETTINGS_IDX) + file_name, true);

  for (const auto& system : config_layer->GetLayerMap())
  {
    for (const auto& section : system.second)
    {
      for (const auto& value : section->GetValues())
      {
        if (!IsSettingSaveable({system.first, section->GetName(), value.first}))
          continue;

        const auto ini_location =
            GetINILocationFromConfig({system.first, section->GetName(), value.first});
        if (ini_location.first.empty() && ini_location.second.empty())
          continue;

        IniFile::Section* ini_section = ini.GetOrCreateSection(ini_location.first);
        ini_section->Set(ini_location.second, value.second);
      }
    }
  }

  // Try to save to the revision specific INI first, if it exists.
  const std::string gameini_with_rev =
      File::GetUserPath(D_GAMESETTINGS_IDX) + m_id + StringFromFormat("r%d", m_revision) + ".ini";
  if (File::Exists(gameini_with_rev))
  {
    ini.Save(gameini_with_rev);
    return;
  }

  // Otherwise, save to the game INI. We don't try any INI broader than that because it will
  // likely cause issues with cheat codes and game patches.
  const std::string gameini = File::GetUserPath(D_GAMESETTINGS_IDX) + m_id + ".ini";
  ini.Save(gameini);
}

// Loader generation
std::unique_ptr<Config::ConfigLayerLoader> GenerateGlobalGameConfigLoader(const std::string& id,
                                                                          u16 revision)
{
  return std::make_unique<INIGameConfigLayerLoader>(id, revision, true);
}

std::unique_ptr<Config::ConfigLayerLoader> GenerateLocalGameConfigLoader(const std::string& id,
                                                                         u16 revision)
{
  return std::make_unique<INIGameConfigLayerLoader>(id, revision, false);
}
}
