local Device = require("device")
local ImageViewer = require("ui/widget/imageviewer")
local InfoMessage = require("ui/widget/infomessage")
local NetworkMgr = require("ui/network/manager")
local Screen = require("device").screen
local UIManager = require("ui/uimanager")
local WidgetContainer = require("ui/widget/container/widgetcontainer")
local lfs = require("libs/libkoreader-lfs")
local socket = require("socket")
local _ = require("gettext")

-- Defaults. A generated config.lua sitting next to this file overrides any of
-- these; when it is absent (hand install) the defaults stand on their own.
local PORT = 8080
local POLL = 1                          -- seconds between accept() polls
local KEEP = 30                         -- how many received images to retain
local MAX_BODY = 32 * 1024 * 1024
local SAVE_DIR = require("datastorage"):getDataDir() .. "/cast"

do
    -- Everything here is inside the pcall, including working out where this
    -- file lives: source carries no directory when the plugin is loaded by a
    -- relative path, and a nil there would take the whole plugin down at load
    -- time rather than just falling back to the defaults.
    local ok, cfg = pcall(function()
        local src = debug.getinfo(1, "S").source or ""
        local dir = src:match("@?(.*[/\\])") or "./"
        return dofile(dir .. "config.lua")
    end)
    if ok and type(cfg) == "table" then
        PORT = tonumber(cfg.port) or PORT
        POLL = tonumber(cfg.poll) or POLL
        KEEP = tonumber(cfg.keep) or KEEP
        MAX_BODY = tonumber(cfg.max_body) or MAX_BODY
        if type(cfg.save_dir) == "string" and cfg.save_dir ~= "" then
            SAVE_DIR = cfg.save_dir
        end
    end
end

-- Module-level, NOT instance-level. KOReader creates a fresh plugin instance for
-- FileManager and again for ReaderUI, so instance state is lost when you open a
-- book. Keeping the socket here means the server survives that transition.
local S = { server = nil, poll_cb = nil }

local PhoneCast = WidgetContainer:extend{
    name = "phonecast",
    is_doc_only = false,
}

--------------------------------------------------------------------- helpers

-- No packets are sent; this just asks the kernel which source address it would
-- use. Works regardless of whether the Wi-Fi interface is eth0 or wlan0, which
-- varies across Kobo generations.
local function local_ip()
    local udp = socket.udp()
    if not udp then return "?" end
    udp:setpeername("8.8.8.8", 53)
    local ip = udp:getsockname()
    udp:close()
    return ip or "?"
end

