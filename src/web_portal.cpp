/**
 * @file web_portal.cpp
 * @brief WiFi web portal — ESPAsyncWebServer with REST API and embedded UI.
 *
 * Architecture:
 *   - WiFi runs in AP+STA mode (AP always available for direct connection)
 *   - ESPAsyncWebServer handles HTTP requests asynchronously on Core 0
 *   - REST API endpoints read/write current_params via dsp_params_mutex
 *   - Single-page web app is served as an embedded HTML string
 *   - The web app polls /api/status every 200ms for live VU meter
 *   - All DSP parameter changes go through the same mutex as the physical UI
 *
 * Thread safety:
 *   - GET endpoints: take mutex, copy params to local, release mutex, serialize
 *   - POST endpoints: parse JSON, take mutex, update params, release mutex
 *   - The DSP task also takes the same mutex when reading params
 */

#include "web_portal.h"
#include "globals.h"
#include "dsp_config.h"
#include "preset.h"

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// ─────────────────────────────────────────────────────────────
// Server instance
// ─────────────────────────────────────────────────────────────
static AsyncWebServer server(WEB_SERVER_PORT);

// ─────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────
static void setup_wifi();
static void setup_routes();
static String build_json_status();
static String build_json_params();
static String build_json_presets();
static void apply_param_path(dsp_params_t &p, const char *path, float value);

