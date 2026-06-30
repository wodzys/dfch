
#include "hooks.h"

#include "Debug.h"
#include "MemAccess.h"
#include "PluginManager.h"
#include "DFHackVersion.h"

#include "modules/Screen.h"
#include "modules/World.h"
#include "modules/Gui.h"
#include "modules/Hotkey.h"

#include "df/world.h"

#include <string>
#include <vector>
#include <cctype>

DFHACK_PLUGIN("dfch");
REQUIRE_GLOBAL(world);

namespace DFHack {
    // DBG_DECLARE(dfch, log);
    // for configuration-related logging
    DBG_DECLARE(dfch, status, DebugCategory::LINFO);
    // for plugin_onupdate logging
    DBG_DECLARE(dfch, onupdate, DebugCategory::LINFO);
    // for command-related logging
    DBG_DECLARE(dfch, command, DebugCategory::LINFO);

    namespace DFCH {
        DFHACK_PLUGIN_IS_ENABLED(is_enabled);
    }
}

using namespace DFHack;
using namespace DFHack::DFCH;

static std::map<std::string, std::string> current_bindings;
static command_result do_command(color_ostream &out, std::vector<std::string> &parameters);
static void add_binding(color_ostream &out);
static void remove_binding(color_ostream &out);

// run when the plugin is loaded
DFhackCExport command_result plugin_init(color_ostream &out, std::vector<PluginCommand> &commands) {
    DEBUG(status,out).print("initializing %s\n", plugin_name);

    commands.push_back(PluginCommand(
        plugin_name,
        "Dwarf Fortress Chinese plugin.",
        do_command,
        Gui::anywhere_hotkey
    ));

    if (!Hooks::init()) {
        DEBUG(status,out).print("%s init failed, plugin not fully loaded\n", plugin_name);
        return CR_FAILURE;
    }
    Hooks::attach();

    return CR_OK;
}

DFhackCExport command_result plugin_shutdown(color_ostream &out) {
    DEBUG(status,out).print("shutting down {}\n", plugin_name);

    Hooks::detach();
    Hooks::shutdown();

    return CR_OK;
}

DFhackCExport command_result plugin_enable(color_ostream &out, bool enable) {
    DEBUG(status,out).print("{} from the API\n", enable ? "enabled" : "disabled");
    std::string plugin_name_upper = plugin_name;
    std::transform(plugin_name_upper.begin(), plugin_name_upper.end(), plugin_name_upper.begin(),
                [](unsigned char c) { return std::toupper(c); });
    if (enable != is_enabled) {
        is_enabled = enable;
        Hooks::plugin_enable(is_enabled);
        if (is_enabled) {
            add_binding(out);
        } else {
            remove_binding(out);
        }

        if (is_enabled) {
            float duration_ms = static_cast<float>(Hooks::dfch_init_elapsed_us) / 1000.0f;
            out.print("{} enabled | init: {:0.3f} ms | outperforms 99 % of all plugins.\n", plugin_name_upper, duration_ms);
        }
    } else {
        out.print("{} now is {}.\n", plugin_name_upper, enable ? "enabled" : "disabled");
    }

    return CR_OK;
}

