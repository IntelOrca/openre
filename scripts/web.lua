-- web.lua — minimal debug web server for the OpenRE Live Console: serves a
-- self-contained HTML page with live game state and a Lua eval box.

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
.field label{flex:0 0 150px;color:var(--muted);font-family:ui-monospace,monospace;font-size:12px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
.fval{flex:1;background:var(--bg);border:1px solid var(--border);color:var(--text);border-radius:6px;padding:5px 8px;font-family:ui-monospace,monospace;font-size:12px;min-width:0;}
.fval:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 2px rgba(88,166,255,.25);}
.fval:disabled{color:var(--muted);opacity:.6;}
.reslist-panel{width:340px;background:var(--panel);border-right:1px solid var(--border);overflow:auto;}
.res-row{display:flex;align-items:center;gap:8px;padding:8px 10px;border-bottom:1px solid rgba(48,54,61,.5);cursor:pointer;font-size:12px;}
.res-row:hover{background:var(--panel2);}
.res-row.selected{background:rgba(88,166,255,.15);}
.res-cookie{font-family:ui-monospace,monospace;color:var(--accent);flex:0 0 48px;}
.res-type{color:var(--accent2);flex:0 0 56px;text-transform:uppercase;font-size:11px;}
.res-path{flex:1;color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
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
</div>
<script>
let POLL_MS = ]] .. tostring(pollMs) .. [[;
const pm = new URLSearchParams(location.search).get('poll');
if (pm) POLL_MS = parseInt(pm, 10) || POLL_MS;
const LS_KEY = 'openre.inspector.v1';
let state = { view:'scene', selected:null, expanded:{}, resSelected:null,
  lastTreeHash:'', lastResHash:'', lastInspectorKey:'none' };
let scene = null, resources = null;
let inspectorInputs = {};
let previewCookie = null;
try { const s = JSON.parse(localStorage.getItem(LS_KEY)); if (s) Object.assign(state, s); } catch(_) {}
function saveState(){
  try { localStorage.setItem(LS_KEY, JSON.stringify({ view:state.view, selected:state.selected, expanded:state.expanded, resSelected:state.resSelected })); } catch(_) {}
}
document.getElementById('pollLabel').textContent = POLL_MS;
function switchView(v){
  state.view=v; saveState();
  document.querySelectorAll('.menu-item').forEach(e=>e.classList.toggle('active', e.dataset.view===v));
  document.getElementById('sceneView').classList.toggle('active', v==='scene');
  document.getElementById('resourcesView').classList.toggle('active', v==='resources');
  if (v==='resources') previewCookie=null;
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
  return (resources.resources||[]).map(r=>r.cookie+':'+r.type+':'+r.path).join(';');
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
}
function escapeHtml(s){ return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c])); }
function makeInput(key, val){
  let input;
  if(typeof val==='boolean'){ input=document.createElement('input'); input.type='checkbox'; input.checked=!!val; input.onchange=()=>setField(key, input.checked); }
  else if(typeof val==='number'){ input=document.createElement('input'); input.type='number'; input.step='any'; input.value=String(val); input.onchange=()=>setField(key, parseFloat(input.value)); }
  else if(val==null){ input=document.createElement('input'); input.type='text'; input.value='(null)'; input.disabled=true; }
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
function buildResList(){
  const el=document.getElementById('reslist'); el.innerHTML='';
  for(const r of (resources?resources.resources:[])){
    const row=document.createElement('div'); row.className='res-row'+(state.resSelected===r.cookie?' selected':'');
    row.innerHTML='<span class="res-cookie">#'+r.cookie+'</span><span class="res-type">'+escapeHtml(r.type)+'</span><span class="res-path">'+escapeHtml(r.path)+'</span>';
    row.onclick=()=>{ state.resSelected=r.cookie; previewCookie=null; saveState(); buildResList(); renderPreview(); };
    el.appendChild(row);
  }
}
async function renderPreview(){
  const el=document.getElementById('preview');
  if(state.resSelected==null){ el.innerHTML='<div class="empty">Select a resource to preview.</div>'; previewCookie=null; return; }
  if(state.resSelected===previewCookie) return;
  previewCookie=state.resSelected;
  el.innerHTML='<div class="empty">loading...</div>';
  try{
    const resp=await fetch('/api/resource/image?cookie='+state.resSelected);
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
async function poll(){
  try{
    if(state.view==='scene'){
      const j=await (await fetch('/api/scene')).json(); scene=j;
      const h=treeHash();
      if(h!==state.lastTreeHash){ state.lastTreeHash=h; buildTree(); }
      renderInspector();
    } else {
      const j=await (await fetch('/api/resources')).json(); resources=j;
      const h=resHash();
      if(h!==state.lastResHash){ state.lastResHash=h; buildResList(); }
      renderPreview();
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

-- Field schema for each component type, in display order. Keep in sync with
-- the component names returned by scene.getComponents.
local SCENE_SCHEMA = {
    ["core.transform"] = { "name", "x", "y", "z", "scale.x", "scale.y", "scale.z" },
    ["core.sprite"] = { "size.width", "size.height", "src.x", "src.y", "src.width", "src.height", "texture", "flags" },
    ["core.text"] = { "text", "size.width", "size.height", "flags", "font" },
    ["core.legacy"] = { "enabled", "size.width", "size.height" },
    ["core.video"] = { "status", "position", "videoWidth", "videoHeight", "resource" },
}

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
                    for _, f in ipairs(SCENE_SCHEMA[ctype] or {}) do
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
        respond(sock, "200 OK", "text/html; charset=utf-8", renderPage(pollMs))
    elseif method == "GET" and base == "/api/scene" then
        local ok, sc = pcall(buildScene)
        respond(sock, "200 OK", "application/json", jsonEncode(ok and sc or { entities = {} }))
    elseif method == "GET" and base == "/api/resources" then
        local ok, rs = pcall(buildResources)
        respond(sock, "200 OK", "application/json", jsonEncode(ok and rs or { resources = {} }))
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
end)