// ─────────────────────────────────────────────────────────────
// Embedded web app (single-page, served as PROGMEM string)
// ─────────────────────────────────────────────────────────────
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no"><title>ESP32-DSP</title>
<style>*{box-sizing:border-box;margin:0;padding:0}:root{--bg:#1a1a2e;--card:#16213e;--acc:#0f3460;--hl:#e94560;--tx:#eee;--td:#8899aa;--ok:#4ecca3;--wn:#ffc107}body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:var(--bg);color:var(--tx);min-height:100vh;padding-bottom:80px}
.hd{background:var(--acc);padding:12px;text-align:center;position:sticky;top:0;z-index:100;box-shadow:0 2px 10px rgba(0,0,0,.3)}.hd h1{font-size:1.3em;margin-bottom:4px}.hd .st{font-size:.8em;color:var(--td)}.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:4px}.d-g{background:var(--ok)}.d-r{background:var(--hl)}.d-y{background:var(--wn)}
.tb{display:flex;overflow-x:auto;background:var(--card);position:sticky;top:52px;z-index:99;box-shadow:0 2px 8px rgba(0,0,0,.2)}.t{flex:1;min-width:60px;padding:10px 4px;text-align:center;font-size:.75em;color:var(--td);cursor:pointer;border-bottom:3px solid transparent;white-space:nowrap}.t.a{color:var(--hl);border-bottom-color:var(--hl)}
.p{display:none;padding:12px}.p.a{display:block}
.c{background:var(--card);border-radius:8px;padding:12px;margin-bottom:12px;box-shadow:0 2px 6px rgba(0,0,0,.2)}.c h3{font-size:.9em;color:var(--td);margin-bottom:8px;text-transform:uppercase;letter-spacing:1px}
.vu{background:#000;border-radius:4px;height:20px;overflow:hidden;margin:8px 0}.vu-b{height:100%;width:0%;background:linear-gradient(90deg,#4ecca3,#ffc107,#e94560);transition:width .1s;border-radius:4px}.vu-l{display:flex;justify-content:space-between;font-size:.7em;color:var(--td);margin-top:2px}
.sl{display:flex;align-items:center;margin-bottom:10px}.sl label{width:90px;font-size:.8em;color:var(--td);flex-shrink:0}.sl input[type=range]{flex:1;-webkit-appearance:none;height:6px;background:var(--acc);border-radius:3px;outline:none}.sl input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;border-radius:50%;background:var(--hl);cursor:pointer}.sl .v{width:60px;text-align:right;font-size:.8em;color:var(--ok);font-family:monospace}
.btn{display:inline-block;padding:8px 16px;border:none;border-radius:8px;cursor:pointer;font-size:.85em;font-weight:600;text-transform:uppercase;letter-spacing:.5px}.btn-p{background:var(--hl);color:#fff}.btn-s{background:var(--acc);color:var(--tx)}.btn-g{background:var(--ok);color:#1a1a2e}.br{display:flex;gap:8px}.br .btn{flex:1}
.tr{display:flex;align-items:center;justify-content:space-between;margin-bottom:10px}.tr label{font-size:.85em}
.tg{width:44px;height:24px;background:var(--acc);border-radius:12px;position:relative;cursor:pointer;transition:background .3s}.tg.on{background:var(--ok)}.tg::after{content:'';position:absolute;width:20px;height:20px;background:#fff;border-radius:50%;top:2px;left:2px;transition:left .3s}.tg.on::after{left:22px}
.ec{width:100%;height:200px;background:#000;border-radius:4px;margin-bottom:8px;cursor:crosshair}.el{display:flex;justify-content:space-between;font-size:.65em;color:(--td)}
.pg{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}.pb{padding:10px;background:var(--acc);border:none;border-radius:8px;color:var(--tx);cursor:pointer;font-size:.8em;text-align:center}.pb:hover{background:var(--hl)}.pb.a{background:var(--ok);color:#1a1a2e}.pb .n{font-weight:600;display:block}.pb .d{font-size:.7em;color:var(--td)}
.sg{display:grid;grid-template-columns:1fr 1fr;gap:8px}.si{background:var(--acc);border-radius:8px;padding:10px;text-align:center}.si .l{font-size:.7em;color:var(--td);text-transform:uppercase}.si .v{font-size:1.1em;font-weight:600;color:var(--ok);font-family:monospace}
.toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);background:var(--ok);color:#1a1a2e;padding:10px 20px;border-radius:8px;font-size:.85em;font-weight:600;opacity:0;transition:opacity .3s;z-index:200;pointer-events:none}.toast.show{opacity:1}
</style></head><body>
<div class="hd"><h1>&#x1F3A7; ESP32-DSP</h1><div class="st"><span class="dot" id="cd"></span><span id="ct">Connecting...</span>&nbsp;|&nbsp;BT: <span id="bt">--</span>&nbsp;|&nbsp;Preset: <span id="pn">1/8</span></div></div>
<div class="tb"><div class="t a" onclick="showTab(0)" id="tab0">&#x1F4CA; Status</div><div class="t" onclick="showTab(1)" id="tab1">&#x1F39A; EQ</div><div class="t" onclick="showTab(2)" id="tab2">&#x1F50A; Dynamics</div><div class="t" onclick="showTab(3)" id="tab3">&#x1F500; Xover</div><div class="t" onclick="showTab(4)" id="tab4">&#x1F4BE; Presets</div></div>
<div class="p a" id="panel0"><div class="c"><h3>RMS Level</h3><div class="vu"><div class="vu-b" id="vuBar"></div></div><div class="vu-l"><span>-60dB</span><span id="vuDb">-- dB</span><span>0dB</span></div></div><div class="c"><h3>Quick Controls</h3><div class="br" style="margin-bottom:8px"><button class="btn btn-p" id="muteBtn" onclick="toggleMute()">&#x1F507; Mute</button><button class="btn btn-s" id="bypassBtn" onclick="toggleBypass()">&#x1F500; Bypass</button></div><div class="sl"><label>Master Vol</label><input type="range" id="masterVol" min="0" max="100" value="80" oninput="updateParam('master_volume',this.value/100)" onchange="sendParam('master_volume',this.value/100)"><span class="v" id="masterVolVal">80%</span></div></div><div class="c"><h3>System Status</h3><div class="sg"><div class="si"><div class="l">BT State</div><div class="v" id="btG">--</div></div><div class="si"><div class="l">WiFi RSSI</div><div class="v" id="wr">-- dBm</div></div><div class="si"><div class="l">Preset</div><div class="v" id="ap">1</div></div><div class="si"><div class="l">CPU</div><div class="v" id="cpu">--%</div></div></div></div></div>
<div class="p" id="panel1"><div class="c"><h3>EQ Curve</h3><canvas class="ec" id="eqCanvas" width="360" height="200"></canvas><div class="el"><span>20Hz</span><span>100Hz</span><span>500Hz</span><span>1kHz</span><span>5kHz</span><span>10kHz</span><span>20kHz</span></div></div><div id="eqBands"></div></div>
<div class="p" id="panel2"><div class="c"><h3>Woofer (Low)</h3><div class="tr"><label>Compressor</label><div class="tg" id="cLT" onclick="toggleSwitch('low_driver.compressor.enabled',this)"></div></div><div class="sl"><label>Threshold</label><input type="range" id="cLTs" min="-60" max="0" value="-12" onchange="sendParam('low_driver.compressor.threshold_db',this.value)"><span class="v" id="cLTsV">-12dB</span></div><div class="sl"><label>Ratio</label><input type="range" id="cLR" min="1" max="20" value="2" step="0.5" onchange="sendParam('low_driver.compressor.ratio',this.value)"><span class="v" id="cLRV">2:1</span></div><div class="sl"><label>Attack</label><input type="range" id="cLA" min="1" max="100" value="10" onchange="sendParam('low_driver.compressor.attack_ms',this.value)"><span class="v" id="cLAV">10ms</span></div><div class="sl"><label>Release</label><input type="range" id="cLRl" min="10" max="1000" value="100" onchange="sendParam('low_driver.compressor.release_ms',this.value)"><span class="v" id="cLRlV">100ms</span></div><div class="tr"><label>Limiter</label><div class="tg on" id="lLT" onclick="toggleSwitch('low_driver.limiter.enabled',this)"></div></div><div class="sl"><label>Ceiling</label><input type="range" id="lLTs" min="-20" max="0" value="-3" onchange="sendParam('low_driver.limiter.threshold_db',this.value)"><span class="v" id="lLTsV">-3dB</span></div></div>
<div class="c"><h3>Tweeter (High)</h3><div class="tr"><label>Compressor</label><div class="tg" id="cHT" onclick="toggleSwitch('high_driver.compressor.enabled',this)"></div></div><div class="sl"><label>Threshold</label><input type="range" id="cHTs" min="-60" max="0" value="-12" onchange="sendParam('high_driver.compressor.threshold_db',this.value)"><span class="v" id="cHTsV">-12dB</span></div><div class="sl"><label>Ratio</label><input type="range" id="cHR" min="1" max="20" value="2" step="0.5" onchange="sendParam('high_driver.compressor.ratio',this.value)"><span class="v" id="cHRV">2:1</span></div><div class="sl"><label>Attack</label><input type="range" id="cHA" min="1" max="100" value="10" onchange="sendParam('high_driver.compressor.attack_ms',this.value)"><span class="v" id="cHAV">10ms</span></div><div class="sl"><label>Release</label><input type="range" id="cHRl" min="10" max="1000" value="100" onchange="sendParam('high_driver.compressor.release_ms',this.value)"><span class="v" id="cHRlV">100ms</span></div><div class="tr"><label>Limiter</label><div class="tg on" id="lHT" onclick="toggleSwitch('high_driver.limiter.enabled',this)"></div></div><div class="sl"><label>Ceiling</label><input type="range" id="lHTs" min="-20" max="0" value="-3" onchange="sendParam('high_driver.limiter.threshold_db',this.value)"><span class="v" id="lHTsV">-3dB</span></div></div></div>
<div class="p" id="panel3"><div class="c"><h3>Crossover</h3><div class="sl"><label>Frequency</label><input type="range" id="xf" min="200" max="4000" value="2000" step="50" onchange="sendParam('crossover_hz',this.value)"><span class="v" id="xfV">2000 Hz</span></div><div class="sl"><label>Low Gain</label><input type="range" id="lg" min="0" max="200" value="100" onchange="sendParam('low_gain',this.value/100)"><span class="v" id="lgV">1.00x</span></div><div class="sl"><label>High Gain</label><input type="range" id="hg" min="0" max="200" value="100" onchange="sendParam('high_gain',this.value/100)"><span class="v" id="hgV">1.00x</span></div></div><div class="c"><h3>Delay</h3><div class="sl"><label>Low Delay</label><input type="range" id="ld" min="0" max="200" value="0" onchange="sendParam('low_driver.delay.samples',this.value*4410)"><span class="v" id="ldV">0.0 ms</span></div><div class="tr"><label>Low On</label><div class="tg" id="ldT" onclick="toggleSwitch('low_driver.delay.enabled',this)"></div></div><div class="sl"><label>High Delay</label><input type="range" id="hd" min="0" max="200" value="0" onchange="sendParam('high_driver.delay.samples',this.value*4410)"><span class="v" id="hdV">0.0 ms</span></div><div class="tr"><label>High On</label><div class="tg" id="hdT" onclick="toggleSwitch('high_driver.delay.enabled',this)"></div></div></div></div>
<div class="p" id="panel4"><div class="c"><h3>Presets</h3><div class="pg" id="pg"></div></div><div class="c"><h3>Save Current</h3><button class="btn btn-g" style="width:100%" onclick="savePreset()">&#x1F4BE; Save to Active Slot</button></div></div>
<div class="toast" id="toast"></div>
<script>
let params={},pollTimer=null;
function showTab(n){document.querySelectorAll('.p').forEach((p,i)=>p.classList.toggle('a',i===n));document.querySelectorAll('.t').forEach((t,i)=>t.classList.toggle('a',i===n));if(n===1)setTimeout(drawEQ,50)}
function startPolling(){pollTimer=setInterval(fetchStatus,200)}
async function fetchStatus(){try{const r=await fetch('/api/status');const d=await r.json();updateStatus(d);document.getElementById('cd').className='dot d-g';document.getElementById('ct').textContent='Connected'}catch(e){document.getElementById('cd').className='dot d-r';document.getElementById('ct').textContent='Disconnected'}}
function updateStatus(d){let db=d.rms_level>0?20*Math.log10(d.rms_level/32768):-60;let pct=Math.max(0,Math.min(100,(db+60)*1.667));document.getElementById('vuBar').style.width=pct+'%';document.getElementById('vuDb').textContent=db.toFixed(1)+' dB';const bt=['Disconnected','Connected','Playing'];document.getElementById('bt').textContent=bt[d.bt_state]||'--';document.getElementById('btG').textContent=bt[d.bt_state]||'--';document.getElementById('pn').textContent=(d.active_preset+1)+'/8';document.getElementById('ap').textContent=d.active_preset+1;document.getElementById('wr').textContent=(d.wifi_rssi||0)+' dBm';const mb=document.getElementById('muteBtn');if(d.mute){mb.style.background='#e94560';mb.innerHTML='&#x1F507; MUTED'}else{mb.style.background='';mb.innerHTML='&#x1F507; Mute'}const bb=document.getElementById('bypassBtn');if(d.bypass){bb.style.background='#ffc107';bb.innerHTML='&#x1F500; BYPASSED'}else{bb.style.background='';bb.innerHTML='&#x1F500; Bypass'}}
async function fetchParams(){try{const r=await fetch('/api/params');params=await r.json();populateControls();drawEQ()}catch(e){console.log(e)}}
function populateControls(){if(!params.master_volume)return;document.getElementById('masterVol').value=Math.round(params.master_volume*100);document.getElementById('masterVolVal').textContent=Math.round(params.master_volume*100)+'%';document.getElementById('xf').value=params.crossover_hz;document.getElementById('xfV').textContent=Math.round(params.crossover_hz)+' Hz';document.getElementById('lg').value=Math.round(params.low_gain*100);document.getElementById('lgV').textContent=params.low_gain.toFixed(2)+'x';document.getElementById('hg').value=Math.round(params.high_gain*100);document.getElementById('hgV').textContent=params.high_gain.toFixed(2)+'x';const eq=document.getElementById('eqBands');eq.innerHTML='';for(let i=0;i<4;i++){const b=params.eq_bands[i]||{};eq.innerHTML+='<div class="c"><h3>EQ Band '+i+'</h3><div class="tr"><label>Enabled</label><div class="tg'+(b.enabled?' on':'')+'" onclick="toggleSwitch(\'eq_bands.'+i+'.enabled\',this)"></div></div><div class="sl"><label>Freq</label><input type="range" min="20" max="20000" value="'+(b.freq_hz||1000)+'" onchange="sendParam(\'eq_bands.'+i+'.freq_hz\',this.value)"><span class="v">'+(b.freq_hz||1000)+'Hz</span></div><div class="sl"><label>Gain</label><input type="range" min="-12" max="12" value="'+(b.gain_db||0)+'" step="0.5" onchange="sendParam(\'eq_bands.'+i+'.gain_db\',this.value)"><span class="v">'+(b.gain_db||0)+'dB</span></div><div class="sl"><label>Q</label><input type="range" min="1" max="100" value="'+((b.q||1)*10)+'" onchange="sendParam(\'eq_bands.'+i+'.q\',this.value/10)"><span class="v">'+(b.q||1).toFixed(1)+'</span></div></div>'}
const lc=params.low_driver.compressor;document.getElementById('cLT').className='tg'+(lc.enabled?' on':'');document.getElementById('cLTs').value=lc.threshold_db;document.getElementById('cLTsV').textContent=lc.threshold_db+'dB';document.getElementById('cLR').value=lc.ratio;document.getElementById('cLRV').textContent=lc.ratio+':1';document.getElementById('cLA').value=lc.attack_ms;document.getElementById('cLAV').textContent=lc.attack_ms+'ms';document.getElementById('cLRl').value=lc.release_ms;document.getElementById('cLRlV').textContent=lc.release_ms+'ms';const ll=params.low_driver.limiter;document.getElementById('lLT').className='tg'+(ll.enabled?' on':'');document.getElementById('lLTs').value=ll.threshold_db;document.getElementById('lLTsV').textContent=ll.threshold_db+'dB';const hc=params.high_driver.compressor;document.getElementById('cHT').className='tg'+(hc.enabled?' on':'');document.getElementById('cHTs').value=hc.threshold_db;document.getElementById('cHTsV').textContent=hc.threshold_db+'dB';document.getElementById('cHR').value=hc.ratio;document.getElementById('cHRV').textContent=hc.ratio+':1';document.getElementById('cHA').value=hc.attack_ms;document.getElementById('cHAV').textContent=hc.attack_ms+'ms';document.getElementById('cHRl').value=hc.release_ms;document.getElementById('cHRlV').textContent=hc.release_ms+'ms';const hl=params.high_driver.limiter;document.getElementById('lHT').className='tg'+(hl.enabled?' on':'');document.getElementById('lHTs').value=hl.threshold_db;document.getElementById('lHTsV').textContent=hl.threshold_db+'dB';const ld=params.low_driver.delay;document.getElementById('ld').value=ld.samples/4410;document.getElementById('ldV').textContent=(ld.samples*1000/44100).toFixed(1)+' ms';document.getElementById('ldT').className='tg'+(ld.enabled?' on':'');const hd=params.high_driver.delay;document.getElementById('hd').value=hd.samples/4410;document.getElementById('hdV').textContent=(hd.samples*1000/44100).toFixed(1)+' ms';document.getElementById('hdT').className='tg'+(hd.enabled?' on':'')}
function drawEQ(){const c=document.getElementById('eqCanvas');if(!c)return;const ctx=c.getContext('2d');const w=c.width,h=c.height;ctx.fillStyle='#000';ctx.fillRect(0,0,w,h);ctx.strokeStyle='#333';ctx.lineWidth=1;for(let i=0;i<8;i++){let x=i*(w/7);ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,h);ctx.stroke()}for(let i=0;i<5;i++){let y=i*(h/4);ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(w,y);ctx.stroke()}ctx.strokeStyle='#555';ctx.setLineDash([4,4]);ctx.beginPath();ctx.moveTo(0,h/2);ctx.lineTo(w,h/2);ctx.stroke();ctx.setLineDash([]);if(!params.eq_bands)return;ctx.strokeStyle='#4ecca3';ctx.lineWidth=2;ctx.beginPath();for(let px=0;px<w;px++){let freq=20*Math.pow(1000,px/w);let total=0;for(let i=0;i<4;i++){const b=params.eq_bands[i];if(!b||!b.enabled)continue;let dist=Math.abs(Math.log2(freq/b.freq_hz));let g=0;if(i===0){g=freq<b.freq_hz?b.gain_db:b.gain_db*Math.max(0,1-dist*2)}else if(i===3){g=freq>b.freq_hz?b.gain_db:b.gain_db*Math.max(0,1-dist*2)}else{let s=b.freq_hz/b.q;g=b.gain_db*Math.exp(-dist*dist/(2*(s/b.freq_hz)*(s/b.freq_hz)))}total+=g}let py=h/2-(total/24)*(h/2);py=Math.max(0,Math.min(h,py));if(px===0)ctx.moveTo(px,py);else ctx.lineTo(px,py)}ctx.stroke();ctx.fillStyle='#8899aa';ctx.font='10px sans-serif';ctx.fillText('+12dB',4,14);ctx.fillText('0dB',4,h/2+4);ctx.fillText('-12dB',4,h-4)}
async function sendParam(path,value){try{await fetch('/api/params',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path,value:parseFloat(value)})});showToast('Updated')}catch(e){showToast('Error')}}
function updateParam(path,value){if(path==='master_volume')document.getElementById('masterVolVal').textContent=Math.round(value*100)+'%';sendParam(path,value)}
async function toggleSwitch(path,el){el.classList.toggle('on');await sendParam(path,el.classList.contains('on'))}
async function toggleMute(){await sendParam('mute',!params.mute);fetchStatus()}
async function toggleBypass(){await sendParam('bypass',!params.bypass);fetchStatus()}
async function fetchPresets(){try{const r=await fetch('/api/presets');const d=await r.json();const g=document.getElementById('pg');g.innerHTML='';for(let i=0;i<8;i++){const n=(d.presets&&d.presets[i])?d.presets[i]:'Preset '+(i+1);const a=i===d.active?' a':'';g.innerHTML+='<div class="pb'+a+'" onclick="loadPreset('+i+')"><span class="n">'+n+'</span><span class="d">Slot '+(i+1)+'</span></div>'}}catch(e){console.log(e)}}
async function loadPreset(idx){await fetch('/api/presets/load',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({index:idx})});showToast('Preset '+(idx+1)+' loaded');await fetchParams();await fetchPresets();fetchStatus()}
async function savePreset(){const idx=params.active_preset||0;await fetch('/api/presets/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({index:idx})});showToast('Saved to '+(idx+1));await fetchPresets()}
function showToast(msg){const t=document.getElementById('toast');t.textContent=msg;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),1500)}
window.addEventListener('load',()=>{fetchStatus();fetchParams();fetchPresets();startPolling()});
</script></body></html>)rawliteral";

