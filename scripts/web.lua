-- web.lua — minimal debug web server for the OpenRE Live Console: serves a
-- self-contained HTML page with live game state and a Lua eval box.
-- Also a worked example of the InputHandler middleware: with
-- OPENRE_WEB_INPUT=1 it installs a hidden entity at the earliest walk
-- position whose handler replaces the frame with automated input, removing
-- all manual input (see the InputHandler sample section below).

-- FlagGroup enum values (see docs/openre.lua for the full list).
local FlagGroup = { Status = 1 }

local port = tonumber(re.getEnv("OPENRE_WEB_PORT")) or 8080
local pollMs = tonumber(re.getEnv("OPENRE_WEB_POLL")) or 800

local ok, listener = pcall(function()
    return re.network.createTcpListener("127.0.0.1", port)
end)
if not ok then
    print("web: could not bind 127.0.0.1:" .. port .. " (" .. tostring(listener) .. ")")
    return
end

print("listening on 127.0.0.1:" .. port)

-- ── InputHandler sample ──────────────────────────────────────────────
-- Worked example of the InputHandler middleware for plugin authors. When
-- enabled (OPENRE_WEB_INPUT=1) a hidden entity is placed directly under
-- the scene root — the earliest walk position, visited before every
-- sub-root — carrying a core.InputHandler whose callback replaces the
-- frame's state/press with the commands posted over HTTP, so all manual
-- input is removed and only the injected input reaches the game.
--
--   POST /api/input/set  {"state":["up","fire"],"press":["accept"]}
--   POST /api/input/tap   {"press":["accept"],"ms":200}  (auto-clearing edge)
--   POST /api/input/clear
--   GET  /api/input
--
-- Handlers are always invoked (there is no consume), so the only way to
-- "consume" the manual input is to replace the frame values, which is what
-- this callback does when injection is active. Clearing posts empty state
-- so the following handlers/game see no input.
--
-- /api/input/set holds until /api/input/clear (a held key). That is wrong for
-- menus where one edge per frame is consumed — a held press cascades through
-- multiple items. /api/input/tap arms the commands for a short window (default
-- 200ms, enough to register while menus lock input during transitions) and
-- auto-clears when the window elapses, so a caller gets exactly one edge.
local webInput = {
    active = false,
    state = {},
    press = {},
    tapTicks = 0, -- frames remaining in an armed tap; 0 when none
    entity = nil,
}
local webInputEnabled = (re.getEnv("OPENRE_WEB_INPUT") or "") == "1"

local function installWebInputHandler()
    if not webInputEnabled or webInput.entity ~= nil then return end
    local ok, err = pcall(function()
        -- No parent arg → a direct child of the scene root, so the walk
        -- (deepest, last child first, root last) reaches it before every
        -- sub-root: the earliest handler in the chain.
        local ent = scene.createEntity()
        scene.setField(ent, "name", "web_input")
        local handler = scene.addComponent(ent, "core.InputHandler")
        scene.setField(handler, "callback", function(frame)
            if webInput.active then
                frame.state = webInput.state
                frame.press = webInput.press
                -- Consume an armed tap here, at the input-walk cadence, rather
                -- than counting frames in web.Pump: the walk and the managed
                -- pump both run at the throttled 60fps cadence (they are
                -- gated on the same app.framePaced flag), but decrementing in
                -- the handler guarantees a ticks=1 tap is consumed on the
                -- same frame it would be injected, so expiry stays exact no
                -- matter how often the pump is dispatched.
                if webInput.tapTicks > 0 then
                    webInput.tapTicks = webInput.tapTicks - 1
                    if webInput.tapTicks == 0 then
                        webInput.active = false
                        webInput.state = {}
                        webInput.press = {}
                    end
                end
            end
        end)
        webInput.entity = ent
    end)
    if not ok then
        print("web: could not install input handler: " .. tostring(err))
    end
end

-- ── Inspector overlay ────────────────────────────────────────────────
-- Draws live guide lines in the game around the entity selected in the
-- web UI (the tree click posts to /api/inspector/select). The tree is
-- built lazily under the "overlay" sub-root and refreshed every tick:
--
--   overlay → inspector → entity_lines → { line_left, line_right,
--   line_top, line_bottom }
--
-- Each line is a texture-less white sprite (render2d paints a sprite
-- without a texture as a solid quad). The two vertical lines run the
-- full window height at the box's left/right edge and the two horizontal
-- lines run the full window width at its top/bottom edge; the box is the
-- selected entity's real UiSize (its UiSize scaled by the world
-- transform composed up the parent chain, exactly as render2d computes
-- draw size).
local inspectorSel = nil -- selected entity id (nil = none)
local inspectorTree = { root = nil, lines = {} }
local INSPECT_LINE_COLOR = { 255, 255, 255, 255 }
local INSPECT_LINE_THICKNESS = 2
local INSPECT_LINE_Z = -10000 -- always in front of game UI
local INSPECT_LINES = { "line_left", "line_right", "line_top", "line_bottom" }

-- Create an entity under `parent` and return the native handle plus its
-- scene id (scene.createEntity does not return the id).
local function spawnWithId(parent)
    local seen = {}
    for _, id in ipairs(scene.children(parent)) do
        seen[id] = true
    end
    local handle = scene.createEntity(parent)
    if handle == nil then return nil, nil end
    for _, id in ipairs(scene.children(parent)) do
        if not seen[id] then return handle, id end
    end
    return handle, nil
end