DFhackCExport command_result plugin_onstatechange(color_ostream &out, state_change_event event) {
    switch (event) {
        case SC_UNKNOWN:
            DEBUG(status,out).print("gameStateChanged: SC_UNKNOWN\n");
            break;
        case SC_WORLD_LOADED:
            DEBUG(status,out).print("gameStateChanged: SC_WORLD_LOADED\n");
            break;
        case SC_WORLD_UNLOADED:
            DEBUG(status,out).print("gameStateChanged: SC_WORLD_UNLOADED\n");
            break;
        case SC_MAP_LOADED:
            DEBUG(status,out).print("gameStateChanged: SC_MAP_LOADED\n");
            break;
        case SC_MAP_UNLOADED:
            DEBUG(status,out).print("gameStateChanged: SC_MAP_UNLOADED\n");
            break;
        case SC_VIEWSCREEN_CHANGED:
        {
            auto vs = Gui::getCurViewscreen(true);
            std::string name = Core::getInstance().p->readClassName(*(void**)vs);
            if (name.starts_with("viewscreen_"))
                // name = name.substr(11, name.size()-11-2);
                name = name.substr(4, name.size()-4-2);
            else if (dfhack_viewscreen::is_instance(vs)) {
                auto fs = Gui::getFocusStrings(vs);
                if (fs.size()) {
                    name = "";
                    for (const auto& str : fs) {
                        name += str;
                        name += ";";
                    }
                }
            }
            // DEBUG(status,out).print("gameStateChanged: SC_VIEWSCREEN_CHANGED (%s)\n", name.c_str());
            // printf("gameStateChanged: SC_VIEWSCREEN_CHANGED (%s)\n", name.c_str());

            Hooks::screen_changed(name);

            break;
        }
        case SC_CORE_INITIALIZED:
            DEBUG(status,out).print("gameStateChanged: SC_CORE_INITIALIZED\n");
            break;
        case SC_BEGIN_UNLOAD:
            DEBUG(status,out).print("gameStateChanged: SC_BEGIN_UNLOAD\n");
            break;
        case SC_PAUSED:
            DEBUG(status,out).print("gameStateChanged: SC_PAUSED\n");
            break;
        case SC_UNPAUSED:
            DEBUG(status,out).print("gameStateChanged: SC_UNPAUSED\n");
            break;
    }

    return CR_OK;
}

DFhackCExport command_result plugin_onupdate (color_ostream &out) {
    DEBUG(onupdate,out).print(
        "onupdate called (run 'debugfilter set info skeleton onupdate' to stop"
        " seeing these messages)\n");
    // printf("plugin_onupdate called\n");

    return CR_OK;
}

DFhackCExport command_result plugin_save_site_data (color_ostream &out) {
    DEBUG(status,out).print("save or unload is imminent; time to persist state for site\n");

    return CR_OK;
}

DFhackCExport command_result plugin_save_world_data (color_ostream &out) {
    DEBUG(status,out).print("save or unload is imminent; time to persist state for world\n");

    // Call functions in the Persistence module here. If your PersistantDataItem
    // objects are already up to date, then they will get persisted with the
    // save automatically and you do not need to implement this function.
    return CR_OK;
}

DFhackCExport command_result plugin_load_world_data (color_ostream &out) {
    DEBUG(status,out).print("world is loading; time to load world-global persisted state\n");

    // Call functions in the Persistence module here. See
    // persistent_per_save_example.cpp for an example.
    return CR_OK;
}

DFhackCExport command_result plugin_load_site_data (color_ostream &out) {
    DEBUG(status,out).print("site is loading; time to load site-local persisted state\n");

    // Call functions in the Persistence module here. See
    // persistent_per_save_example.cpp for an example.
    return CR_OK;
}

static command_result do_command(color_ostream &out, std::vector<std::string> &parameters) {
    DEBUG(command,out).print("{} command called with {} parameters\n",
        plugin_name, parameters.size());
    if (parameters.size() == 0) {
        return CR_OK;
    }

    Hooks::do_command(parameters);
    return CR_OK;
}

static void add_binding(color_ostream &out) {
    current_bindings["Ctrl-Alt-L"] = "dfch save_untrans";
    current_bindings["Ctrl-Alt-R"] = "dfch reload_dicts";
    current_bindings["Ctrl-Alt-K"] = "dfch show_ch";

    for (const auto& binding : current_bindings) {
        Core::getInstance().getHotkeyManager()->addKeybind(binding.first, binding.second);
        DEBUG(status,out).print("adding keybinding: {} -> {}\n", binding.first, binding.second);
    }
}

static void remove_binding(color_ostream &out) {
    for (const auto& binding : current_bindings) {
        Core::getInstance().getHotkeyManager()->removeKeybind(binding.first);
        DEBUG(status,out).print("removing keybinding: {} -> {}\n", binding.first, binding.second);
    }
}