// ─────────────────────────────────────────────────────────────
// JSON builders
// ─────────────────────────────────────────────────────────────

static String build_json_status()
{
    dsp_params_t p;
    bool have_params = false;
    if (xSemaphoreTake(dsp_params_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        p = current_params;
        xSemaphoreGive(dsp_params_mutex);
        have_params = true;
    }

    float rms = global_rms_level;
    int bt = bt_state;
    int preset = have_params ? p.active_preset : 0;
    bool mute = have_params ? p.mute : false;
    bool bypass = have_params ? p.bypass : false;
    int rssi = WiFi.RSSI();

    // Use a reasonable JSON buffer size
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"bt_state\":%d,\"rms_level\":%.1f,\"active_preset\":%u,"
        "\"mute\":%s,\"bypass\":%s,\"wifi_rssi\":%d}",
        bt, rms, preset,
        mute ? "true" : "false",
        bypass ? "true" : "false",
        rssi);
    return String(buf);
}

static String build_json_params()
{
    dsp_params_t p;
    if (xSemaphoreTake(dsp_params_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return "{}";
    }
    p = current_params;
    xSemaphoreGive(dsp_params_mutex);

    // Estimate: dsp_params_t is ~200 bytes, JSON is ~2-3x
    JsonDocument doc;

    doc["master_volume"] = roundf(p.master_volume * 1000.0f) / 1000.0f;
    doc["crossover_hz"] = roundf(p.crossover_hz);
    doc["low_gain"] = roundf(p.low_gain * 100.0f) / 1000.0f;
    doc["high_gain"] = roundf(p.high_gain * 100.0f) / 1000.0f;
    doc["mute"] = p.mute;
    doc["bypass"] = p.bypass;
    doc["active_preset"] = p.active_preset;

    // EQ bands
    JsonArray eq = doc["eq_bands"].to<JsonArray>();
    for (int i = 0; i < EQ_MAX_BANDS; i++) {
        JsonObject band = eq.add<JsonObject>();
        band["enabled"] = p.eq_bands[i].enabled;
        band["freq_hz"] = roundf(p.eq_bands[i].freq_hz);
        band["gain_db"] = roundf(p.eq_bands[i].gain_db * 10.0f) / 10.0f;
        band["q"] = roundf(p.eq_bands[i].q * 10.0f) / 10.0f;
    }

    // Low driver
    JsonObject low = doc["low_driver"].to<JsonObject>();
    JsonObject lowComp = low["compressor"].to<JsonObject>();
    lowComp["enabled"] = p.low_driver.compressor.enabled;
    lowComp["threshold_db"] = roundf(p.low_driver.compressor.threshold_db);
    lowComp["ratio"] = roundf(p.low_driver.compressor.ratio * 10.0f) / 10.0f;
    lowComp["attack_ms"] = roundf(p.low_driver.compressor.attack_ms);
    lowComp["release_ms"] = roundf(p.low_driver.compressor.release_ms);
    lowComp["makeup_db"] = roundf(p.low_driver.compressor.makeup_db);
    JsonObject lowLim = low["limiter"].to<JsonObject>();
    lowLim["enabled"] = p.low_driver.limiter.enabled;
    lowLim["threshold_db"] = roundf(p.low_driver.limiter.threshold_db);
    JsonObject lowDelay = low["delay"].to<JsonObject>();
    lowDelay["enabled"] = p.low_driver.delay.enabled;
    lowDelay["samples"] = p.low_driver.delay.samples;

    // High driver
    JsonObject high = doc["high_driver"].to<JsonObject>();
    JsonObject highComp = high["compressor"].to<JsonObject>();
    highComp["enabled"] = p.high_driver.compressor.enabled;
    highComp["threshold_db"] = roundf(p.high_driver.compressor.threshold_db);
    highComp["ratio"] = roundf(p.high_driver.compressor.ratio * 10.0f) / 10.0f;
    highComp["attack_ms"] = roundf(p.high_driver.compressor.attack_ms);
    highComp["release_ms"] = roundf(p.high_driver.compressor.release_ms);
    highComp["makeup_db"] = roundf(p.high_driver.compressor.makeup_db);
    JsonObject highLim = high["limiter"].to<JsonObject>();
    highLim["enabled"] = p.high_driver.limiter.enabled;
    highLim["threshold_db"] = roundf(p.high_driver.limiter.threshold_db);
    JsonObject highDelay = high["delay"].to<JsonObject>();
    highDelay["enabled"] = p.high_driver.delay.enabled;
    highDelay["samples"] = p.high_driver.delay.samples;

    String output;
    serializeJson(doc, output);
    return output;
}

static String build_json_presets()
{
    JsonDocument doc;
    JsonArray names = doc["presets"].to<JsonArray>();
    char nameBuf[12];
    for (int i = 0; i < NUM_PRESETS; i++) {
        if (preset_get_name(i, nameBuf)) {
            names.add(nameBuf);
        } else {
            char defaultName[12];
            snprintf(defaultName, sizeof(defaultName), "Preset %d", i + 1);
            names.add(defaultName);
        }
    }
    dsp_params_t p;
    if (xSemaphoreTake(dsp_params_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        p = current_params;
        xSemaphoreGive(dsp_params_mutex);
    }
    doc["active"] = p.active_preset;

    String output;
    serializeJson(doc, output);
    return output;
}

// ─────────────────────────────────────────────────────────────
// WiFi Setup
// ─────────────────────────────────────────────────────────────

static void setup_wifi()
{
    WiFi.mode(WIFI_AP_STA);

    // Always create AP for direct connection
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, false, WIFI_AP_MAX_CONN);
    web_server_ip = WiFi.softAPIP().toString();
    Serial.printf("[OK] WiFi AP: %s @ %s\n", WIFI_AP_SSID, web_server_ip.c_str());

    // Optionally connect to existing network
    if (strlen(WIFI_STA_SSID) > 0) {
        Serial.printf("[...] Connecting to WiFi: %s\n", WIFI_STA_SSID);
        WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);

        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_STA_TIMEOUT_MS) {
            delay(250);
        }

        if (WiFi.status() == WL_CONNECTED) {
            web_server_ip = WiFi.localIP().toString();
            Serial.printf("[OK] WiFi STA: %s\n", web_server_ip.c_str());
        } else {
            Serial.println("[WARN] WiFi STA connection failed — AP still available");
        }
    }

    wifi_connected = true;
}