local function receive_exactly(client, len)
    local parts, got, deadline = {}, 0, os.time() + 60
    while got < len do
        if os.time() > deadline then return nil, "timeout" end
        local chunk, err, partial = client:receive(math.min(65536, len - got))
        local data = chunk or partial
        if data and #data > 0 then
            parts[#parts + 1] = data
            got = got + #data
        elseif err and err ~= "timeout" then
            return nil, err
        end
    end
    return table.concat(parts)
end

-- Browser <form> uploads arrive as multipart; Shortcuts/curl send the raw file.
-- Handle both.
local function extract_payload(body, ctype)
    if ctype and ctype:find("multipart/form-data", 1, true) then
        local boundary = ctype:match("boundary=([^;%s]+)")
        if not boundary then return nil end
        boundary = boundary:gsub('^"', ""):gsub('"$', "")
        local _, head_end = body:find("\r\n\r\n", 1, true)
        if not head_end then return nil end
        local tail = body:find("\r\n--" .. boundary, head_end + 1, true)
        if not tail then return nil end
        return body:sub(head_end + 1, tail - 1)
    end
    return body
end

local function ext_for(bytes)
    if bytes:sub(1, 8) == "\137PNG\r\n\26\n" then return "png" end
    if bytes:sub(1, 2) == "\255\216" then return "jpg" end
    if bytes:sub(1, 6):match("^GIF8") then return "gif" end
    return "png"
end

local function prune()
    local files = {}
    for f in lfs.dir(SAVE_DIR) do
        if f ~= "." and f ~= ".." then files[#files + 1] = f end
    end
    table.sort(files)
    for i = 1, #files - KEEP do
        os.remove(SAVE_DIR .. "/" .. files[i])
    end
end

local UPLOAD_PAGE = [[<!doctype html><meta name=viewport content="width=device-width">
<body style="font:16px sans-serif;padding:2em">
<h3>Send to Kobo</h3>
<form method=post enctype=multipart/form-data action=/upload>
<input type=file name=f accept="image/*"><br><br>
<button style="padding:1em 2em">Upload</button></form></body>]]

---------------------------------------------------------------- server logic

function PhoneCast:display(path)
    UIManager:show(ImageViewer:new{
        file = path,
        fullscreen = true,
        with_title_bar = false,
        -- 0 = scale to fit the screen. Drop this line for 1:1 pixels.
        scale_factor = 0,
    })
end

function PhoneCast:handle(client)
    client:settimeout(10)

    local request = client:receive("*l")
    if not request then client:close() return end
    local method, path = request:match("^(%u+)%s+(%S+)")

    local length, ctype = 0, nil
    while true do
        local line = client:receive("*l")
        if not line or line == "" then break end
        local k, v = line:match("^([%w%-]+):%s*(.+)$")
        if k then
            k = k:lower()
            if k == "content-length" then length = tonumber(v) or 0 end
            if k == "content-type" then ctype = v end
        end
    end

    if method == "GET" then
        client:send("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n"
            .. "Content-Length: " .. #UPLOAD_PAGE .. "\r\n\r\n" .. UPLOAD_PAGE)
        client:close()
        return
    end

    if method ~= "POST" or length <= 0 or length > MAX_BODY then
        client:send("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n")
        client:close()
        return
    end

    local body, err = receive_exactly(client, length)
    if not body then
        client:send("HTTP/1.1 408 Timeout\r\nConnection: close\r\n\r\n")
        client:close()
        return
    end

    local payload = extract_payload(body, ctype)
    if not payload or #payload < 16 then
        client:send("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n")
        client:close()
        return
    end

    local name = os.date("%Y%m%d-%H%M%S") .. "." .. ext_for(payload)
    local file = SAVE_DIR .. "/" .. name
    local fh = io.open(file, "wb")
    -- Answering "ok" for an image that never reached the disk is the one failure
    -- the sender cannot diagnose, so report it instead.
    if not fh then
        client:send("HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n")
        client:close()
        require("logger").warn("phonecast: cannot write", file)
        return
    end
    fh:write(payload)
    fh:close()
    prune()

    client:send("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n"
        .. "Content-Length: 3\r\n\r\nok\n")
    client:close()

    self:display(file)
end

function PhoneCast:poll()
    if not S.server then return end
    -- Drain everything queued, then reschedule.
    for _ = 1, 4 do
        local client = S.server:accept()
        if not client then break end
        local ok, err = pcall(function() self:handle(client) end)
        if not ok then
            pcall(function() client:close() end)
            require("logger").warn("phonecast:", err)
        end
    end
    UIManager:scheduleIn(POLL, S.poll_cb)
end

function PhoneCast:start()
    if S.server then
        UIManager:show(InfoMessage:new{
            text = _("Already listening on") .. "\nhttp://" .. local_ip() .. ":" .. PORT,
        })
        return
    end

    lfs.mkdir(SAVE_DIR)

    NetworkMgr:runWhenOnline(function()
        local srv = socket.tcp()
        srv:setoption("reuseaddr", true)
        local ok, err = srv:bind("*", PORT)
        if not ok then
            UIManager:show(InfoMessage:new{ text = _("Bind failed: ") .. tostring(err) })
            return
        end
        srv:listen(8)
        srv:settimeout(0)          -- non-blocking accept
        S.server = srv

        S.poll_cb = function() self:poll() end
        UIManager:scheduleIn(POLL, S.poll_cb)
        UIManager:preventStandby()

        UIManager:show(InfoMessage:new{
            text = string.format("%s\n\nhttp://%s:%d\n\n%s: %d x %d",
                _("Listening for images"), local_ip(), PORT,
                _("Screen"), Screen:getWidth(), Screen:getHeight()),
            timeout = 12,
        })
    end)
end

function PhoneCast:stop()
    if S.poll_cb then UIManager:unschedule(S.poll_cb); S.poll_cb = nil end
    if S.server then S.server:close(); S.server = nil; UIManager:allowStandby() end
    UIManager:show(InfoMessage:new{ text = _("Stopped."), timeout = 2 })
end

------------------------------------------------------------------ plugin glue

function PhoneCast:init()
    self.ui.menu:registerToMainMenu(self)
end

function PhoneCast:addToMainMenu(menu_items)
    menu_items.phonecast = {
        text = _("Phone Cast"),
        sorting_hint = "tools",
        sub_item_table = {
            {
                text_func = function()
                    return S.server and _("Stop listening") or _("Start listening")
                end,
                callback = function()
                    if S.server then self:stop() else self:start() end
                end,
            },
            {
                text = _("Show connection info"),
                enabled_func = function() return S.server ~= nil end,
                callback = function()
                    UIManager:show(InfoMessage:new{
                        text = string.format("http://%s:%d\n\n%s: %d x %d",
                            local_ip(), PORT, _("Screen"),
                            Screen:getWidth(), Screen:getHeight()),
                    })
                end,
            },
            {
                text = _("Show last image"),
                callback = function()
                    local newest
                    for f in lfs.dir(SAVE_DIR) do
                        if f ~= "." and f ~= ".." and (not newest or f > newest) then
                            newest = f
                        end
                    end
                    if newest then self:display(SAVE_DIR .. "/" .. newest) end
                end,
            },
        },
    }
end

return PhoneCast
