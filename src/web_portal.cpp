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
// Embedded web app fallback (served from LittleFS /data/index.html
// when available; this PROGMEM copy is the fallback).
// ─────────────────────────────────────────────────────────────
// NOTE: The full redesigned UI is in data/index.html (LittleFS).
// This PROGMEM fallback provides a minimal functional UI.
// To update: copy data/index.html content into this string.
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no"><meta name="theme-color" content="#0f0f1a"><title>ESP32-DSP</title>
<style>*{box-sizing:border-box;margin:0;padding:0}:root{--bg:#0f0f1a;--srf:#1a1a2e;--acc:#6c63ff;--tx:#e8e8f0;--ts:#9090b0;--dg:#ff6584;--ok:#34d399;--r:16px;--t:44px}body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:var(--bg);color:var(--tx);min-height:100dvh;padding-bottom:calc(68px + env(safe-area-inset-bottom,0px) + 16px)}
.hd{position:sticky;top:0;z-index:100;display:flex;align-items:center;gap:8px;padding:12px 16px;background:rgba(15,15,26,.9);backdrop-filter:blur(20px);border-bottom:1px solid rgba(255,255,255,.06)}.hd__t{font-size:1.1rem;font-weight:700;flex:1}.hd__b{display:flex;align-items:center;gap:4px;font-size:.75rem;color:var(--ts);background:var(--srf);border:1px solid rgba(255,255,255,.06);padding:4px 10px;border-radius:20px}.dot{display:inline-block;width:8px;height:8px;border-radius:50%}.dg{background:var(--ok);box-shadow:0 0 6px var(--ok)}.dr{background:var(--dg);box-shadow:0 0 6px var(--dg)}
.bn{position:fixed;bottom:0;left:0;right:0;height:68px;z-index:100;display:flex;background:rgba(15,15,26,.9);backdrop-filter:blur(20px);border-top:1px solid rgba(255,255,255,.06);padding-bottom:env(safe-area-inset-bottom,0px)}.bt{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:3px;color:#5a5a78;font-size:.65rem;font-weight:600;min-height:var(--t)}.bt.a{color:var(--acc)}.bt__i{font-size:1.25rem}
.p{display:none;padding:12px;max-width:960px;margin:0 auto}.p.a{display:block}
.c{background:var(--srf);border:1px solid rgba(255,255,255,.06);border-radius:var(--r);padding:16px;margin-bottom:12px}.ct{font-size:.78rem;font-weight:700;text-transform:uppercase;letter-spacing:1px;color:var(--ts);margin-bottom:12px}
.vu-bars{display:flex;gap:2px;height:28px;align-items:flex-end;margin:8px 0}.vu-bar{flex:1;border-radius:2px 2px 0 0;min-height:3px;background:var(--srf);transition:background .15s,height .15s}.vu-db{text-align:center;font-size:1.4rem;font-weight:700;font-family:monospace;margin-top:4px}
.sg{display:grid;grid-template-columns:1fr 1fr;gap:8px}.si{background:rgba(255,255,255,.04);border-radius:10px;padding:10px;text-align:center}.si .l{font-size:.65rem;color:#5a5a78;text-transform:uppercase;letter-spacing:.8px;font-weight:600}.si .v{font-size:1rem;font-weight:700;font-family:monospace;color:var(--tx)}
.sl{display:flex;align-items:center;gap:12px;margin-bottom:12px}.sl label{min-width:75px;font-size:.82rem;color:var(--ts);font-weight:500;flex-shrink:0}.sl input[type=range]{flex:1;-webkit-appearance:none;height:var(--t);background:transparent;cursor:pointer;touch-action:pan-y}.sl input[type=range]::-webkit-slider-runnable-track{height:6px;border-radius:3px;background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.06)}.sl input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;border-radius:50%;background:var(--acc);border:3px solid var(--bg);box-shadow:0 0 8px rgba(108,99,255,.4);margin-top:-9px}.sl .v{min-width:68px;text-align:right;font-size:.85rem;font-family:monospace;font-weight:600}
.tr{display:flex;align-items:center;justify-content:space-between;min-height:var(--t);margin-bottom:12px}.tr label{font-size:.88rem;font-weight:500}
.tg{position:relative;width:48px;height:26px;flex-shrink:0}.tg input{opacity:0;width:0;height:0;position:absolute}.tg__t{position:absolute;inset:0;border-radius:13px;background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.06);transition:all .25s;cursor:pointer}.tg__t::after{content:'';position:absolute;width:20px;height:20px;border-radius:50%;background:var(--ts);top:2px;left:2px;transition:all .25s}.tg input:checked+.tg__t{background:var(--acc);border-color:var(--acc)}.tg input:checked+.tg__t::after{left:24px;background:#fff}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;min-height:var(--t);padding:8px 16px;border-radius:10px;font-weight:700;font-size:.85rem;text-transform:uppercase;border:none;cursor:pointer}.btn-p{background:var(--dg);color:#fff}.btn-g{background:rgba(255,255,255,.08);color:var(--tx);border:1px solid rgba(255,255,255,.06)}.btn-s{background:var(--ok);color:#0f0f1a}.br{display:flex;gap:8px}.br .btn{flex:1}
.ec{width:100%;height:200px;background:#12121f;border-radius:10px;border:1px solid rgba(255,255,255,.06);margin-bottom:8px;display:block;cursor:crosshair}
.pg{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}.pc{background:rgba(255,255,255,.04);border:2px solid rgba(255,255,255,.06);border-radius:10px;padding:12px;text-align:center;cursor:pointer;min-height:var(--t);display:flex;flex-direction:column;align-items:center;justify-content:center}.pc.a{border-color:var(--acc);background:rgba(108,99,255,.12);box-shadow:0 0 20px rgba(108,99,255,.15)}.pc .n{font-weight:700;font-size:.85rem}.pc .d{font-size:.65rem;color:#5a5a78}
.toast{position:fixed;bottom:calc(68px + env(safe-area-inset-bottom,0px) + 12px);left:50%;transform:translateX(-50%) translateY(20px);background:rgba(42,42,74,.95);color:var(--tx);border:1px solid rgba(255,255,255,.1);border-radius:16px;padding:12px 20px;font-size:.85rem;font-weight:600;box-shadow:0 8px 24px rgba(0,0,0,.4);opacity:0;transition:all .25s;pointer-events:none;z-index:200;white-space:nowrap}.toast.show{opacity:1;transform:translateX(-50%) translateY(0)}
@media(min-width:768px){.p{padding:20px 24px}}
</style></head><body>
<div class="hd"><span style="font-size:1.35rem;filter:drop-shadow(0 0 8px rgba(108,99,255,.4))">🎧</span><span class="hd__t">ESP32-DSP</span><span class="hd__b"><span class="dot dr" id="cd"></span><span id="ct">Connecting…</span></span></div>
<div class="bn"><button class="bt a" onclick="showTab(0)" id="bn0"><span class="bt__i">📊</span>Status</button><button class="bt" onclick="showTab(1)" id="bn1"><span class="bt__i">🎚️</span>EQ</button><button class="bt" onclick="showTab(2)" id="bn2"><span class="bt__i">🔊</span>Dyn</button><button class="bt" onclick="showTab(3)" id="bn3"><span class="bt__i">🔀</span>Xover</button><button class="bt" onclick="showTab(4)" id="bn4"><span class="bt__i">💾</span>Preset</button></div>
<div class="p a" id="panel0"><div class="c"><div class="ct">📊 Output Level</div><div class="vu-bars" id="vuMeter"></div><div class="vu-db" id="vuDb">-- dB</div></div><div class="c"><div class="ct">🎛️ Quick Controls</div><div class="br" style="margin-bottom:12px"><button class="btn btn-p" id="muteBtn" onclick="toggleMute()">🔇 Mute</button><button class="btn btn-g" id="bypassBtn" onclick="toggleBypass()">🔀 Bypass</button></div><div class="sl"><label>Master</label><input type="range" id="mv" min="0" max="100" value="80" oninput="document.getElementById('mvV').textContent=this.value+'%'" onchange="sp('master_volume',this.value/100)"><span class="v" id="mvV">80%</span></div></div><div class="c"><div class="ct">📡 System Status</div><div class="sg"><div class="si"><div class="l">Bluetooth</div><div class="v" id="bt">--</div></div><div class="si"><div class="l">WiFi</div><div class="v" id="wr">-- dBm</div></div><div class="si"><div class="l">Preset</div><div class="v" id="pn">1/8</div></div><div class="si"><div class="l">CPU</div><div class="v" id="cpu">--%</div></div></div></div></div>
<div class="p" id="panel1"><div class="c"><canvas class="ec" id="eqC"></canvas><div style="display:flex;justify-content:space-between;font-size:.65rem;color:#5a5a78;font-family:monospace"><span>20Hz</span><span>100Hz</span><span>500Hz</span><span>1kHz</span><span>5kHz</span><span>10kHz</span><span>20kHz</span></div></div><div id="eqBands"></div></div>
<div class="p" id="panel2"><div class="c"><div style="font-size:1rem;font-weight:700;margin-bottom:12px">🔵 Woofer (Low)</div><div class="ct">📉 Compressor</div><div class="tr"><label>Enable</label><label class="tg"><input type="checkbox" id="lcEn" onchange="tp('low_driver.compressor.enabled',this.checked)"><div class="tg__t"></div></label></div><div class="sl"><label>Thresh</label><input type="range" id="lcTh" min="-60" max="0" value="-12" onchange="sp('low_driver.compressor.threshold_db',this.value)"><span class="v" id="lcThV">-12dB</span></div><div class="sl"><label>Ratio</label><input type="range" id="lcRa" min="1" max="20" value="2" step=".5" onchange="sp('low_driver.compressor.ratio',this.value)"><span class="v" id="lcRaV">2:1</span></div><div class="sl"><label>Attack</label><input type="range" id="lcAt" min="1" max="100" value="10" onchange="sp('low_driver.compressor.attack_ms',this.value)"><span class="v" id="lcAtV">10ms</span></div><div class="sl"><label>Release</label><input type="range" id="lcRe" min="10" max="1000" value="100" onchange="sp('low_driver.compressor.release_ms',this.value)"><span class="v" id="lcReV">100ms</span></div><div class="sl"><label>Makeup</label><input type="range" id="lcMk" min="0" max="24" value="0" step=".5" onchange="sp('low_driver.compressor.makeup_db',this.value)"><span class="v" id="lcMkV">0dB</span></div><hr style="border:none;border-top:1px solid rgba(255,255,255,.06);margin:12px 0"><div class="ct">🛡️ Limiter</div><div class="tr"><label>Enable</label><label class="tg"><input type="checkbox" id="llEn" checked onchange="tp('low_driver.limiter.enabled',this.checked)"><div class="tg__t"></div></label></div><div class="sl"><label>Ceiling</label><input type="range" id="llTh" min="-20" max="0" value="-3" onchange="sp('low_driver.limiter.threshold_db',this.value)"><span class="v" id="llThV">-3dB</span></div></div><div class="c"><div style="font-size:1rem;font-weight:700;margin-bottom:12px">🔴 Tweeter (High)</div><div class="ct">📉 Compressor</div><div class="tr"><label>Enable</label><label class="tg"><input type="checkbox" id="hcEn" onchange="tp('high_driver.compressor.enabled',this.checked)"><div class="tg__t"></div></label></div><div class="sl"><label>Thresh</label><input type="range" id="hcTh" min="-60" max="0" value="-12" onchange="sp('high_driver.compressor.threshold_db',this.value)"><span class="v" id="hcThV">-12dB</span></div><div class="sl"><label>Ratio</label><input type="range" id="hcRa" min="1" max="20" value="2" step=".5" onchange="sp('high_driver.compressor.ratio',this.value)"><span class="v" id="hcRaV">2:1</span></div><div class="sl"><label>Attack</label><input type="range" id="hcAt" min="1" max="100" value="10" onchange="sp('high_driver.compressor.attack_ms',this.value)"><span class="v" id="hcAtV">10ms</span></div><div class="sl"><label>Release</label><input type="range" id="hcRe" min="10" max="1000" value="100" onchange="sp('high_driver.compressor.release_ms',this.value)"><span class="v" id="hcReV">100ms</span></div><div class="sl"><label>Makeup</label><input type="range" id="hcMk" min="0" max="24" value="0" step=".5" onchange="sp('high_driver.compressor.makeup_db',this.value)"><span class="v" id="hcMkV">0dB</span></div><hr style="border:none;border-top:1px solid rgba(255,255,255,.06);margin:12px 0"><div class="ct">🛡️ Limiter</div><div class="tr"><label>Enable</label><label class="tg"><input type="checkbox" id="hlEn" checked onchange="tp('high_driver.limiter.enabled',this.checked)"><div class="tg__t"></div></label></div><div class="sl"><label>Ceiling</label><input type="range" id="hlTh" min="-20" max="0" value="-3" onchange="sp('high_driver.limiter.threshold_db',this.value)"><span class="v" id="hlThV">-3dB</span></div></div></div>
<div class="p" id="panel3"><div class="c"><div class="ct">🔀 Crossover</div><div class="sl"><label>Frequency</label><input type="range" id="xf" min="200" max="4000" value="2000" step="50" onchange="sp('crossover_hz',this.value)"><span class="v" id="xfV">2000 Hz</span></div><div class="sl"><label>Low Gain</label><input type="range" id="lg" min="0" max="200" value="100" onchange="sp('low_gain',this.value/100)"><span class="v" id="lgV">1.00x</span></div><div class="sl"><label>High Gain</label><input type="range" id="hg" min="0" max="200" value="100" onchange="sp('high_gain',this.value/100)"><span class="v" id="hgV">1.00x</span></div></div><div class="c"><div class="ct">⏱️ Time Alignment</div><div class="tr"><label>Low Delay On</label><label class="tg"><input type="checkbox" id="ldEn" onchange="tp('low_driver.delay.enabled',this.checked)"><div class="tg__t"></div></label></div><div class="sl"><label>Low Delay</label><input type="range" id="ld" min="0" max="200" value="0" onchange="sp('low_driver.delay.samples',this.value*4410)"><span class="v" id="ldV">0.0 ms</span></div><hr style="border:none;border-top:1px solid rgba(255,255,255,.06);margin:12px 0"><div class="tr"><label>High Delay On</label><label class="tg"><input type="checkbox" id="hdEn" onchange="tp('high_driver.delay.enabled',this.checked)"><div class="tg__t"></div></label></div><div class="sl"><label>High Delay</label><input type="range" id="hdd" min="0" max="200" value="0" onchange="sp('high_driver.delay.samples',this.value*4410)"><span class="v" id="hddV">0.0 ms</span></div></div></div>
<div class="p" id="panel4"><div class="c"><div class="ct">💾 Presets</div><div class="pg" id="pg"></div></div><div class="c"><button class="btn btn-s" style="width:100%" onclick="savePreset()">💾 Save to Active Slot</button></div></div>
<div class="toast" id="toast"></div>
<script>let params={},pollTimer=null,vuPeak=-60,vuPkT=0;const N=20;
window.addEventListener('load',()=>{initVU();Promise.all([fetchStatus(),fetchParams(),fetchPresets()]).then(()=>{pollTimer=setInterval(fetchStatus,150)});window.addEventListener('resize',()=>{clearTimeout(wt);wt=setTimeout(drawEQ,150)})});
let wt;
function showTab(n){document.querySelectorAll('.p').forEach((p,i)=>p.classList.toggle('a',i===n));document.querySelectorAll('.bt').forEach((t,i)=>t.classList.toggle('a',i===n));if(n===1)setTimeout(drawEQ,60)}
async function fetchStatus(){try{const r=await fetch('/api/status');const d=await r.json();updSt(d);document.getElementById('cd').className='dot dg';document.getElementById('ct').textContent='Connected'}catch(e){document.getElementById('cd').className='dot dr';document.getElementById('ct').textContent='Disconnected'}}
function updSt(d){const db=d.rms_level>0?20*Math.log10(d.rms_level/32768):-60;updVU(db);document.getElementById('vuDb').textContent=db.toFixed(1)+' dB';const bt=['Disconnected','Connected','Playing'];document.getElementById('bt').textContent=bt[d.bt_state]||'--';document.getElementById('wr').textContent=(d.wifi_rssi||0)+' dBm';document.getElementById('pn').textContent=(d.active_preset+1)+'/8';const mb=document.getElementById('muteBtn');if(d.mute){mb.classList.add('btn-p');mb.classList.remove('btn-g');mb.innerHTML='🔇 MUTED'}else{mb.classList.remove('btn-p');mb.classList.add('btn-g');mb.innerHTML='🔇 Mute'}const bb=document.getElementById('bypassBtn');if(d.bypass){bb.style.background='#fbbf24';bb.innerHTML='🔀 BYPASSED'}else{bb.style.background='';bb.innerHTML='🔀 Bypass'}}
function initVU(){const v=document.getElementById('vuMeter');let h='<div class="vu-bars">';for(let i=0;i<N;i++)h+=`<div class="vu-bar" id="vu${i}" style="height:3px;background:rgba(255,255,255,.04)"></div>`;h+='</div>';h+='<div style="display:flex;justify-content:space-between;font-size:.65rem;color:#5a5a78;font-family:monospace;margin-top:4px"><span>-60</span><span>-40</span><span>-20</span><span>-10</span><span>0</span></div>';v.innerHTML=h}
function updVU(db){const pct=Math.max(0,Math.min(1,(db+60)/60));const n=Math.round(pct*N);if(db>vuPeak){vuPeak=db;vuPkT=Date.now()}else if(Date.now()-vuPkT>800)vuPeak=Math.max(-60,vuPeak-1);const pk=Math.round(((vuPeak+60)/60)*N);for(let i=0;i<N;i++){const b=document.getElementById('vu'+i);if(!b)continue;const pct=i/N;let h=3;if(i<n){h=3+(i/N)*25;b.style.background=pct<.6?'#34d399':pct<.8?'#fbbf24':'#ff6584'}else if(i===pk){h=3+(i/N)*25;b.style.background='#8b83ff'}else{b.style.background='rgba(255,255,255,.04)'}b.style.height=h+'px'}}
async function fetchParams(){try{const r=await fetch('/api/params');params=await r.json();popCtrls();drawEQ()}catch(e){console.warn(e)}}
function popCtrels(){if(!params.master_volume)return;document.getElementById('mv').value=Math.round(params.master_volume*100);document.getElementById('mvV').textContent=Math.round(params.master_volume*100)+'%';document.getElementById('xf').value=params.crossover_hz;document.getElementById('xfV').textContent=Math.round(params.crossover_hz)+' Hz';document.getElementById('lg').value=Math.round(params.low_gain*100);document.getElementById('lgV').textContent=params.low_gain.toFixed(2)+'x';document.getElementById('hg').value=Math.round(params.high_gain*100);document.getElementById('hgV').textContent=params.high_gain.toFixed(2)+'x';const eq=document.getElementById('eqBands');eq.innerHTML='';for(let i=0;i<4;i++){const b=params.eq_bands[i]||{};eq.innerHTML+=`<div class="c"><div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:12px"><div><div style="font-weight:700;font-size:.9rem">Band ${i+1}</div><div style="font-size:.65rem;color:#5a5a78">${b.freq_hz<200?'Low Shelf':b.freq_hz>4000?'High Shelf':'Parametric'}</div></div><label class="tg"><input type="checkbox" ${b.checked?'checked':''} onchange="tp('eq_bands.${i}.enabled',this.checked)"><div class="tg__t"></div></label></div><div class="sl"><label>Freq</label><input type="range" min="20" max="20000" value="${b.freq_hz||1000}" onchange="sp('eq_bands.${i}.freq_hz',this.value)"><span class="v">${b.freq_hz>=1000?(b.freq_hz/1000).toFixed(1)+'kHz':b.freq_hz+'Hz'}</span></div><div class="sl"><label>Gain</label><input type="range" min="-12" max="12" value="${b.gain_db||0}" step=".5" onchange="sp('eq_bands.${i}.gain_db',this.value)"><span class="v">${b.gain_db||0}dB</span></div><div class="sl"><label>Q</label><input type="range" min="1" max="100" value="${((b.q||1)*10)}" onchange="sp('eq_bands.${i}.q',this.value/10)"><span class="v">${(b.q||1).toFixed(1)}</span></div></div>`}const lc=params.low_driver.compressor;document.getElementById('lcEn').checked=lc.enabled;document.getElementById('lcTh').value=lc.threshold_db;document.getElementById('lcThV').textContent=lc.threshold_db+'dB';document.getElementById('lcRa').value=lc.ratio;document.getElementById('lcRaV').textContent=lc.ratio+':1';document.getElementById('lcAt').value=lc.attack_ms;document.getElementById('lcAtV').textContent=lc.attack_ms+'ms';document.getElementById('lcRe').value=lc.release_ms;document.getElementById('lcReV').textContent=lc.release_ms+'ms';document.getElementById('lcMk').value=lc.makeup_db;document.getElementById('lcMkV').textContent=lc.makeup_db+'dB';const ll=params.low_driver.limiter;document.getElementById('llEn').checked=ll.enabled;document.getElementById('llTh').value=ll.threshold_db;document.getElementById('llThV').textContent=ll.threshold_db+'dB';const hc=params.high_driver.compressor;document.getElementById('hcEn').checked=hc.enabled;document.getElementById('hcTh').value=hc.threshold_db;document.getElementById('hcThV').textContent=hc.threshold_db+'dB';document.getElementById('hcRa').value=hc.ratio;document.getElementById('hcRaV').textContent=hc.ratio+':1';document.getElementById('hcAt').value=hc.attack_ms;document.getElementById('hcAtV').textContent=hc.attack_ms+'ms';document.getElementById('hcRe').value=hc.release_ms;document.getElementById('hcReV').textContent=hc.release_ms+'ms';document.getElementById('hcMk').value=hc.makeup_db;document.getElementById('hcMkV').textContent=hc.makeup_db+'dB';const hl=params.high_driver.limiter;document.getElementById('hlEn').checked=hl.enabled;document.getElementById('hlTh').value=hl.threshold_db;document.getElementById('hlThV').textContent=hl.threshold_db+'dB';const ld=params.low_driver.delay;document.getElementById('ldEn').checked=ld.enabled;document.getElementById('ld').value=ld.samples/4410;document.getElementById('ldV').textContent=(ld.samples*1000/44100).toFixed(1)+' ms';const hd=params.high_driver.delay;document.getElementById('hdEn').checked=hd.enabled;document.getElementById('hdd').value=hd.samples/4410;document.getElementById('hddV').textContent=(hd.samples*1000/44100).toFixed(1)+' ms'}
function popCtrls(){popCtrels()}
function drawEQ(){const c=document.getElementById('eqC');if(!c||!params.eq_bands)return;const ctx=c.getContext('2d');const rect=c.getBoundingClientRect();const dpr=window.devicePixelRatio||1;c.width=rect.width*dpr;c.height=rect.height*dpr;ctx.scale(dpr,dpr);const w=rect.width,h=rect.height;ctx.fillStyle='#12121f';ctx.fillRect(0,0,w,h);[20,50,100,200,500,1000,2000,5000,10000,20000].forEach(f=>{const x=Math.log10(f/20)/Math.log10(1000)*w;ctx.strokeStyle='rgba(255,255,255,.04)';ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,h);ctx.stroke()});for(let db=-12;db<=12;db+=6){const y=h/2-(db/24)*h/2;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(w,y);ctx.stroke()}ctx.strokeStyle='rgba(255,255,255,.12)';ctx.setLineDash([6,4]);ctx.beginPath();ctx.moveTo(0,h/2);ctx.lineTo(w,h/2);ctx.stroke();ctx.setLineDash([]);ctx.strokeStyle='#6c63ff';ctx.lineWidth=2.5;ctx.lineJoin='round';ctx.beginPath();for(let px=0;px<w;px++){const freq=20*Math.pow(1000,px/w);let total=0;for(let i=0;i<4;i++){const b=params.eq_bands[i];if(!b||!b.enabled)continue;total+=biqResp(freq,b)}const py=h/2-(total/24)*h/2;if(px===0)ctx.moveTo(px,py);else ctx.lineTo(px,py)}ctx.stroke()}
function biqResp(freq,band){const f=Math.max(20,Math.min(20000,freq));const A=Math.pow(10,band.gain_db/40);const w0=2*Math.PI*f/44100;const s=Math.sin(w0),c=Math.cos(w0);const a=s/(2*band.q);let b0,b1,b2,a0,a1,a2;if(f<200){const q=Math.sqrt(A);b0=A*((A+1)-(A-1)*c+2*q*a);b1=2*A*((A-1)-(A+1)*c);b2=A*((A+1)-(A-1)*c-2*q*a);a0=(A+1)+(A-1)*c+2*q*a;a1=-2*((A-1)+(A+1)*c);a2=(A+1)+(A-1)*c-2*q*a}else if(f>4000){const q=Math.sqrt(A);b0=A*((A+1)+(A-1)*c+2*q*a);b1=-2*A*((A-1)+(A+1)*c);b2=A*((A+1)+(A-1)*c-2*q*a);a0=(A+1)-(A-1)*c+2*q*a;a1=2*((A-1)-(A+1)*c);a2=(A+1)-(A-1)*c-2*q*a}else{b0=1+a*A;b1=-2*c;b2=1-a*A;a0=1+a/A;a1=-2*c;b2=1-a/A}const nR=b0+b1*c+b2*Math.cos(2*w0),nI=-(b1*s+b2*Math.sin(2*w0));const dR=a0+a1*c+a2*Math.cos(2*w0),dI=-(a1*s+a2*Math.sin(2*w0));return=10*Math.log10((nR*nR+nI*nI)/(dR*dR+dI*dI))}
async function sp(p,v){try{await fetch('/api/params',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path:p,value:parseFloat(v)})});showToast('✓ Updated')}catch(e){showToast('✗ Error')}}
async function tp(p,c){await sp(p,c)}
async function fetchPresets(){try{const r=await fetch('/api/presets');const d=await r.json();const g=document.getElementById('pg');g.innerHTML='';for(let i=0;i<8;i++){const n=(d.presets&&d.presets[i])?d.presets[i]:'Preset '+(i+1);const a=i===d.active?' a':'';g.innerHTML+=`<div class="pc${a}" onclick="loadPreset(${i})"><span class="n">${n}</span><span class="d">Slot ${i+1}</span></div>`}}catch(e){console.warn(e)}}
async function loadPreset(i){await fetch('/api/presets/load',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({index:i})});showToast('✓ Loaded');await fetchParams();await fetchPresets()}
async function savePreset(){const i=params.active_preset||0;await fetch('/api/presets/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({index:i})});showToast('✓ Saved');await fetchPresets()}
function showToast(m){const t=document.getElementById('toast');t.textContent=m;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),1200)}
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
