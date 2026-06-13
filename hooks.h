#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace DFHack {
namespace DFCH {
namespace Hooks {

    inline std::int64_t dfch_init_elapsed_us = 0;

    void init();
    void shutdown();
    void plugin_enable(bool enabled);
    void screen_changed(std::string screen_name);
    void do_command(const std::vector<std::string>& commands);

    void attach();
    void detach();

    // void attach_text_hooks();
    void attach_basic_hooks();

    // void detach_text_hooks();
    void detach_basic_hooks();

} // namespace Hooks
} // namespace DFCH
} // namespace DFHack
