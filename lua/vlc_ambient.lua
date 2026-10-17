-- vlc_ambient.lua - VLC 3 extension for VlcAmbient (ambient glow around the video).
-- Adds "View -> Ambient light"; each click opens the VlcAmbient settings
-- panel, starting VlcAmbient.exe first if it is not running.
--
-- Contract with the app (data dir = %LOCALAPPDATA%\VlcAmbient\):
--   exe_path  - UTF-8 full path of VlcAmbient.exe (written by the app)
--   alive     - unix timestamp, refreshed every ~5 s while the app runs
--   command   - we write "show" here (via command.tmp + rename)

local TAG = "[Ambient light] "
local ALIVE_TIMEOUT = 15 -- seconds

function descriptor()
    return {
        title = "Ambient light",
        version = "1.0",
        author = "VlcAmbient",
        shortdesc = "Ambient light",
        description = "Открывает панель настроек подсветки VlcAmbient "
            .. "(свечение вокруг видео VLC). Если VlcAmbient не запущен, "
            .. "расширение запускает его.",
        capabilities = {}
    }
end

---------------------------------------------------------------- helpers

local function warn(text)
    pcall(function() vlc.msg.warn(TAG .. text) end)
end

-- Message for the user: OSD (visible only while a video plays) + log.
local function notify(text)
    warn(text)
    pcall(function() vlc.osd.message(text, nil, "top-left", 5000000) end)
end

local function trim(s)
    if type(s) ~= "string" then return nil end
    s = s:gsub("^\239\187\191", "") -- UTF-8 BOM
    return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

-- vlc.io.* takes UTF-8 paths; the plain io/os functions take ANSI paths
-- (which is what os.getenv returns). Try both.
local function open_file(path, mode)
    local ok, f = pcall(function() return vlc.io and vlc.io.open(path, mode) end)
    if ok and f then return f end
    ok, f = pcall(io.open, path, mode)
    if ok and f then return f end
    return nil
end

local function read_file(path)
    local f = open_file(path, "rb")
    if not f then return nil end
    local ok, data = pcall(function() return f:read("*a") end)
    pcall(function() f:close() end)
    return ok and data or nil
end

local function write_file(path, data)
    local f = open_file(path, "wb")
    if not f then return false end
    local ok = pcall(function() f:write(data) end)
    pcall(function() f:close() end)
    return ok
end

local function remove_file(path)
    pcall(os.remove, path)
    pcall(function() if vlc.io then vlc.io.unlink(path) end end)
end

local function file_exists(path)
    local f = open_file(path, "rb")
    if not f then return false end
    pcall(function() f:close() end)
    return true
end

-- Data dir candidates: %LOCALAPPDATA% (ANSI) and, for non-ASCII user names,
-- the same folder derived from VLC's UTF-8 user dir (%APPDATA%\vlc).
local function data_dir()
    local dirs = {}
    local ok, env = pcall(os.getenv, "LOCALAPPDATA")
    if ok and env and env ~= "" then
        dirs[#dirs + 1] = env:gsub("[\\/]+$", "") .. "\\VlcAmbient\\"
    end
    local ok2, ud = pcall(function() return vlc.config.userdatadir() end)
    if ok2 and type(ud) == "string" then
        local root, n = ud:gsub("[\\/][Rr]oaming[\\/][Vv][Ll][Cc][\\/]*$", "\\Local")
        if n == 1 then dirs[#dirs + 1] = root .. "\\VlcAmbient\\" end
    end
    for _, d in ipairs(dirs) do
        if file_exists(d .. "exe_path") or file_exists(d .. "alive") then
            return d
        end
    end
    return dirs[1]
end

---------------------------------------------------------------- actions

local function app_running(dir)
    local alive = tonumber(trim(read_file(dir .. "alive")) or "")
    return alive ~= nil and math.abs(os.time() - alive) <= ALIVE_TIMEOUT
end

-- command.tmp -> command, so the app never sees a half-written file.
local function send_command(dir, word)
    local tmp, cmd = dir .. "command.tmp", dir .. "command"
    if not write_file(tmp, word) then
        warn("cannot write " .. tmp)
        return false
    end
    remove_file(cmd)
    local ok, renamed = pcall(os.rename, tmp, cmd)
    if ok and renamed then return true end
    -- os.rename failed (e.g. non-ASCII path): write the file directly.
    remove_file(tmp)
    if write_file(cmd, word) then return true end
    warn("cannot write " .. cmd)
    return false
end

local function launch_app(dir)
    local exe = trim(read_file(dir .. "exe_path"))
    if not exe or exe == "" or exe:find('"', 1, true) then
        notify("Ambient light: запустите VlcAmbient.exe один раз")
        return
    end
    if not file_exists(exe) then
        notify("Ambient light: VlcAmbient.exe не найден (" .. exe
            .. "), запустите его один раз")
        return
    end
    -- cmd.exe: start "<window title>" "<program>" args; returns immediately.
    local ok, err = pcall(os.execute, 'start "" "' .. exe .. '" --show')
    if not ok then warn("os.execute failed: " .. tostring(err)) end
end

local function open_panel()
    local dir = data_dir()
    if not dir then
        notify("Ambient light: не найдена папка LOCALAPPDATA")
        return
    end
    if app_running(dir) and send_command(dir, "show") then return end
    launch_app(dir)
end

---------------------------------------------------------------- VLC hooks

function activate()
    local ok, err = pcall(open_panel)
    if not ok then warn("error: " .. tostring(err)) end
    -- Deactivate right away so the next menu click activates (opens) again.
    pcall(function() vlc.deactivate() end)
end

function deactivate()
end

function close()
    pcall(function() vlc.deactivate() end)
end