// ─────────────────────────────────────────────────────────────
// HTTP Route Handlers
// ─────────────────────────────────────────────────────────────

static void on_get_root(AsyncWebServerRequest *request)
{
    // Try to serve from LittleFS first
    if (LittleFS.exists("/index.html")) {
        request->send(LittleFS, "/index.html", "text/html");
    } else {
        // Fallback: redirect to setup page
        request->send(200, "text/html",
            "<html><body><h1>ESP32-DSP</h1>"
            "<p>Web UI not found. Please upload index.html to LittleFS.</p>"
            "<p>Connect via Bluetooth for audio. Use the OLED + encoder for DSP control.</p>"
            "</body></html>");
    }
}

static void on_get_status(AsyncWebServerRequest *request)
{
    request->send(200, "application/json", build_json_status());
}

static void on_get_params(AsyncWebServerRequest *request)
{
    request->send(200, "application/json", build_json_params());
}

static void on_post_params(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    String body = (char *)data;
    body = body.substring(0, len);

    // Parse JSON and apply
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        request->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
        return;
    }

    const char *path = doc["path"];
    float value = doc["value"];

    if (!path) {
        request->send(400, "application/json", "{\"error\":\"missing path\"}");
        return;
    }

    // Apply the parameter under mutex
    if (xSemaphoreTake(dsp_params_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Navigate the path and set the value
        // Supported paths: master_volume, crossover_hz, low_gain, high_gain,
        //   mute, bypass, eq_bands.N.enabled, eq_bands.N.freq_hz, eq_bands.N.gain_db, eq_bands.N.q,
        //   low_driver.compressor.enabled, low_driver.compressor.threshold_db, etc.
        apply_param_path(current_params, path, value);
        xSemaphoreGive(dsp_params_mutex);
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(503, "application/json", "{\"error\":\"mutex timeout\"}");
    }
}

