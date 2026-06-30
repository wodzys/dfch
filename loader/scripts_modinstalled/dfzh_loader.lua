-- main file for the dfzh_loader mod

--@ module = true

--[====[
dfzh_loader
===========

Tags: dfhack | localization | chinese

Usage
-----
    Auto-loads on DFHack init (SC_CORE_INITIALIZED) without requiring any
    commands.  Also exposes CLI sub-commands for manual control:

    dfzh_loader -reload
        Re-run the full installation pipeline: plugin DLL, data sync, and
        SDL2_ttf.dll.  Useful after updating mod files in-place.

    dfzh_loader -unload
        Remove installed artifacts only: hack/plugins/dfch.plug.dll and
        hack/data/dfch/.  Does NOT touch the mod source directory or
        SDL2_ttf.dll in the DF root.

    Both flags are mutually exclusive; supplying no flag prints this help.
]====]

local utils = require "utils"
local json = require('json')
local scriptmanager = require('script-manager')

local GLOBAL_KEY = 'dfzh_loader'

-- ============================================================================

local function get_default_state()
    return {
        loaded = false,
        load_attempted = false,
    }
end
state = state or get_default_state()


-- ============================================================================
-- Utility: ensure a directory exists (mkdir_recursive is idempotent)
-- ============================================================================
local function ensure_dir(dir)
    if dfhack.filesystem.exists(dir) then
        return true
    end
    return dfhack.filesystem.mkdir_recursive(dir)
end

-- ============================================================================
-- Persisted deployment state (via json.open)
-- Stores deploy_state.json in <hack>/data/dfch/ alongside other mod data.
-- ============================================================================
local function load_state(data_dst_dir)
    ensure_dir(data_dst_dir)
    local config = json.open(data_dst_dir .. '/deploy_state.json')
    return config
end

-- ============================================================================
-- Utility: binary-safe file copy
-- ============================================================================
local function copy_file(src, dst)
    local sf, err = io.open(src, "rb")
    if not sf then
        return false, "open src: " .. (err or "unknown")
    end
    local df, derr = io.open(dst, "wb")
    if not df then
        sf:close()
        return false, "open dst: " .. (derr or "unknown")
    end
    local block = 65536  -- 64 KiB
    while true do
        local data = sf:read(block)
        if not data then break end
        local ok, werr = pcall(df.write, df, data)
        if not ok then
            sf:close()
            df:close()
            return false, "write: " .. tostring(werr)
        end
    end
    sf:close()
    df:close()
    return true
end

-- ============================================================================
-- Utility: recursively remove a directory tree
-- ============================================================================
local function remove_tree(dir)
    if not dfhack.filesystem.exists(dir) then
        return
    end
    for _, name in ipairs(dfhack.filesystem.listdir(dir) or {}) do
        local path = dir .. '/' .. name
        if dfhack.filesystem.isdir(path) then
            remove_tree(path)
        else
            local ok, err = os.remove(path)
            if not ok then
                dfhack.printerr(string.format("[%s] [2/3] Remove failed: %s (%s)", GLOBAL_KEY, name, err))
            end
        end
    end
    dfhack.filesystem.rmdir(dir)
end

-- ============================================================================
-- Utility: recursively copy a directory tree
-- ============================================================================
local function copy_tree(src_dir, dst_dir)
    ensure_dir(dst_dir)
    for _, name in ipairs(dfhack.filesystem.listdir(src_dir) or {}) do
        local sp, dp = src_dir .. '/' .. name, dst_dir .. '/' .. name
        if dfhack.filesystem.isdir(sp) then
            copy_tree(sp, dp)
        else
            local ok, err = copy_file(sp, dp)
            if not ok then
                dfhack.printerr(string.format("[%s] [2/3] Copy failed: %s (%s)", GLOBAL_KEY, name, err))
            end
        end
    end
end