-- Build (or rebuild) the inspector tree under the overlay root. Stale
-- ids mean the scene was rebuilt, so the old subtree is destroyed and
-- recreated once; returns false when no scene is active.
local function ensureInspectorTree()
    local rootId = inspectorTree.root
    if rootId ~= nil and scene.entityFromId(rootId) ~= nil then
        local allLive = #inspectorTree.lines == #INSPECT_LINES
        for _, id in ipairs(inspectorTree.lines) do
            if scene.entityFromId(id) == nil then allLive = false break end
        end
        if allLive then return true end
    end
    if rootId ~= nil then
        local stale = scene.entityFromId(rootId)
        if stale ~= nil then scene.destroy(stale) end -- destroys the whole subtree
    end
    local overlayRoot = scene.getRoot("overlay")
    if overlayRoot == nil then return false end
    local inspector, inspectorId = spawnWithId(overlayRoot)
    if inspector == nil then return false end
    scene.setField(inspector, "name", "inspector")
    -- Commit now: a later partial build failure leaves a stale subtree that
    -- the stale-id check above destroys on the next tick.
    inspectorTree.root = inspectorId
    inspectorTree.lines = {}
    local entityLines = spawnWithId(inspector)
    if entityLines == nil then return false end
    scene.setField(entityLines, "name", "entity_lines")
    for _, lineName in ipairs(INSPECT_LINES) do
        local line, lineId = spawnWithId(entityLines)
        if line == nil then return false end
        scene.setField(line, "name", lineName)
        scene.setField(line, "z", INSPECT_LINE_Z)
        local sprite = scene.addComponent(line, "core.Sprite")
        scene.setField(sprite, "color", INSPECT_LINE_COLOR)
        local size = scene.addComponent(line, "core.UiSize")
        scene.setField(size, "width", 0) -- hidden until a selection lands
        scene.setField(size, "height", 0)
        inspectorTree.lines[#inspectorTree.lines + 1] = lineId
    end
    return true
end

local function readNumber(handle, name)
    return tonumber(scene.getField(handle, name)) or 0
end

local function readScale(handle, name)
    return tonumber(scene.getField(handle, name)) or 1
end

-- Position one line sprite; a 0 size hides it.
local function placeLine(lineId, x, y, w, h)
    local line = scene.entityFromId(lineId)
    if line == nil then return end
    scene.setField(line, "x", x)
    scene.setField(line, "y", y)
    local size = scene.component(line, "core.UiSize")
    if size ~= nil then
        scene.setField(size, "width", w)
        scene.setField(size, "height", h)
    end
end

-- Centre, width and height of the selected entity's real box in window
-- pixels, or nil when nothing is selected or the entity is gone. This
-- mirrors render2d::worldMatrix (row-vector TRS, UI has no rotation):
-- the world position is the local positions accumulated up the parent
-- chain (each scaled by its ancestors' scales) and the world size is
-- UiSize scaled by the chain's product of scales.
local function selectedBox()
    if inspectorSel == nil then return nil end
    local ent = scene.entityFromId(inspectorSel)
    if ent == nil then return nil end
    local selected = ent
    local chain = {}
    while ent ~= nil do
        chain[#chain + 1] = ent
        local pid = scene.parent(ent) -- nil at the scene root
        if pid == nil then break end
        ent = scene.entityFromId(pid)
    end
    local wx, wy, wsx, wsy = 0, 0, 1, 1
    for i = #chain, 1, -1 do
        local node = chain[i]
        wx = wx + readNumber(node, "x") * wsx
        wy = wy + readNumber(node, "y") * wsy
        wsx = wsx * readScale(node, "scale.x")
        wsy = wsy * readScale(node, "scale.y")
    end
    local size = scene.component(selected, "core.UiSize")
    local w = size ~= nil and readNumber(size, "width") or 0
    local h = size ~= nil and readNumber(size, "height") or 0
    return wx, wy, w * wsx, h * wsy
end

local function updateInspectorOverlay()
    if not ensureInspectorTree() then return end
    local sw, sh = gfx.screenWidth(), gfx.screenHeight()
    local cx, cy, w, h = selectedBox()
    if cx == nil then
        for _, lineId in ipairs(inspectorTree.lines) do
            placeLine(lineId, 0, 0, 0, 0)
        end
        return
    end
    local t = INSPECT_LINE_THICKNESS
    local x0, y0 = cx - w * 0.5, cy - h * 0.5
    local x1, y1 = cx + w * 0.5, cy + h * 0.5
    placeLine(inspectorTree.lines[1], x0, sh * 0.5, t, sh) -- line_left
    placeLine(inspectorTree.lines[2], x1, sh * 0.5, t, sh) -- line_right
    placeLine(inspectorTree.lines[3], sw * 0.5, y0, sw, t) -- line_top
    placeLine(inspectorTree.lines[4], sw * 0.5, y1, sw, t) -- line_bottom
end

-- Self-contained page served at GET /. The page is generated per request so
-- the configured poll interval (pollMs) can be embedded. The scene tree and
-- resource browser poll the JSON APIs and re-render live (pollMs <= 1000ms by
-- default, configurable via the OPENRE_WEB_POLL env var or ?poll= query).
local function renderPage(pollMs)
    return [[<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OpenRE Inspector</title>
<style>
:root{
  --bg:#0d1117; --panel:#161b22; --panel2:#1c2230; --border:#30363d;
  --text:#c9d1d9; --muted:#8b949e; --accent:#58a6ff; --accent2:#3fb950;
  --chip:#21262d;
}
*{box-sizing:border-box;}
html,body{height:100%;margin:0;}
body{background:var(--bg);color:var(--text);font:14px/1.4 -apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;display:flex;flex-direction:column;height:100vh;overflow:hidden;}
header{display:flex;align-items:center;gap:12px;padding:10px 16px;background:linear-gradient(180deg,#161b22,#0d1117);border-bottom:1px solid var(--border);}
header .logo{width:12px;height:12px;border-radius:3px;background:linear-gradient(135deg,var(--accent),#a371f7);box-shadow:0 0 12px rgba(88,166,255,.6);}
header h1{font-size:15px;margin:0;font-weight:600;letter-spacing:.3px;}
header .sub{color:var(--muted);font-size:12px;}
.layout{display:flex;flex:1;min-height:0;}
.sidebar{width:200px;background:var(--panel);border-right:1px solid var(--border);padding:10px;display:flex;flex-direction:column;gap:6px;}
.menu-item{display:flex;align-items:center;gap:10px;padding:10px 12px;border-radius:8px;color:var(--muted);cursor:pointer;font-weight:600;transition:.12s;border:1px solid transparent;}
.menu-item:hover{background:var(--panel2);color:var(--text);}
.menu-item.active{background:var(--panel2);color:var(--accent);border-color:rgba(88,166,255,.4);box-shadow:inset 0 0 0 1px rgba(88,166,255,.15);}
.menu-item .dot{width:8px;height:8px;border-radius:50%;background:currentColor;}
.sidebar .foot{margin-top:auto;color:var(--muted);font-size:11px;}
.view{flex:1;display:none;min-width:0;}
.view.active{display:flex;}
.tree-panel{flex:1 1 auto;min-width:260px;background:var(--panel);border-right:1px solid var(--border);overflow:auto;padding:8px;}
.tree-panel h2,.inspector h2{padding:8px 10px;margin:0;font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);}
.tree-node{user-select:none;}
.tree-row{display:flex;align-items:center;gap:4px;padding:4px 6px;border-radius:6px;cursor:pointer;}
.tree-row:hover{background:var(--panel2);}
.tree-row.selected{background:rgba(88,166,255,.15);color:var(--accent);}
.tw-toggle{width:14px;display:inline-block;text-align:center;color:var(--muted);font-size:10px;}
.tw-label{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;}
.inspector{flex:0 0 400px;overflow:auto;padding:12px;}
.empty{color:var(--muted);padding:20px;text-align:center;}
.insp-title{font-family:ui-monospace,monospace;font-size:13px;margin-bottom:10px;color:var(--accent);}
.card{background:var(--panel);border:1px solid var(--border);border-radius:10px;margin-bottom:10px;overflow:hidden;}
.card-h{padding:8px 12px;background:var(--panel2);font-weight:600;font-size:12px;border-bottom:1px solid var(--border);color:var(--accent2);}
.field{display:flex;align-items:center;gap:10px;padding:6px 12px;border-bottom:1px solid rgba(48,54,61,.5);}
.field:last-child{border-bottom:none;}
.sys-row{display:flex;align-items:center;gap:10px;padding:7px 12px;border-bottom:1px solid rgba(48,54,61,.5);font-size:12px;}
.sys-row:last-child{border-bottom:none;}
.sys-order{flex:0 0 42px;color:var(--muted);font-family:ui-monospace,monospace;}
.sys-name{flex:1;font-family:ui-monospace,monospace;color:var(--text);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
.sys-prio{flex:0 0 auto;color:var(--muted);font-family:ui-monospace,monospace;font-size:11px;}
.sys-kind{flex:0 0 auto;padding:2px 8px;border-radius:999px;font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.04em;}
.sys-kind-native{background:rgba(63,185,80,.15);color:var(--accent2);border:1px solid rgba(63,185,80,.35);}
.sys-kind-lua{background:rgba(163,113,247,.15);color:#a371f7;border:1px solid rgba(163,113,247,.35);}
.field label{flex:0 0 150px;color:var(--muted);font-family:ui-monospace,monospace;font-size:12px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
.fval{flex:1;background:var(--bg);border:1px solid var(--border);color:var(--text);border-radius:6px;padding:5px 8px;font-family:ui-monospace,monospace;font-size:12px;min-width:0;}
.fval:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 2px rgba(88,166,255,.25);}
.fval:disabled{color:var(--muted);opacity:.6;}
.reslist-panel{width:340px;background:var(--panel);border-right:1px solid var(--border);overflow:auto;}
.res-row{display:flex;align-items:center;gap:8px;padding:8px 10px;border-bottom:1px solid rgba(48,54,61,.5);cursor:pointer;font-size:12px;}
.res-row:hover{background:var(--panel2);}
.res-row.selected{background:rgba(88,166,255,.15);}
.res-cookie{font-family:ui-monospace,monospace;color:var(--accent);flex:0 0 44px;}
.res-type{color:var(--accent2);flex:0 0 56px;text-transform:uppercase;font-size:11px;}
.res-path{flex:1;color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
.res-cookies{flex:0 0 auto;max-width:110px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:var(--muted);font-family:ui-monospace,monospace;font-size:10px;}
.preview{flex:1;overflow:auto;padding:16px;display:flex;flex-direction:column;align-items:center;gap:10px;background:repeating-conic-gradient(#1b212b 0% 25%, #11151c 0% 50%) 50% / 24px 24px;}
.preview canvas{max-width:100%;height:auto;border:1px solid var(--border);border-radius:6px;box-shadow:0 6px 24px rgba(0,0,0,.5);image-rendering:pixelated;}
.img-info{color:var(--muted);font-family:ui-monospace,monospace;font-size:12px;}
::-webkit-scrollbar{width:10px;height:10px;}
::-webkit-scrollbar-thumb{background:#30363d;border-radius:6px;}
::-webkit-scrollbar-track{background:transparent;}
</style></head>
<body>
<header>
  <div class="logo"></div>
  <h1>OpenRE Inspector</h1>
  <span class="sub">live scene &amp; resource viewer</span>
</header>
<div class="layout">
  <nav class="sidebar">
    <div class="menu-item active" data-view="scene"><span class="dot"></span>Scene</div>
    <div class="menu-item" data-view="resources"><span class="dot"></span>Resources</div>
    <div class="menu-item" data-view="systems"><span class="dot"></span>Systems</div>
    <div class="menu-item" data-view="components"><span class="dot"></span>Components</div>
    <div class="menu-item" data-view="shot"><span class="dot"></span>Screenshot</div>
    <div class="foot">poll: <span id="pollLabel"></span>ms</div>
  </nav>
  <section class="view active" id="sceneView">
    <div class="tree-panel"><h2>Scene Tree</h2><div id="tree"></div></div>
    <div class="inspector" id="inspector"><div class="empty">Select an entity from the tree.</div></div>
  </section>
  <section class="view" id="resourcesView">
    <div class="reslist-panel"><h2>Resources</h2><div id="reslist"></div></div>
    <div class="preview" id="preview"><div class="empty">Select a resource to preview.</div></div>
  </section>
  <section class="view" id="systemsView">
    <div class="tree-panel" style="max-width:none;flex:1 1 auto;"><h2>Running Systems</h2><div id="systems"></div></div>
    <div class="comp-panel" style="flex:0 0 340px;border-left:1px solid var(--border);overflow:auto;padding:12px;" id="systemsInspector"></div>
  </section>
  <section class="view" id="componentsView">
    <div class="reslist-panel" style="width:360px;"><h2>Components</h2><div id="components"></div></div>
    <div class="comp-panel" style="flex:1;overflow:auto;padding:16px;" id="componentInspector"><div class="empty">Select a component to see its fields.</div></div>
  </section>
  <section class="view" id="shotView">
    <div style="display:flex;align-items:center;gap:10px;padding:10px;border-bottom:1px solid var(--border);">
      <button id="shotRefresh">Refresh</button>
      <label style="display:flex;align-items:center;gap:6px;color:var(--muted);font-size:12px;">
        <input type="checkbox" id="shotAuto"> auto-refresh
      </label>
      <span id="shotInfo" class="img-info"></span>
    </div>
    <div class="preview" style="flex:1;" id="shotPanel"><div class="empty">No screenshot yet.</div></div>
  </section>
</div>
<script>
let POLL_MS = ]] .. tostring(pollMs) .. [[;
const pm = new URLSearchParams(location.search).get('poll');
if (pm) POLL_MS = parseInt(pm, 10) || POLL_MS;
const LS_KEY = 'openre.inspector.v1';
let state = { view:'scene', selected:null, expanded:{}, resSelected:null, compSelected:null, sysSel:null,
  lastTreeHash:'', lastResHash:'', lastInspectorKey:'none', lastSysHash:'' , lastCompHash:''};
let scene = null, resources = null, systems = null, components = null;
let inspectorInputs = {};
let previewCookie = null;
try { const s = JSON.parse(localStorage.getItem(LS_KEY)); if (s) Object.assign(state, s); } catch(_) {}
function saveState(){
  try { localStorage.setItem(LS_KEY, JSON.stringify({ view:state.view, selected:state.selected, expanded:state.expanded, resSelected:state.resSelected, compSelected:state.compSelected, sysSel:state.sysSel })); } catch(_) {}
}
document.getElementById('pollLabel').textContent = POLL_MS;
// Re-arm the in-game inspector guide lines for a selection restored from
// localStorage, so they survive a page reload.
if (state.selected != null) {
  fetch('/api/inspector/select',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({entity:state.selected})}).catch(()=>{});
}
function switchView(v){
  state.view=v; saveState();
  document.querySelectorAll('.menu-item').forEach(e=>e.classList.toggle('active', e.dataset.view===v));
  document.getElementById('sceneView').classList.toggle('active', v==='scene');
  document.getElementById('resourcesView').classList.toggle('active', v==='resources');
  document.getElementById('systemsView').classList.toggle('active', v==='systems');
  document.getElementById('componentsView').classList.toggle('active', v==='components');
  document.getElementById('shotView').classList.toggle('active', v==='shot');
  if (v==='resources') previewCookie=null;
  if (v==='shot') refreshShot();
  if (v==='systems') renderSystems();
  if (v==='components') renderComponents();
}
document.querySelectorAll('.menu-item').forEach(m=>m.onclick=()=>switchView(m.dataset.view));
if (state.view!=='scene') switchView(state.view);
function entityById(id){ if(!scene) return null; for(const e of scene.entities){ if(e.id===id) return e; } return null; }
function hexId(id){ return '0x' + Number(id).toString(16).toUpperCase(); }
function entityLabel(e){ return (e.name && e.name.length>0) ? e.name : ('Entity '+hexId(e.id)); }
function isExpanded(id){ return state.expanded[id] !== false; }
function depthOf(e){ let d=0; const seen=new Set(); let cur=e; while(cur && cur.parent!=null && !seen.has(cur.id)){ seen.add(cur.id); cur=entityById(cur.parent); d++; } return d; }
function treeHash(){
  if(!scene) return 'none';
  const parts=[];
  for(const e of scene.entities){ parts.push(e.id+':'+(e.parent==null?'':e.parent)+':'+(e.children||[]).join(',')+':'+e.components.map(c=>c.type).join('|')); }
  return parts.join(';');
}
function resHash(){
  if(!resources) return 'none';
  return (resources.resources||[]).map(r=>r.resource+':'+r.type+':'+r.path+':'+(r.cookies||[]).join(',')).join(';');
}
function sysHash(){
  if(!systems) return 'none';
  return (systems.systems||[]).map(s=>s.name+':'+s.kind+':'+s.placement+':'+(s.before||[]).join('|')+':'+(s.after||[]).join('&')).join(';');
}
function compHash(){
  if(!components) return 'none';
  return (components.components||[]).map(c=>c.name+':'+c.kind+':'+(c.fields||[]).map(f=>f.name+':'+f.type).join(',')).join(';');
}
function renderSystems(){
  const el=document.getElementById('systems');
  if(!systems){ el.innerHTML='<div class="empty">No system data.</div>'; return; }
  const list=(systems.systems||[]);
  let html='<div class="card"><div class="card-h">All Systems · '+list.length+'</div>';
  if(list.length===0){ html+='<div class="empty" style="padding:12px">none registered</div>'; }
  for(let i=0;i<list.length;i++){
    const s=list[i];
    const constraints=[];
    if(s.placement && s.placement!=='default') constraints.push(s.placement);
    if(s.after&&s.after.length>0) constraints.push('after: '+s.after.join(', '));
    if(s.before&&s.before.length>0) constraints.push('before: '+s.before.join(', '));
    html+='<div class="sys-row" onclick="selectSystem('+i+')" style="cursor:pointer">'
      +'<span class="sys-order">#'+(i+1)+'</span>'
      +'<span class="sys-name" title="'+(constraints.length?escapeHtml(constraints.join(' · ')):'no constraints')+'">'+escapeHtml(s.name)+'</span>'
      +(s.placement&&s.placement!=='default'?'<span class="sys-prio">'+escapeHtml(s.placement)+'</span>':'')
      +'<span class="sys-kind '+(s.kind==='lua'?'sys-kind-lua':'sys-kind-native')+'">'+escapeHtml(s.kind)+'</span></div>';
  }
  html+='</div>';
  el.innerHTML=html;
  renderSystemsInspector();
}
function renderSystemsInspector(){
  const el=document.getElementById('systemsInspector');
  const s=(systems&&systems.systems||[])[state.sysSel];
  if(!s){ el.innerHTML='<div class="empty">Select a system for details.</div>'; return; }
  let html='<div class="insp-title">'+escapeHtml(s.name)+'</div>';
  html+='<div class="card"><div class="field"><label>kind</label><div class="fval" style="flex:1">'+escapeHtml(s.kind)+'</div></div>';
  html+='<div class="field"><label>placement</label><div class="fval" style="flex:1">'+escapeHtml(s.placement||'default')+'</div></div>';
  html+='<div class="field"><label>after</label><div class="fval" style="flex:1">'+(s.after&&s.after.length?escapeHtml(s.after.join(', ')):'<em>—</em>')+'</div></div>';
  html+='<div class="field"><label>before</label><div class="fval" style="flex:1">'+(s.before&&s.before.length?escapeHtml(s.before.join(', ')):'<em>—</em>')+'</div></div></div>';
  el.innerHTML=html;
}
function selectSystem(i){
  state.sysSel=i; saveState(); renderSystemsInspector();
}
function renderComponents(){
  const el=document.getElementById('components');
  const list=(components?components.components:[]);
  el.innerHTML='';
  for(let i=0;i<list.length;i++){
    const c=list[i];
    const row=document.createElement('div'); row.className='res-row'+(state.compSelected===c.name?' selected':'');
    row.innerHTML='<span class="res-path" style="flex:1">'+escapeHtml(c.name)+'</span>'
      +'<span class="sys-kind '+(c.kind==='runtime'?'sys-kind-lua':'sys-kind-native')+'">'+escapeHtml(c.kind)+'</span>';
    row.onclick=()=>{ state.compSelected=c.name; saveState(); buildCompList(); renderComponentInspector(); };
    el.appendChild(row);
  }
}
function buildCompList(){ renderComponents(); }
function renderComponentInspector(){
  const el=document.getElementById('componentInspector');
  const c=(components?components.components:[]).find(x=>x.name===state.compSelected);
  if(!c){ el.innerHTML='<div class="empty">Select a component to see its fields.</div>'; return; }
  let html='<div class="insp-title">'+escapeHtml(c.name)+'</div>';
  html+='<div class="card"><div class="field"><label>kind</label><div class="fval" style="flex:1">'+escapeHtml(c.kind)+'</div></div></div>';
  html+='<div class="card"><div class="card-h">Fields · '+(c.fields||[]).length+'</div>';
  if(!c.fields||c.fields.length===0){ html+='<div class="empty" style="padding:12px">no fields</div>'; }
  for(const f of (c.fields||[])){
    html+='<div class="field"><label>'+escapeHtml(f.name)+'</label>';
    html+='<span class="sys-kind sys-prio" style="background:rgba(88,166,255,.12);color:var(--accent);border:1px solid rgba(88,166,255,.3)">'+escapeHtml(f.type)+'</span>';
    if(f.default!==undefined&&f.default!=='') html+='<span class="fval" style="flex:1" title="default">'+escapeHtml(String(f.default))+'</span>';
    html+='</div>';
  }
  html+='</div>';
  el.innerHTML=html;
}
function inspectorKey(e){
  if(!e) return 'none';
  const sig=e.components.map(c=>c.type+':'+c.fields.map(f=>f.n).join(',')).join('|');
  return e.id+'#'+sig;
}
function buildTree(){
  const root=document.getElementById('tree'); root.innerHTML='';
  const ids=new Set(scene?scene.entities.map(e=>e.id):[]);
  const roots=(scene?scene.entities:[]).filter(e=> e.parent==null || !ids.has(e.parent));
  for(const r of roots) root.appendChild(treeNode(r,0));
}
function treeNode(e,depth){
  const wrap=document.createElement('div'); wrap.className='tree-node';
  const row=document.createElement('div'); row.className='tree-row'+(state.selected===e.id?' selected':'');
  row.style.paddingLeft=(6+depth*16)+'px';
  const hasKids=(e.children&&e.children.length>0);
  const tog=document.createElement('span'); tog.className='tw-toggle'; tog.textContent=hasKids?(isExpanded(e.id)?'▾':'▸'):'';
  tog.onclick=(ev)=>{ ev.stopPropagation(); if(hasKids){ state.expanded[e.id]=!isExpanded(e.id); saveState(); buildTree(); } };
  const lab=document.createElement('span'); lab.className='tw-label'; lab.textContent=entityLabel(e);
  row.onclick=()=>selectEntity(e.id);
  row.appendChild(tog); row.appendChild(lab); wrap.appendChild(row);
  if(hasKids && isExpanded(e.id)){
    const kids=e.children.map(cid=>entityById(cid)).filter(Boolean);
    for(const k of kids) wrap.appendChild(treeNode(k,depth+1));
  }
  return wrap;
}
function selectEntity(id){
  state.selected=id; saveState(); buildTree(); renderInspector();
  fetch('/api/inspector/select',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({entity:id})}).catch(()=>{});
}
function escapeHtml(s){ return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c])); }
function makeInput(key, val){
  let input;
  if(typeof val==='boolean'){ input=document.createElement('input'); input.type='checkbox'; input.checked=!!val; input.onchange=()=>setField(key, input.checked); }
  else if(typeof val==='number'){ input=document.createElement('input'); input.type='number'; input.step='any'; input.value=String(val); input.onchange=()=>setField(key, parseFloat(input.value)); }
  else if(val==null){ input=document.createElement('input'); input.type='text'; input.value='(null)'; input.disabled=true; }
  else if(val==='[Managed Function]'){ input=document.createElement('input'); input.type='text'; input.value=val; input.disabled=true; }
  else { input=document.createElement('input'); input.type='text'; input.value=String(val); input.onchange=()=>setField(key, input.value); }
  input.className='fval'; input.dataset.key=key;
  return input;
}
function setField(key, value){
  const p=key.split('|'); const entity=parseInt(p[0],10), comp=p[1], field=p[2];
  fetch('/api/scene/set',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({entity:entity,component:comp,field:field,value:value})}).catch(()=>{});
}
function renderInspector(){
  const el=document.getElementById('inspector');
  const e=entityById(state.selected);
  if(!e){ el.innerHTML='<div class="empty">Select an entity from the tree.</div>'; inspectorInputs={}; state.lastInspectorKey='none'; return; }
  const key=inspectorKey(e);
  if(key!==state.lastInspectorKey){
    el.innerHTML=''; inspectorInputs={};
    const title=document.createElement('div'); title.className='insp-title'; title.textContent=entityLabel(e); el.appendChild(title);
    for(const comp of e.components){
      const card=document.createElement('div'); card.className='card';
      const h=document.createElement('div'); h.className='card-h'; h.textContent=comp.type; card.appendChild(h);
      for(const f of comp.fields){
        const k=e.id+'|'+comp.type+'|'+f.n;
        const field=document.createElement('div'); field.className='field';
        const lab=document.createElement('label'); lab.textContent=f.n; field.appendChild(lab);
        const input=makeInput(k, f.v); field.appendChild(input); inspectorInputs[k]=input;
        card.appendChild(field);
      }
      el.appendChild(card);
    }
    state.lastInspectorKey=key;
  } else {
    updateInspectorValues(e);
  }
}
function updateInspectorValues(e){
  const active=document.activeElement;
  for(const comp of e.components){
    for(const f of comp.fields){
      const k=e.id+'|'+comp.type+'|'+f.n;
      const input=inspectorInputs[k]; if(!input) continue;
      if(input===active) continue;
      if(typeof f.v==='boolean'){ if(input.checked!==!!f.v) input.checked=!!f.v; }
      else { const disp=(f.v==null?'(null)':String(f.v)); if(input.value!==disp) input.value=disp; }
    }
  }
}
function selectedResource(){
  if(!resources) return null;
  for(const r of resources.resources){ if(r.resource===state.resSelected) return r; }
  return null;
}
function buildResList(){
  const el=document.getElementById('reslist'); el.innerHTML='';
  for(const r of (resources?resources.resources:[])){
    const row=document.createElement('div'); row.className='res-row'+(state.resSelected===r.resource?' selected':'');
    let html='<span class="res-cookie">#'+r.resource+'</span>'
      +'<span class="res-type" title="'+(r.streaming?'streamed from disk':'fully loaded')+'">'+escapeHtml(r.type)+'</span>'
      +'<span class="res-path" title="'+escapeHtml(r.path)+'">'+escapeHtml(r.path)+'</span>';
    if(r.cookies&&r.cookies.length>0){
      html+='<span class="res-cookies" title="consumer cookies: '+r.cookies.map(c=>'#'+c).join(', ')+'">'
        +r.cookies.map(c=>'#'+c).join(' ')+'</span>';
    }
    row.innerHTML=html;
    row.onclick=()=>{ state.resSelected=r.resource; previewCookie=null; saveState(); buildResList(); renderPreview(); };
    el.appendChild(row);
  }
}
async function renderPreview(){
  const el=document.getElementById('preview');
  const r=selectedResource();
  if(!r){ el.innerHTML='<div class="empty">Select a resource to preview.</div>'; previewCookie=null; return; }
  if(state.resSelected===previewCookie) return;
  previewCookie=state.resSelected;
  // The image endpoint is keyed by consumer cookie; any live one resolves to
  // the same resource.
  const cookie=(r.cookies&&r.cookies.length>0)?r.cookies[0]:null;
  if(cookie==null){ el.innerHTML='<div class="empty">No image available.</div>'; return; }
  el.innerHTML='<div class="empty">loading...</div>';
  try{
    const resp=await fetch('/api/resource/image?cookie='+cookie);
    if(!resp.ok){ el.innerHTML='<div class="empty">No image available.</div>'; return; }
    const w=parseInt(resp.headers.get('X-Width'),10), h=parseInt(resp.headers.get('X-Height'),10);
    const buf=new Uint8ClampedArray(await resp.arrayBuffer());
    el.innerHTML='';
    const canvas=document.createElement('canvas'); canvas.width=w; canvas.height=h;
    const ctx=canvas.getContext('2d'); const img=ctx.createImageData(w,h); img.data.set(buf); ctx.putImageData(img,0,0);
    el.appendChild(canvas);
    const info=document.createElement('div'); info.className='img-info'; info.textContent=w+' x '+h+'  (RGBA)';
    el.appendChild(info);
  }catch(e){ el.innerHTML='<div class="empty">Error loading image.</div>'; }
}
async function refreshShot(){
  const panel=document.getElementById('shotPanel');
  try{
    const resp=await fetch('/api/screenshot');
    if(!resp.ok){ panel.innerHTML='<div class="empty">Screenshot unavailable.</div>'; return; }
    const w=parseInt(resp.headers.get('X-Width'),10), h=parseInt(resp.headers.get('X-Height'),10);
    const url=URL.createObjectURL(await resp.blob());
    let img=panel.querySelector('img');
    if(!img){
      panel.innerHTML='';
      img=document.createElement('img'); panel.appendChild(img);
    }
    img.src=url;
    img.onload=()=>{ URL.revokeObjectURL(url); };
    document.getElementById('shotInfo').textContent=w+' x '+h+'  '+new Date().toLocaleTimeString();
  }catch(e){}
}
document.getElementById('shotRefresh').onclick=refreshShot;
setInterval(()=>{ if(state.view==='shot' && document.getElementById('shotAuto').checked) refreshShot(); }, POLL_MS);
async function poll(){
  try{
    if(state.view==='scene'){
      const j=await (await fetch('/api/scene')).json(); scene=j;
      const h=treeHash();
      if(h!==state.lastTreeHash){ state.lastTreeHash=h; buildTree(); }
      renderInspector();
    } else if(state.view==='resources'){
      const j=await (await fetch('/api/resources')).json(); resources=j;
      const h=resHash();
      if(h!==state.lastResHash){ state.lastResHash=h; buildResList(); }
      renderPreview();
    } else if(state.view==='systems'){
      const j=await (await fetch('/api/systems')).json(); systems=j;
      const h=sysHash();
      if(h!==state.lastSysHash){ state.lastSysHash=h; renderSystems(); }
    } else if(state.view==='components'){
      const j=await (await fetch('/api/components')).json(); components=j;
      const h=compHash();
      if(h!==state.lastCompHash){ state.lastCompHash=h; buildCompList(); renderComponentInspector(); }
    }
  }catch(e){}
}
setInterval(poll, POLL_MS); poll();
</script>
</body></html>]]
end

-- Everything after the header/body separator (Content-Length is ignored).
local function requestBody(req)
    local i = req:find("\r\n\r\n")
    if i then return req:sub(i + 4) end
    local j = req:find("\n\n")
    if j then return req:sub(j + 2) end
    return ""
end

-- Returns true once the whole request (including any Content-Length body)
-- has been buffered. GET requests are complete at the header terminator.
local function requestComplete(buf)
    local s, e = buf:find("\r\n\r\n")
    if not s then
        s, e = buf:find("\n\n")
    end
    if not s then return false end
    local cl = buf:lower():match("content%-length:%s*(%d+)")
    if cl then
        cl = tonumber(cl)
        local have = #buf - e
        if have < cl then return false end
    end
    return true
end

local function jsonEscape(s)
    return s:gsub("[%z\1-\31\\\"%c]", function(c)
        if c == "\\" then return "\\\\"
        elseif c == '"' then return '\\"'
        elseif c == "\n" then return "\\n"
        elseif c == "\r" then return "\\r"
        elseif c == "\t" then return "\\t"
        else return string.format("\\u%04x", string.byte(c)) end
    end)
end

local function isArray(t)
    local n = 0
    for k, _ in pairs(t) do
        if type(k) ~= "number" then return false end
        n = n + 1
    end
    if n == 0 then return true end
    for i = 1, n do
        if t[i] == nil then return false end
    end
    return true
end

local function jsonEncode(v)
    if v == nil then return "null" end
    local t = type(v)
    if t == "string" then
        return '"' .. jsonEscape(v) .. '"'
    elseif t == "number" then
        if v == math.floor(v) then return tostring(v) end
        return string.format("%.6g", v)
    elseif t == "boolean" then
        return v and "true" or "false"
    elseif t == "function" then
        return '"[Managed Function]"'
    elseif t == "table" then
        if isArray(v) then
            local parts = {}
            for i = 1, #v do parts[#parts + 1] = jsonEncode(v[i]) end
            return "[" .. table.concat(parts, ",") .. "]"
        else
            local parts = {}
            for k, val in pairs(v) do
                if type(k) == "string" then
                    parts[#parts + 1] = '"' .. jsonEscape(k) .. '":' .. jsonEncode(val)
                end
            end
            return "{" .. table.concat(parts, ",") .. "}"
        end
    end
    return "null"
end

-- Minimal JSON parser supporting the object/array/string/number/bool/null
-- shapes produced by this file's clients.
local function jsonDecode(s)
    if type(s) ~= "string" or #s == 0 then return nil end
    local pos = 1
    local function err(msg) error(msg .. " at " .. pos) end
    local function skipws()
        while pos <= #s and s:find("^%s", pos) do pos = pos + 1 end
    end
    local function parse()
        skipws()
        local c = s:sub(pos, pos)
        if c == "{" then
            pos = pos + 1
            local t = {}
            skipws()
            if s:sub(pos, pos) == "}" then pos = pos + 1; return t end
            while true do
                skipws()
                if s:sub(pos, pos) ~= '"' then err("expected string key") end
                local key = parse()
                skipws()
                if s:sub(pos, pos) ~= ":" then err("expected colon") end
                pos = pos + 1
                t[key] = parse()
                skipws()
                local ch = s:sub(pos, pos)
                if ch == "," then pos = pos + 1
                elseif ch == "}" then pos = pos + 1; break
                else err("expected , or }") end
            end
            return t
        elseif c == "[" then
            pos = pos + 1
            local t = {}
            skipws()
            if s:sub(pos, pos) == "]" then pos = pos + 1; return t end
            while true do
                t[#t + 1] = parse()
                skipws()
                local ch = s:sub(pos, pos)
                if ch == "," then pos = pos + 1
                elseif ch == "]" then pos = pos + 1; break
                else err("expected , or ]") end
            end
            return t
        elseif c == '"' then
            pos = pos + 1
            local buf = {}
            while true do
                local ch = s:sub(pos, pos)
                if ch == '"' then pos = pos + 1; break end
                if ch == "\\" then
                    pos = pos + 1
                    local e = s:sub(pos, pos)
                    if e == "n" then buf[#buf + 1] = "\n"
                    elseif e == "t" then buf[#buf + 1] = "\t"
                    elseif e == "r" then buf[#buf + 1] = "\r"
                    elseif e == '"' then buf[#buf + 1] = '"'
                    elseif e == "\\" then buf[#buf + 1] = "\\"
                    elseif e == "/" then buf[#buf + 1] = "/"
                    elseif e == "u" then
                        local code = tonumber(s:sub(pos + 1, pos + 4), 16)
                        pos = pos + 4
                        if code < 0x80 then buf[#buf + 1] = string.char(code)
                        elseif code < 0x800 then buf[#buf + 1] = string.char(0xC0 + (code >> 6), 0x80 + (code & 0x3F))
                        else buf[#buf + 1] = string.char(0xE0 + (code >> 12), 0x80 + ((code >> 6) & 0x3F), 0x80 + (code & 0x3F)) end
                    else buf[#buf + 1] = e end
                    pos = pos + 1
                else
                    buf[#buf + 1] = ch
                    pos = pos + 1
                end
            end
            return table.concat(buf)
        elseif c == "t" then pos = pos + 4; return true
        elseif c == "f" then pos = pos + 5; return false
        elseif c == "n" then pos = pos + 4; return nil
        else
            local m = s:match("^[%-%d%.eE%+]+", pos)
            if not m then err("unexpected character") end
            pos = pos + #m
            return tonumber(m)
        end
    end
    return parse()
end

-- Splits "path?a=b&c=d" into the path and a params table.
local function splitPath(path)
    local qpos = path:find("?", 1, true)
    if not qpos then return path, {} end
    local base = path:sub(1, qpos - 1)
    local qs = path:sub(qpos + 1)
    local params = {}
    for kv in qs:gmatch("[^&]+") do
        local k, v = kv:match("^([^=]+)=?(.*)$")
        if k then params[k] = v or "" end
    end
    return base, params
end

-- Field names come from the engine API (scene.getFieldNames) so the inspector
-- always reflects the real component API instead of a hard-coded copy.
local function componentFieldNames(comp)
    local ok, names = pcall(scene.getFieldNames, comp)
    return ok and names or {}
end

-- Builds the full scene-tree snapshot as a JSON-friendly table.
local function buildScene()
    local out = { entities = {} }
    local ids = scene.allEntities()
    for _, id in ipairs(ids) do
        local ent = scene.entityFromId(id)
        if ent then
            local node = {
                id = id,
                name = scene.getField(ent, "name"),
                parent = scene.parent(ent),
                children = scene.children(ent),
                components = {},
            }
            local comps = scene.getComponents(ent)
            for _, ctype in ipairs(comps) do
                local comp = scene.component(ent, ctype)
                local fields = {}
                if comp then
                    for _, f in ipairs(componentFieldNames(comp)) do
                        fields[#fields + 1] = { n = f, v = scene.getField(comp, f) }
                    end
                end
                node.components[#node.components + 1] = { type = ctype, fields = fields }
            end
            out.entities[#out.entities + 1] = node
        end
    end
    return out
end

-- Builds the resource-manager snapshot as a JSON-friendly table.
local function buildResources()
    return { resources = gfx.listResources() }
end

local function respond(sock, status, contentType, body, headers)
    local header = "HTTP/1.1 " .. status .. "\r\n"
        .. "Content-Type: " .. contentType .. "\r\n"
    if headers then
        for _, h in ipairs(headers) do
            header = header .. h .. "\r\n"
        end
    end
    header = header
        .. "Content-Length: " .. #body .. "\r\n"
        .. "Connection: close\r\n\r\n"
    sock:write(header .. body)
    sock:flush()
    sock:dispose()
end

-- A socket awaiting the screenshot requested on a previous tick (the grab
-- completes inside the next present, handled on a later tick). Declared
-- before handle() so the route can park its socket in it. The grab takes one
-- full frame, so deferring the response lets the download land before we
-- respond; give up after a few frames if no present ever serviced it.
local pendingShot = nil
local pendingShotTicks = 0

local function handle(sock, req)
    sock.noDelay = true

    local line = req:match("^([^\r\n]*)")
    local method, path = line:match("^(%u+)%s+([^%s]+)")
    if not method then
        respond(sock, "400 Bad Request", "text/plain", "bad request")
        return
    end

    local base, params = splitPath(path)

    if method == "GET" and base == "/" then
        -- Never cache the page: the served JS/CSS changes whenever web.lua is
        -- edited (and hot-reloaded), so a stale cache copy silently loses new
        -- behaviour (e.g. the /api/inspector/select POST).
        respond(sock, "200 OK", "text/html; charset=utf-8", renderPage(pollMs), { "Cache-Control: no-store" })
    elseif method == "GET" and base == "/api/scene" then
        local ok, sc = pcall(buildScene)
        respond(sock, "200 OK", "application/json", jsonEncode(ok and sc or { entities = {} }))
    elseif method == "GET" and base == "/api/resources" then
        local ok, rs = pcall(buildResources)
        respond(sock, "200 OK", "application/json", jsonEncode(ok and rs or { resources = {} }))
    elseif method == "GET" and base == "/api/systems" then
        -- Native app-shell systems and Lua-managed ECS systems share one
        -- registry, so a single list is enough.
        local ok, sys = pcall(re.systems)
        local out = { systems = ok and type(sys) == "table" and sys or {} }
        respond(sock, "200 OK", "application/json", jsonEncode(out))
    elseif method == "GET" and base == "/api/components" then
        local ok, comps = pcall(re.components)
        local out = { components = ok and type(comps) == "table" and comps or {} }
        respond(sock, "200 OK", "application/json", jsonEncode(out))
    elseif method == "GET" and base == "/api/resource/image" then
        local cookie = tonumber(params.cookie or "")
        if not cookie then
            respond(sock, "400 Bad Request", "text/plain", "missing cookie")
            return
        end
        local img = gfx.getResourceImage(cookie)
        if not img then
            respond(sock, "404 Not Found", "text/plain", "no image for cookie " .. tostring(cookie))
            return
        end
        respond(sock, "200 OK", "application/octet-stream", img.data,
            { "X-Width: " .. img.width, "X-Height: " .. img.height })
    elseif method == "GET" and base == "/api/screenshot" then
        if not gfx.requestScreenshot then
            respond(sock, "501 Not Implemented", "text/plain", "screenshot is not available")
        elseif pendingShot ~= nil then
            -- One grab per present; a second request while one is already
            -- pending would silently orphan the parked socket.
            respond(sock, "503 Service Unavailable", "text/plain", "a screenshot request is already in progress")
        else
            local ok = gfx.requestScreenshot()
            if ok == false then
                respond(sock, "503 Service Unavailable", "text/plain", "no frame has been presented yet")
            else
                -- The grab completes inside the next present; poll for it on
                -- the next few ticks once the renderer has produced a frame.
                pendingShot = sock
                pendingShotTicks = 0
            end
        end
    elseif method == "POST" and base == "/api/scene/set" then
        local body = requestBody(req)
        local ok, reqj = pcall(jsonDecode, body)
        local res
        if ok and type(reqj) == "table" then
            local ent = scene.entityFromId(tonumber(reqj.entity))
            local comp = ent and scene.component(ent, reqj.component)
            if comp then
                local ok2, err = pcall(scene.setField, comp, reqj.field, reqj.value)
                res = ok2 and '{"ok":true}' or '{"ok":false,"error":"' .. jsonEscape(tostring(err)) .. '"}'
            else
                res = '{"ok":false,"error":"entity or component not found"}'
            end
        else
            res = '{"ok":false,"error":"invalid request"}'
        end
        respond(sock, "200 OK", "application/json", res)
    elseif method == "POST" and base == "/api/eval" then
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
    elseif method == "POST" and base == "/api/input/set" then
        -- Arms automated input injection (see the InputHandler sample). The
        -- commands stay active until /api/input/clear — a held key.
        if webInputEnabled then
            local okj, obj = pcall(jsonDecode, requestBody(req))
            if okj and type(obj) == "table" then
                webInput.active = true
                webInput.state = type(obj.state) == "table" and obj.state or {}
                webInput.press = type(obj.press) == "table" and obj.press or {}
                respond(sock, "200 OK", "application/json", '{"ok":true}')
            else
                respond(sock, "400 Bad Request", "application/json", '{"ok":false,"error":"bad body"}')
            end
        else
            respond(sock, "200 OK", "application/json", '{"ok":false,"error":"input injection disabled (set OPENRE_WEB_INPUT=1)"}')
        end
    elseif method == "POST" and base == "/api/input/tap" then
        -- Arms a single auto-clearing edge: the commands are injected for a
        -- short window, then cleared automatically. Exactly the set/clear
        -- dance a test driver would otherwise perform by hand.
        if webInputEnabled then
            local okj, obj = pcall(jsonDecode, requestBody(req))
            if okj and type(obj) == "table" then
                -- ms is converted to frames at a nominal 60fps; a caller that
                -- knows the real frame rate can pass exact ticks instead.
                local ms = tonumber(obj.ms) or 200
                local ticks = tonumber(obj.ticks) or math.max(1, math.floor(ms / (1000 / 60)))
                -- Default to "accept" so a bare POST /api/input/tap steps one
                -- menu item, matching how drivers historically used set+clear.
                webInput.state = type(obj.state) == "table" and obj.state or {}
                webInput.press = type(obj.press) == "table" and obj.press or { "accept" }
                webInput.active = true
                webInput.tapTicks = ticks
                respond(sock, "200 OK", "application/json",
                    '{"ok":true,"press":"' .. table.concat(webInput.press, ",") .. '","ticks":' .. ticks .. '}')
            else
                respond(sock, "400 Bad Request", "application/json", '{"ok":false,"error":"bad body"}')
            end
        else
            respond(sock, "200 OK", "application/json", '{"ok":false,"error":"input injection disabled (set OPENRE_WEB_INPUT=1)"}')
        end
    elseif method == "POST" and base == "/api/input/clear" then
        webInput.active = false
        webInput.state = {}
        webInput.press = {}
        webInput.tapTicks = 0
        respond(sock, "200 OK", "application/json", '{"ok":true}')
    elseif method == "GET" and base == "/api/input" then
        respond(sock, "200 OK", "application/json",
            jsonEncode({ enabled = webInputEnabled, active = webInput.active, state = webInput.state, press = webInput.press }))
    elseif method == "POST" and base == "/api/inspector/select" then
        -- Sets the entity the in-game inspector guide lines track, or clears
        -- them when `entity` is null (or the scene is rebuilt under it).
        local okj, obj = pcall(jsonDecode, requestBody(req))
        if not okj or type(obj) ~= "table" then
            respond(sock, "400 Bad Request", "application/json", '{"ok":false,"error":"bad body"}')
        else
            inspectorSel = tonumber(obj.entity) or nil
            respond(sock, "200 OK", "application/json", '{"ok":true}')
        end
    elseif method == "GET" and base == "/api/health" then
        respond(sock, "200 OK", "application/json",
            jsonEncode({ ok = true, port = port, pollMs = pollMs, input = webInputEnabled }))
    elseif method == "POST" and base == "/api/quit" then
        -- Graceful shutdown for test drivers: lets the engine unwind and the
        -- process exit cleanly, instead of leaving a detached game orphaned.
        local ok, err = pcall(scene.exit)
        if ok then
            respond(sock, "200 OK", "application/json", '{"ok":true,"quitting":true}')
        else
            respond(sock, "200 OK", "application/json",
                '{"ok":false,"error":"' .. jsonEscape(tostring(err)) .. '"}')
        end
    else
        respond(sock, "404 Not Found", "text/plain", "404 not found")
    end
end

-- Requests are received across frames: read() returns whatever the network
-- thread has buffered so far (best-effort), so we accumulate until the
-- header terminator arrives, then handle the request.
local inflight = {}

-- Managed-system harness for this script VM, mirroring ecs.lua: the engine
-- dispatches registered systems through the runner installed with
-- re.setSystemRunner (kept per-VM, so this coexists with the game module's
-- own runner). We only drive a single per-frame pump system here.
local webSystemsById = {}
re.resetManagedSystems() -- drop stale web.Pump defs from an earlier web.lua load

local function webRunSystem(id)
    local fn = webSystemsById[id]
    if fn then
        local ok, err = pcall(fn)
        if not ok then
            print("web: managed system failed: " .. tostring(err))
        end
    end
end
re.setSystemRunner(webRunSystem)

webSystemsById[1] = function()
    -- Install the InputHandler sample once the scene exists.
    installWebInputHandler()

    -- Keep the inspector guide lines on the selected entity.
    local iok, ierr = pcall(updateInspectorOverlay)
    if not iok then
        print("web: inspector overlay error: " .. tostring(ierr))
    end

    -- A tap's expiry is driven from the web_input InputHandler (which runs at
    -- the throttled input-walk cadence) rather than counted in frames here:
    -- web.Pump and the walk can still land on different frames within the
    -- paced cadence, and counting from the pump side would expire a taps=1 edge
    -- a frame early. The handler consumes one tick per injection instead.
    -- (see installWebInputHandler)

    -- Serve a requested screenshot once the renderer has grabbed the frame.
    if pendingShot and gfx.takeScreenshot then
        local ok, shot = pcall(gfx.takeScreenshot)
        if ok and type(shot) == "table" and type(shot.data) == "string" then
            local sock = pendingShot
            pendingShot = nil
            pendingShotTicks = 0
            respond(sock, "200 OK", "image/bmp", shot.data,
                { "X-Width: " .. tostring(shot.width), "X-Height: " .. tostring(shot.height) })
        else
            pendingShotTicks = pendingShotTicks + 1
            if pendingShotTicks >= 8 then
                local sock = pendingShot
                pendingShot = nil
                pendingShotTicks = 0
                respond(sock, "503 Service Unavailable", "text/plain", "no frame has been presented")
            end
        end
    end

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
        if requestComplete(buf) then
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
end
-- Run after core.Module, whose pad_set() performs the input walk. A tap
-- armed here (POST /api/input/tap) is injected by the *next* paced frame's
-- input walk and consumed by the web_input handler at the walk cadence, so a
-- ticks=1 tap yields exactly one edge (same as the old tick hook cadence).
re.registerSystem("web.Pump", { "core.Module" }, {}, "default", 1)