static void on_get_presets(AsyncWebServerRequest *request)
{
    request->send(200, "application/json", build_json_presets());
}

static void on_post_preset_save(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    String body = (char *)data;
    body = body.substring(0, len);

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        request->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
        return;
    }

    uint8_t idx = doc["index"] | 0;
    if (idx >= NUM_PRESETS) idx = 0;

    if (preset_save(idx)) {
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(500, "application/json", "{\"error\":\"save failed\"}");
    }
}

static void on_post_preset_load(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    String body = (char *)data;
    body = body.substring(0, len);

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        request->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
        return;
    }

    uint8_t idx = doc["index"] | 0;
    if (idx >= NUM_PRESETS) idx = 0;

    if (preset_load(idx)) {
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(500, "application/json", "{\"error\":\"load failed\"}");
    }
}

static void on_not_found(AsyncWebServerRequest *request)
{
    request->send(404, "text/plain", "Not Found");
}

// ─────────────────────────────────────────────────────────────
// Parameter path resolver
// ─────────────────────────────────────────────────────────────

static void apply_param_path(dsp_params_t &p, const char *path, float value)
{
    // Simple dot-separated path resolver
    // Examples: "master_volume", "eq_bands.0.freq_hz", "low_driver.compressor.threshold_db"

    if (strcmp(path, "master_volume") == 0) {
        p.master_volume = constrain(value, 0.0f, 1.0f);
    } else if (strcmp(path, "crossover_hz") == 0) {
        p.crossover_hz = constrain(value, CROSSOVER_MIN_HZ, CROSSOVER_MAX_HZ);
    } else if (strcmp(path, "low_gain") == 0) {
        p.low_gain = constrain(value, 0.0f, GAIN_MAX);
    } else if (strcmp(path, "high_gain") == 0) {
        p.high_gain = constrain(value, 0.0f, GAIN_MAX);
    } else if (strcmp(path, "mute") == 0) {
        p.mute = (value > 0.5f);
    } else if (strcmp(path, "bypass") == 0) {
        p.bypass = (value > 0.5f);
    } else if (strncmp(path, "eq_bands.", 9) == 0) {
        int band = path[9] - '0';
        if (band >= 0 && band < EQ_MAX_BANDS) {
            const char *field = path + 11; // skip "eq_bands.N."
            if (strcmp(field, "enabled") == 0) p.eq_bands[band].enabled = (value > 0.5f);
            else if (strcmp(field, "freq_hz") == 0) p.eq_bands[band].freq_hz = constrain(value, 20.0f, 20000.0f);
            else if (strcmp(field, "gain_db") == 0) p.eq_bands[band].gain_db = constrain(value, -12.0f, 12.0f);
            else if (strcmp(field, "q") == 0) p.eq_bands[band].q = constrain(value, 0.1f, 10.0f);
        }
    } else if (strncmp(path, "low_driver.", 11) == 0) {
        const char *sub = path + 11;
        if (strcmp(sub, "compressor.enabled") == 0) p.low_driver.compressor.enabled = (value > 0.5f);
        else if (strcmp(sub, "compressor.threshold_db") == 0) p.low_driver.compressor.threshold_db = constrain(value, -60.0f, 0.0f);
        else if (strcmp(sub, "compressor.ratio") == 0) p.low_driver.compressor.ratio = constrain(value, 1.0f, 20.0f);
        else if (strcmp(sub, "compressor.attack_ms") == 0) p.low_driver.compressor.attack_ms = constrain(value, 0.1f, 100.0f);
        else if (strcmp(sub, "compressor.release_ms") == 0) p.low_driver.compressor.release_ms = constrain(value, 10.0f, 1000.0f);
        else if (strcmp(sub, "compressor.makeup_db") == 0) p.low_driver.compressor.makeup_db = constrain(value, 0.0f, 24.0f);
        else if (strcmp(sub, "limiter.enabled") == 0) p.low_driver.limiter.enabled = (value > 0.5f);
        else if (strcmp(sub, "limiter.threshold_db") == 0) p.low_driver.limiter.threshold_db = constrain(value, -60.0f, 0.0f);
        else if (strcmp(sub, "delay.enabled") == 0) p.low_driver.delay.enabled = (value > 0.5f);
        else if (strcmp(sub, "delay.samples") == 0) p.low_driver.delay.samples = constrain((uint16_t)value, 0, DELAY_MAX_SAMPLES);
    } else if (strncmp(path, "high_driver.", 12) == 0) {
        const char *sub = path + 12;
        if (strcmp(sub, "compressor.enabled") == 0) p.high_driver.compressor.enabled = (value > 0.5f);
        else if (strcmp(sub, "compressor.threshold_db") == 0) p.high_driver.compressor.threshold_db = constrain(value, -60.0f, 0.0f);
        else if (strcmp(sub, "compressor.ratio") == 0) p.high_driver.compressor.ratio = constrain(value, 1.0f, 20.0f);
        else if (strcmp(sub, "compressor.attack_ms") == 0) p.high_driver.compressor.attack_ms = constrain(value, 0.1f, 100.0f);
        else if (strcmp(sub, "compressor.release_ms") == 0) p.high_driver.compressor.release_ms = constrain(value, 10.0f, 1000.0f);
        else if (strcmp(sub, "compressor.makeup_db") == 0) p.high_driver.compressor.makeup_db = constrain(value, 0.0f, 24.0f);
        else if (strcmp(sub, "limiter.enabled") == 0) p.high_driver.limiter.enabled = (value > 0.5f);
        else if (strcmp(sub, "limiter.threshold_db") == 0) p.high_driver.limiter.threshold_db = constrain(value, -60.0f, 0.0f);
        else if (strcmp(sub, "delay.enabled") == 0) p.high_driver.delay.enabled = (value > 0.5f);
        else if (strcmp(sub, "delay.samples") == 0) p.high_driver.delay.samples = constrain((uint16_t)value, 0, DELAY_MAX_SAMPLES);
    }
}