-- ============================================================================
-- Phase 1: find and install the plugin DLL.
-- DLL naming convention: dfch-<dfhack_version>.plug.dll
-- Destination: <hack>/plugins/dfch.plug.dll
-- Returns: true on success (aborts entire loader on failure).
-- ============================================================================
local function phase_install_plugin(plugins_src_dir, plugins_dst_dir, dfhack_version)
    local dll_name = string.format("dfch-%s.plug.dll", dfhack_version)
    local dll_src = plugins_src_dir .. '/' .. dll_name
    local dll_dst = plugins_dst_dir .. '/dfch.plug.dll'

    dfhack.printerr(string.format("[%s] [1/3] Plugin DLL: Searching for %s", GLOBAL_KEY, dll_name))

    if not dfhack.filesystem.exists(dll_src) then
        dfhack.printerr(string.format(
            "[%s] [1/3] ERROR: DLL not found for DFHack %s", GLOBAL_KEY, dfhack_version))
        dfhack.printerr(string.format("[%s] [1/3] Expected: %s", GLOBAL_KEY, dll_src))

        local ok, listing = pcall(dfhack.filesystem.listdir, plugins_src_dir)
        if ok and type(listing) == "table" and #listing > 0 then
            dfhack.printerr(string.format("[%s] [1/3] Available DLLs:", GLOBAL_KEY))
            for _, f in ipairs(listing) do
                dfhack.printerr(string.format("    %s", f))
            end
        else
            dfhack.printerr(string.format(
                "[%s] [1/3] Plugin directory empty or missing: %s", GLOBAL_KEY, plugins_src_dir))
        end
        return false
    end

    ensure_dir(plugins_dst_dir)
    if dfhack.filesystem.exists(dll_dst) then
        os.remove(dll_dst)
    end

    dfhack.printerr(string.format("[%s] [1/3] Copying -> %s", GLOBAL_KEY, dll_dst))
    local ok, err = copy_file(dll_src, dll_dst)
    if not ok then
        dfhack.printerr(string.format("[%s] [1/3] Copy failed: %s", GLOBAL_KEY, err))
        return false
    end

    dfhack.printerr(string.format("[%s] [1/3] Plugin DLL installed", GLOBAL_KEY))
    return true
end

-- ============================================================================
-- Phase 2: full data directory sync.
-- Removes the old destination tree, then recursively copies source.
-- Requires Phase 1 success.
-- ============================================================================
local function phase_sync_data(data_src_dir, data_dst_dir)

    dfhack.printerr(string.format("[%s] [2/3] Data sync starting\n    src : %s\n    dst : %s", GLOBAL_KEY, data_src_dir, data_dst_dir))

    if not dfhack.filesystem.exists(data_src_dir) then
        dfhack.printerr(string.format("[%s] [2/3] Source data directory not found, skipping", GLOBAL_KEY))
        return
    end

    remove_tree(data_dst_dir)
    copy_tree(data_src_dir, data_dst_dir)

    dfhack.printerr(string.format("[%s] [2/3] Data sync complete", GLOBAL_KEY))
end

-- ============================================================================
-- Phase 3: copy SDL2_ttf.dll to the destination if missing.
-- Does NOT overwrite an existing file (user may have a custom build).
-- ============================================================================
local function phase_install_sdl_ttf(sdl_ttf_src, sdl_ttf_dst)

    dfhack.printerr(string.format("[%s] [3/3] SDL2_ttf.dll check", GLOBAL_KEY))

    if dfhack.filesystem.exists(sdl_ttf_dst) then
        dfhack.printerr(string.format("[%s] [3/3] Already present in DF root, skipping", GLOBAL_KEY))
        return
    end

    if not dfhack.filesystem.exists(sdl_ttf_src) then
        dfhack.printerr(string.format("[%s] [3/3] Source not found: %s", GLOBAL_KEY, sdl_ttf_src))
        return
    end

    dfhack.printerr(string.format("[%s] [3/3] Copying -> %s", GLOBAL_KEY, sdl_ttf_dst))
    local ok, err = copy_file(sdl_ttf_src, sdl_ttf_dst)
    if not ok then
        dfhack.printerr(string.format("[%s] [3/3] Copy failed: %s", GLOBAL_KEY, err))
        return
    end

    dfhack.printerr(string.format("[%s] [3/3] SDL2_ttf.dll installed", GLOBAL_KEY))
end

