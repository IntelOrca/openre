-- web.lua — minimal debug web server for the OpenRE Live Console: serves a
-- self-contained HTML page with live game state and a Lua eval box.

-- FlagGroup enum values (see docs/openre.lua for the full list).
local FlagGroup = { Status = 1 }

local port = tonumber(re.getEnv("OPENRE_WEB_PORT")) or 8080

local ok, listener = pcall(function()
    return re.network.createTcpListener("127.0.0.1", port)
end)
if not ok then
    print("web: could not bind 127.0.0.1:" .. port .. " (" .. tostring(listener) .. ")")
    return
end

print("listening on 127.0.0.1:" .. port)

-- Self-contained page served at GET /.
local PAGE = [[<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<title>OpenRE Live Console</title>
<style>
body { font-family: monospace; background: #101010; color: #ddd; margin: 1rem; }
h1 { color: #7fd; }
textarea { width: 100%; height: 8em; background: #000; color: #0f0; font-family: monospace; }
pre { background: #000; padding: .5rem; overflow: auto; white-space: pre-wrap; }
button { padding: .4rem 1.2rem; }
</style></head>
<body>
<h1>OpenRE Live Console</h1>
<pre id="state">(no game loaded yet)</pre>
<textarea id="code" placeholder="Lua code to evaluate (debug builds only)"></textarea>
<br>
<button id="run">Run</button>
<pre id="result">(no result yet)</pre>
<script>
async function poll() {
    try {
        const r = await fetch('/api/state');
        const j = await r.json();
        document.getElementById('state').textContent = JSON.stringify(j, null, 2);
    } catch (e) { /* game state not available yet */ }
}
setInterval(poll, 1000);
poll();
document.getElementById('run').addEventListener('click', async function () {
    const code = document.getElementById('code').value;
    const r = await fetch('/api/eval', { method: 'POST', body: code });
    document.getElementById('result').textContent = JSON.stringify(await r.json(), null, 2);
});
</script>
</body></html>]]

-- Everything after the header/body separator (Content-Length is ignored).
local function requestBody(req)
    local i = req:find("\r\n\r\n")
    if i then return req:sub(i + 4) end
    local j = req:find("\n\n")
    if j then return req:sub(j + 2) end
    return ""
end

local function jsonEscape(s)
    return s:gsub("\\", "\\\\"):gsub('"', '\\"'):gsub("\r", "\\r"):gsub("\n", "\\n")
end

-- Live snapshot for GET /api/state; every access is pcall-guarded so the
-- page works before a game is loaded and never crashes the game.
local function gameState()
    local player = nil
    local pok, pent = pcall(re.getEntity, EntityKind.player, 0)
    if pok and pent then
        local life, maxLife, x, y, z
        if pcall(function()
            life, maxLife = pent.life, pent.maxLife
            x, y, z = pent.posX, pent.posY, pent.posZ
        end) and type(life) == "number" then
            player = { life = life, maxLife = maxLife or 0, posX = x or 0, posY = y or 0, posZ = z or 0 }
        end
    end

    local enemiesAlive = 0
    for i = 0, 63 do
        local eok, ent = pcall(re.getEntity, EntityKind.enemy, i)
        if eok and ent then
            local lok, life = pcall(function() return ent.life end)
            if lok and type(life) == "number" and life > 0 then
                enemiesAlive = enemiesAlive + 1
            end
        end
    end

    local flags = {}
    for _, idx in ipairs({ 0, 1, 2, 3 }) do
        local fok, value = pcall(re.getFlag, FlagGroup.Status, idx)
        flags["f" .. idx] = fok and value == true
    end

    return { player = player, enemiesAlive = enemiesAlive, flags = flags }
end

local function jsonEncode(state)
    local parts = { "{" }
    if state.player then
        local p = state.player
        parts[#parts + 1] = '"player":{"life":' .. tostring(p.life)
            .. ',"maxLife":' .. tostring(p.maxLife)
            .. ',"posX":' .. tostring(p.posX)
            .. ',"posY":' .. tostring(p.posY)
            .. ',"posZ":' .. tostring(p.posZ) .. "}"
    else
        parts[#parts + 1] = '"player":null'
    end
    parts[#parts + 1] = ',"enemiesAlive":' .. tostring(state.enemiesAlive)
    parts[#parts + 1] = ',"flags":{'
    local first = true
    for k, v in pairs(state.flags) do
        if not first then parts[#parts + 1] = "," end
        first = false
        parts[#parts + 1] = '"' .. k .. '":' .. (v and "true" or "false")
    end
    parts[#parts + 1] = "}}"
    return table.concat(parts)
end

local function respond(sock, status, contentType, body)
    local header = "HTTP/1.1 " .. status .. "\r\n"
        .. "Content-Type: " .. contentType .. "\r\n"
        .. "Content-Length: " .. #body .. "\r\n"
        .. "Connection: close\r\n\r\n"
    sock:write(header .. body)
    sock:flush()
    sock:dispose()
end

local function handle(sock, req)
    sock.noDelay = true

    local line = req:match("^([^\r\n]*)")
    local method, path = line:match("^(%u+)%s+([^%s]+)")
    if not method then
        respond(sock, "400 Bad Request", "text/plain", "bad request")
        return
    end

    if method == "GET" and path == "/" then
        respond(sock, "200 OK", "text/html; charset=utf-8", PAGE)
    elseif method == "GET" and path == "/api/state" then
        respond(sock, "200 OK", "application/json", jsonEncode(gameState()))
    elseif method == "POST" and path == "/api/eval" then
        local body
        if re.eval then
            local eok, result = re.eval(requestBody(req), "web")
            body = eok
                and '{"ok":true,"result":"' .. jsonEscape(tostring(result)) .. '"}'
                or '{"ok":false,"error":"' .. jsonEscape(tostring(result)) .. '"}'
        else
            body = '{"ok":false,"error":"re.eval is only available in debug builds"}'
        end
        respond(sock, "200 OK", "application/json", body)
    else
        respond(sock, "404 Not Found", "text/plain", "404 not found")
    end
end

-- Requests are received across ticks: read() returns whatever the network
-- thread has buffered so far (best-effort), so we accumulate until the
-- header terminator arrives, then handle the request.
local inflight = {}

re.subscribe(HookKind.tick, function()
    -- Accept any new connections.
    while true do
        local sock = listener:pop()
        if not sock then break end
        sock.noDelay = true
        inflight[sock] = ""
    end

    -- Accumulate buffered bytes for each in-flight request.
    local ready = {}
    for sock, buf in pairs(inflight) do
        for _ = 1, 8 do
            local chunk = sock:read(4096)
            if not chunk then break end
            buf = buf .. chunk
        end
        inflight[sock] = buf
        if buf:find("\r\n\r\n") or buf:find("\n\n") then
            ready[#ready + 1] = { sock, buf }
        end
    end

    -- Handle completed requests.
    for _, entry in ipairs(ready) do
        local sock, req = entry[1], entry[2]
        inflight[sock] = nil
        local hok, herr = pcall(handle, sock, req)
        if not hok then
            print("web: request failed: " .. tostring(herr))
            pcall(function() sock:dispose() end)
        end
    end
end)