// ─────────────────────────────────────────────────────────────
// Route setup
// ─────────────────────────────────────────────────────────────

static void setup_routes()
{
    server.on("/", HTTP_GET, on_get_root);
    server.on("/api/status", HTTP_GET, on_get_status);
    server.on("/api/params", HTTP_GET, on_get_params);

    // POST handlers for AsyncWebServer with body
    // Note: Using on() with body handler directly — no separate handler needed
    server.on("/api/params", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            on_post_params(request, data, len, index, total);
        });

    server.on("/api/presets", HTTP_GET, on_get_presets);
    server.on("/api/presets/save", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            on_post_preset_save(request, data, len, index, total);
        });
    server.on("/api/presets/load", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            on_post_preset_load(request, data, len, index, total);
        });

    server.onNotFound(on_not_found);
}

// ─────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────

void web_init()
{
    // Initialize LittleFS for web files
    if (!LittleFS.begin(true)) {
        Serial.println("[WARN] LittleFS mount failed — web UI will not be available");
    } else {
        Serial.println("[OK] LittleFS mounted");
        // List files for debugging
        File root = LittleFS.open("/");
        if (root) {
            File file = root.openNextFile();
            while (file) {
                Serial.printf("  FS: %s (%d bytes)\n", file.name(), file.size());
                file = root.openNextFile();
            }
        }
    }

    setup_wifi();
    setup_routes();
    server.begin();
    Serial.printf("[OK] Web server started at http://%s\n", web_server_ip.c_str());
}

String web_get_url()
{
    return "http://" + web_server_ip;
}