-- ============================================================================
-- Public entry point
--
-- Parses info.txt from the mod root, compares against persisted deploy_state.json
-- to decide which phases to skip:
--   NUMERIC_VERSION unchanged → skip ALL phases
--   DISPLAYED_VERSION unchanged → skip Phase 2 (data sync) only
-- ============================================================================
function run()
    local mod_path = scriptmanager.getModSourcePath(GLOBAL_KEY):gsub('\\', '/')
    local hack_path = dfhack.getHackPath():gsub('\\', '/')
    local df_path = dfhack.getDFPath():gsub('\\', '/')
    local dfhack_version = dfhack.getDFHackVersion()

    local internal_dir      = mod_path .. 'scripts_modinstalled/internal'
    -- Pre-compute all destination paths
    local plugins_dst_dir   = hack_path .. '/plugins'
    local data_dst_dir      = hack_path .. '/data/dfch'
    local sdl_ttf_dst       = df_path .. '/SDL2_ttf.dll'

    dfhack.printerr(string.format("[%s] Version     : %s", GLOBAL_KEY, dfhack_version))
    dfhack.printerr(string.format("[%s] Mod Path    : %s", GLOBAL_KEY, internal_dir))
    dfhack.printerr(string.format("[%s] Hack Path   : %s", GLOBAL_KEY, hack_path))
    dfhack.printerr(string.format("[%s] DF Path     : %s", GLOBAL_KEY, df_path))

    -- Parse mod version from info.txt (returns strings, nil on failure)
    local meta = scriptmanager.get_mod_info_metadata(mod_path,
        {'NUMERIC_VERSION', 'DISPLAYED_VERSION'})
    local numeric_version = meta and meta.NUMERIC_VERSION
    local displayed_version = meta and meta.DISPLAYED_VERSION

    if numeric_version and displayed_version then
        dfhack.printerr(string.format("[%s] NUMERIC_VERSION: %s. DISPLAYED_VERSION: %s", GLOBAL_KEY, numeric_version, displayed_version))
    end

    -- Load persisted deployment state
    local config = load_state(data_dst_dir)
    local saved = config.data

    -- NUMERIC_VERSION unchanged → everything is up to date
    if numeric_version and saved.numeric_version == numeric_version then
        dfhack.printerr(string.format("[%s] NUMERIC_VERSION unchanged, all phases skipped", GLOBAL_KEY))
        return true
    end

    -- DISPLAYED_VERSION unchanged → skip data sync (Phase 2)
    local skip_data = false
    if displayed_version and saved.displayed_version == displayed_version then
        dfhack.printerr(string.format("[%s] DISPLAYED_VERSION unchanged, skipping Phase 2 (data sync)", GLOBAL_KEY))
        skip_data = true
    end

    -- Phase 1: plugin DLL (abort entire loader on failure)
    local ok = phase_install_plugin(internal_dir .. '/plugins', plugins_dst_dir, dfhack_version)
    if not ok then
        dfhack.printerr(string.format("[%s] Phase 1 failed - loader stopped", GLOBAL_KEY))
        return
    end

    -- Phase 2: data sync (skipped if DISPLAYED_VERSION unchanged)
    if not skip_data then
        phase_sync_data(internal_dir .. '/data', data_dst_dir)
    end

    -- Phase 3: SDL2_ttf
    phase_install_sdl_ttf(internal_dir .. '/SDL2_ttf.dll', sdl_ttf_dst)

    -- Persist deployment state for next startup comparison
    if numeric_version then
        saved.numeric_version = numeric_version
        saved.displayed_version = displayed_version
        config:write()
        dfhack.printerr(string.format("[%s] Deployment state saved", GLOBAL_KEY))
    end

    dfhack.printerr(string.format("[%s] All phases complete", GLOBAL_KEY))
    return true
end

-- ============================================================================
-- Unload: remove installed plugin DLL and data directory.
-- Does NOT touch the mod source or SDL2_ttf.dll in DF root.
-- ============================================================================
local function unload()
    local hack_path = dfhack.getHackPath():gsub('\\', '/')

    local dll_dst = hack_path .. '/plugins/dfch.plug.dll'
    local data_dst_dir = hack_path .. '/data/dfch'

    if dfhack.filesystem.exists(dll_dst) then
        os.remove(dll_dst)
        dfhack.printerr(string.format("[%s] Removed plugin: %s", GLOBAL_KEY, dll_dst))
    end

    if dfhack.filesystem.exists(data_dst_dir) then
        remove_tree(data_dst_dir)
        dfhack.printerr(string.format("[%s] Removed data: %s", GLOBAL_KEY, data_dst_dir))
    end

    dfhack.printerr(string.format("[%s] Unload complete", GLOBAL_KEY))
end

-- ============================================================================

local function do_loader()
    local ok, result = pcall(run)
    if not ok then
        dfhack.printerr(string.format("[%s] Loader error: %s", GLOBAL_KEY, tostring(result)))
        return false
    end
    return result or false
end

dfhack.onStateChange[GLOBAL_KEY] = function(sc)
    if sc == SC_CORE_INITIALIZED then
        dfhack.printerr(string.format("[%s] onStateChange: %s, DF: %s, DFHack: %s",
            GLOBAL_KEY, sc,
            dfhack.getDFVersion(),
            dfhack.getDFHackVersion()))
        dfhack.printerr(string.format("[%s] DF: %s, DFHack: %s",
            GLOBAL_KEY, dfhack.getDFPath(), dfhack.getHackPath()))

        if not state.load_attempted then
            state.load_attempted = true
            state.loaded = do_loader()
            dfhack.printerr(string.format("[%s] Loader finished, loaded=%s",
                GLOBAL_KEY, tostring(state.loaded)))
        end
    end
end

function main(...)
    local validArgs = utils.invert({"reload", "unload"})
    local args = utils.processArgs({...}, validArgs)

    if args.unload then
        unload()
        return
    end

    if args.reload then
        do_loader()
        return
    end
    print(dfhack.script_help())
end

if not dfhack_flags.module then
    main(...)
end
