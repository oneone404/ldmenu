#pragma once
#include <string>
#include <vector>
namespace ldcontrol {
struct Config {
    int function=0,filter=0;bool sell=false,reconnect=false;
};
struct SavedConfig {int index=0;std::wstring name;Config config;};
struct PlayTarget {int index=0;std::wstring name;};
struct GlobalSettings {Config defaults;unsigned settleSeconds=60;unsigned playDelaySeconds=5;std::vector<PlayTarget> playTargets;};
bool validConfig(const Config& c);
bool validGlobal(const GlobalSettings& settings);
bool loadSettings(std::vector<SavedConfig>& out,std::wstring& error,GlobalSettings* global=nullptr);
bool saveSettings(const std::vector<SavedConfig>& entries,std::wstring& error,const GlobalSettings& global={});
bool decodeSettings(const std::string& text,std::vector<SavedConfig>& out,GlobalSettings* global=nullptr);
std::string encodeSettings(const std::vector<SavedConfig>& entries,const GlobalSettings& global={});
}
