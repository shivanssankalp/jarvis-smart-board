/*
  =====================================================================
  JARVIS SMART BOARD  —  ESP32 6-Channel Relay Control System
  =====================================================================
  Standalone WiFi Access Point + local web dashboard. No internet,
  no cloud, no third-party services. Everything (HTML/CSS/JS) is
  served directly from ESP32 flash memory (PROGMEM).

  HARDWARE
  ---------------------------------------------------------------------
  Board          : ESP32 DevKit V1 (or any ESP32 dev board)
  Relay Module   : 6-Channel, ACTIVE LOW
  Relay 1 (Room Light)   -> GPIO 23
  Relay 2 (Ceiling Fan)  -> GPIO 22
  Relay 3 (Television)   -> GPIO 21
  Relay 4 (Power Socket) -> GPIO 19
  Relay 5 (Device 5)     -> GPIO 18
  Relay 6 (Device 6)     -> GPIO 5

  NETWORK
  ---------------------------------------------------------------------
  Mode : WiFi SoftAP (ESP32 creates its own network — no router needed)
  SSID : JARVIS_BOARD          (change below or via Settings page)
  Pass : IronMan@2026
  URL  : http://192.168.4.1    (default ESP32 SoftAP gateway address)

  SECURITY NOTE (read this before deploying)
  ---------------------------------------------------------------------
  The login screen (admin / jarvis123) is BASIC access gating suitable
  for a private home network. It is NOT strong security:
    - Credentials are compiled into the firmware in plain text.
    - Traffic is plain HTTP (no TLS).
    - Only one active session is supported at a time.

  A NOTE ON HOW THE DASHBOARD IS STORED IN THIS FILE
  ---------------------------------------------------------------------
  The HTML/CSS/JS below is written as a sequence of ordinary quoted
  C++ strings (one per line, automatically joined by the compiler)
  rather than a single C++11 "raw string literal". This is
  deliberate: Arduino IDE's automatic prototype scanner does not
  understand raw string syntax, and can misinterpret JavaScript
  function(){...} patterns inside one as real C++ code — corrupting
  the compiled output. Ordinary quoted strings avoid that entirely.

  LIBRARIES (all bundled with the "esp32 by Espressif Systems"
  Arduino core — nothing extra to install)
  ---------------------------------------------------------------------
  WiFi.h        - SoftAP networking
  WebServer.h   - local HTTP server
  Preferences.h - flash (NVS) storage for relay states, names, WiFi
  =====================================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// ---- Explicit forward declarations (belt-and-suspenders safety) ----
void loadPreferences();
void setupRelayPins();
void setRelay(uint8_t ch, bool on);
String applyScene(const String& scene);
void scheduleNextVacationToggle();
void runVacationScheduler();
void setupAP();
String generateSessionToken();
String getCookieValue(const String& cookieHeader, const String& key);
bool isAuthenticated();
void rejectUnauthorized();
String jsonEscape(const String& input);
void setupServer();
void handleRoot();
void handleLogin();
void handleLogout();
void handleStatus();
void handleRelay();
void handleScene();
void handleRename();
void handleWifiChange();
void handleRestart();
void handleFactoryReset();
void handleNotFound();

// ---- Dashboard HTML/CSS/JS (see note above on why this format is used) ----
const char INDEX_HTML[] PROGMEM =
"\n"
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"<meta charset=\"UTF-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no\">\n"
"<meta name=\"color-scheme\" content=\"dark\">\n"
"<title>JARVIS Smart Board</title>\n"
"<style>\n"
":root{\n"
"  --bg-0:#050810; --bg-1:#0a1330; --bg-2:#0d1b3a;\n"
"  --glass-fill:rgba(10,25,50,0.50); --glass-fill-strong:rgba(9,20,42,0.72);\n"
"  --glass-border:rgba(0,217,255,0.28); --glass-border-soft:rgba(0,217,255,0.14);\n"
"  --cyan:#00d9ff; --cyan-dim:#0a90ad; --indigo:#6a5cff;\n"
"  --teal:#00ffc8; --amber:#ffb545; --danger:#ff3b5c;\n"
"  --text-0:#eaf6ff; --text-1:#9fb7d6; --text-2:#5c7796;\n"
"  --font-hud:system-ui,-apple-system,\"Segoe UI\",Roboto,Helvetica,Arial,sans-serif;\n"
"  --font-mono:ui-monospace,\"SF Mono\",\"Cascadia Code\",\"Consolas\",\"Roboto Mono\",monospace;\n"
"  --r-lg:22px; --r-md:16px; --r-sm:10px;\n"
"}\n"
"*{box-sizing:border-box; margin:0; padding:0;}\n"
"html,body{height:100%;}\n"
"body{\n"
"  font-family:var(--font-hud); color:var(--text-0);\n"
"  background:\n"
"    radial-gradient(1200px 700px at 15% -10%, rgba(0,217,255,0.10), transparent 60%),\n"
"    radial-gradient(1000px 600px at 110% 10%, rgba(106,92,255,0.10), transparent 55%),\n"
"    linear-gradient(180deg, var(--bg-0), var(--bg-1) 45%, var(--bg-2));\n"
"  min-height:100%; overflow-x:hidden;\n"
"  -webkit-tap-highlight-color:transparent;\n"
"}\n"
"canvas#particles{position:fixed; inset:0; z-index:0; pointer-events:none; opacity:0.55;}\n"
"button{font-family:inherit; cursor:pointer;}\n"
"input{font-family:inherit;}\n"
"::selection{background:rgba(0,217,255,0.3);}\n"
"\n"
"a,button,input,[tabindex]{outline:none;}\n"
"a:focus-visible,button:focus-visible,input:focus-visible,[tabindex]:focus-visible{\n"
"  outline:2px solid var(--cyan); outline-offset:2px; border-radius:6px;\n"
"}\n"
"\n"
".glass{\n"
"  background:var(--glass-fill);\n"
"  backdrop-filter:blur(18px) saturate(140%);\n"
"  -webkit-backdrop-filter:blur(18px) saturate(140%);\n"
"  border:1px solid var(--glass-border);\n"
"  box-shadow:0 8px 32px rgba(0,0,0,0.45), inset 0 0 40px rgba(0,217,255,0.05);\n"
"}\n"
"\n"
".screen{\n"
"  position:fixed; inset:0; z-index:1; overflow-y:auto;\n"
"  opacity:0; visibility:hidden; transition:opacity .5s ease;\n"
"}\n"
".screen.active{opacity:1; visibility:visible; z-index:2;}\n"
".screen-center{display:flex; align-items:center; justify-content:center; flex-direction:column; padding:2rem;}\n"
"\n"
"/* ---------------- BOOT SCREEN ---------------- */\n"
".boot-logo{\n"
"  font-size:clamp(2rem,7vw,3.2rem); font-weight:800; letter-spacing:.6rem;\n"
"  background:linear-gradient(120deg,var(--cyan),var(--indigo));\n"
"  -webkit-background-clip:text; background-clip:text; color:transparent;\n"
"  animation:bootPulse 2.2s ease-in-out infinite;\n"
"}\n"
"@keyframes bootPulse{0%,100%{filter:drop-shadow(0 0 8px rgba(0,217,255,.35))} 50%{filter:drop-shadow(0 0 22px rgba(106,92,255,.55))}}\n"
".boot-lines{\n"
"  margin-top:2rem; font-family:var(--font-mono); font-size:.85rem; color:var(--cyan);\n"
"  min-height:7.5em; text-align:left; width:min(90vw,360px);\n"
"}\n"
".boot-line{opacity:0; transform:translateX(-6px); animation:bootLineIn .4s ease forwards; margin-bottom:.45em;}\n"
".boot-line::before{content:\"> \"; color:var(--text-2);}\n"
"@keyframes bootLineIn{to{opacity:1; transform:translateX(0);}}\n"
".boot-bar{width:min(90vw,360px); height:3px; background:rgba(255,255,255,.08); border-radius:4px; margin-top:1.4rem; overflow:hidden;}\n"
".boot-bar-fill{height:100%; width:0%; background:linear-gradient(90deg,var(--cyan),var(--indigo)); box-shadow:0 0 12px var(--cyan); transition:width .3s ease;}\n"
"\n"
"/* ---------------- LOGIN SCREEN ---------------- */\n"
".login-panel{\n"
"  width:min(90vw,380px); padding:2.4rem 2rem 2rem; border-radius:var(--r-lg);\n"
"  text-align:center; position:relative;\n"
"}\n"
".login-ring{\n"
"  width:56px; height:56px; margin:0 auto 1rem; border-radius:50%;\n"
"  border:2px dashed var(--glass-border); position:relative;\n"
"  animation:spin 6s linear infinite;\n"
"}\n"
".login-ring::after{\n"
"  content:\"\"; position:absolute; inset:10px; border-radius:50%;\n"
"  border:1px solid var(--cyan); box-shadow:0 0 14px var(--cyan);\n"
"}\n"
"@keyframes spin{to{transform:rotate(360deg);}}\n"
".hud-title{font-size:1.15rem; font-weight:700; letter-spacing:.18rem;}\n"
".hud-title.small{font-size:.95rem;}\n"
".hud-sub{color:var(--text-1); font-size:.78rem; letter-spacing:.1rem; margin-top:.35rem; text-transform:uppercase;}\n"
".hud-label{\n"
"  display:block; text-align:left; font-size:.7rem; letter-spacing:.12rem; color:var(--text-1);\n"
"  text-transform:uppercase; margin:1.1rem 0 .35rem;\n"
"}\n"
"#login-form input{\n"
"  width:100%; padding:.7rem .8rem; background:rgba(255,255,255,.04);\n"
"  border:1px solid var(--glass-border-soft); border-bottom:2px solid var(--cyan-dim);\n"
"  color:var(--text-0); border-radius:8px; font-size:.95rem;\n"
"  transition:border-color .2s, box-shadow .2s;\n"
"}\n"
"#login-form input:focus{border-bottom-color:var(--cyan); box-shadow:0 4px 14px -6px var(--cyan);}\n"
".login-error{\n"
"  min-height:1.4em; color:var(--danger); font-size:.78rem; margin-top:.9rem;\n"
"  letter-spacing:.03rem;\n"
"}\n"
".login-error.shake{animation:shake .4s ease;}\n"
"@keyframes shake{20%{transform:translateX(-6px)} 40%{transform:translateX(6px)} 60%{transform:translateX(-4px)} 80%{transform:translateX(4px)} 100%{transform:translateX(0)}}\n"
"\n"
".btn-primary,.btn-secondary,.btn-outline,.btn-danger{\n"
"  border-radius:10px; padding:.75rem 1.1rem; font-size:.8rem; font-weight:700;\n"
"  letter-spacing:.1rem; border:1px solid transparent; transition:transform .15s, box-shadow .2s, opacity .2s;\n"
"  width:100%; margin-top:1.3rem;\n"
"}\n"
".btn-primary{\n"
"  background:linear-gradient(120deg,var(--cyan),var(--indigo)); color:#03131c;\n"
"  box-shadow:0 6px 20px -8px rgba(0,217,255,.6);\n"
"}\n"
".btn-primary:hover{transform:translateY(-1px); box-shadow:0 10px 26px -8px rgba(0,217,255,.75);}\n"
".btn-primary:disabled{opacity:.6; transform:none;}\n"
".btn-secondary{background:transparent; border-color:var(--cyan); color:var(--cyan);}\n"
".btn-secondary:hover{background:rgba(0,217,255,.08);}\n"
".btn-outline{background:transparent; border-color:var(--glass-border); color:var(--text-0);}\n"
".btn-outline:hover{border-color:var(--cyan); color:var(--cyan);}\n"
".btn-danger{background:rgba(255,59,92,.12); border-color:var(--danger); color:#ffb6c3;}\n"
".btn-danger:hover{background:rgba(255,59,92,.22);}\n"
"\n"
".spinner{\n"
"  width:16px; height:16px; border-radius:50%; border:2px solid rgba(255,255,255,.25);\n"
"  border-top-color:currentColor; display:inline-block; animation:spin .7s linear infinite;\n"
"  vertical-align:middle; margin-right:.5rem;\n"
"}\n"
"\n"
"/* ---------------- DASHBOARD ---------------- */\n"
".dashboard-main{max-width:1180px; margin:0 auto; padding:0 1rem 3rem; position:relative; z-index:1;}\n"
"\n"
".topbar{\n"
"  display:flex; align-items:center; justify-content:space-between; gap:1rem;\n"
"  margin:1rem; padding:.8rem 1.2rem; border-radius:var(--r-md);\n"
"  position:sticky; top:.75rem; z-index:5; flex-wrap:wrap;\n"
"}\n"
".brand{display:flex; align-items:center; gap:.7rem;}\n"
".brand-dot{width:9px; height:9px; border-radius:50%; background:var(--teal); box-shadow:0 0 10px var(--teal); animation:breathe 2.4s ease-in-out infinite;}\n"
".brand-dot.warn{background:var(--amber); box-shadow:0 0 10px var(--amber); animation-duration:.9s;}\n"
"@keyframes breathe{0%,100%{opacity:.55} 50%{opacity:1}}\n"
".brand-title{font-size:.85rem; font-weight:800; letter-spacing:.14rem;}\n"
".brand-credit{font-size:.6rem; font-weight:500; letter-spacing:.03rem; color:var(--text-1); text-transform:none; opacity:.75; margin-left:.3rem;}\n"
".brand-sub{font-size:.62rem; letter-spacing:.1rem; color:var(--text-1); text-transform:uppercase; margin-top:2px;}\n"
"\n"
".topbar-mid{display:flex; align-items:center; gap:.7rem;}\n"
".radar{width:38px; height:38px; border-radius:50%; position:relative; border:1px solid var(--glass-border); overflow:hidden; background:radial-gradient(circle, rgba(0,217,255,.06), transparent 70%);}\n"
".radar-rings{position:absolute; inset:6px; border:1px solid var(--glass-border-soft); border-radius:50%;}\n"
".radar-sweep{\n"
"  position:absolute; inset:0; border-radius:50%;\n"
"  background:conic-gradient(from 0deg, rgba(0,217,255,.55), transparent 30%);\n"
"  animation:radarSpin 3.2s linear infinite;\n"
"}\n"
"@keyframes radarSpin{to{transform:rotate(360deg);}}\n"
".radar-blip{position:absolute; width:4px; height:4px; border-radius:50%; background:var(--teal); box-shadow:0 0 6px var(--teal);}\n"
".clients{font-family:var(--font-mono); font-size:.7rem; letter-spacing:.05rem; color:var(--text-1);}\n"
".clients span{color:var(--cyan); font-weight:700;}\n"
"\n"
".topbar-right{display:flex; align-items:center; gap:.8rem;}\n"
".clock{font-family:var(--font-mono); font-size:.95rem; letter-spacing:.05rem; color:var(--cyan); text-shadow:0 0 10px rgba(0,217,255,.4);}\n"
".icon-btn{\n"
"  width:36px; height:36px; border-radius:50%; background:rgba(255,255,255,.04);\n"
"  border:1px solid var(--glass-border-soft); color:var(--text-0); font-size:1rem;\n"
"  display:flex; align-items:center; justify-content:center; transition:.2s;\n"
"}\n"
".icon-btn:hover{border-color:var(--cyan); color:var(--cyan); box-shadow:0 0 12px -2px var(--cyan);}\n"
"\n"
".power-cluster{\n"
"  display:flex; align-items:center; justify-content:center; gap:clamp(1rem,4vw,3rem);\n"
"  flex-wrap:wrap; margin:2rem 0 2.5rem;\n"
"}\n"
".arc-reactor{\n"
"  position:relative; width:clamp(170px,32vw,220px); height:clamp(170px,32vw,220px);\n"
"  background:none; border:none; padding:0;\n"
"}\n"
".arc-svg{width:100%; height:100%; overflow:visible;}\n"
".arc-ring-outer{fill:none; stroke:var(--glass-border); stroke-width:1.5;}\n"
".arc-ring-dash{\n"
"  fill:none; stroke:var(--cyan-dim); stroke-width:3; stroke-linecap:round;\n"
"  stroke-dasharray:6 11; transform-origin:100px 100px; animation:spin 18s linear infinite;\n"
"  transition:stroke .3s;\n"
"}\n"
".arc-ring-core{fill:rgba(255,255,255,.03); stroke:var(--text-2); stroke-width:1.5; transition:stroke .3s, filter .3s;}\n"
".arc-reactor.on .arc-ring-dash{stroke:var(--cyan); animation-duration:6s; filter:drop-shadow(0 0 6px var(--cyan));}\n"
".arc-reactor.on .arc-ring-core{stroke:var(--teal); filter:drop-shadow(0 0 16px rgba(0,255,200,.65));}\n"
".arc-reactor.on .arc-power-text{color:var(--teal);}\n"
".arc-label{position:absolute; inset:0; display:flex; flex-direction:column; align-items:center; justify-content:center; gap:.3rem;}\n"
".arc-power-icon{font-size:1.6rem; color:var(--text-1); transition:.3s;}\n"
".arc-reactor.on .arc-power-icon{color:var(--teal); text-shadow:0 0 14px rgba(0,255,200,.7);}\n"
".arc-power-text{font-size:.7rem; letter-spacing:.18rem; color:var(--text-1); font-weight:700; transition:.3s;}\n"
"\n"
".gauges{display:flex; gap:1.1rem; flex-wrap:wrap; justify-content:center;}\n"
".gauge{position:relative; width:92px; height:92px;}\n"
".gauge svg{width:100%; height:100%; transform:rotate(-90deg);}\n"
".gauge-bg{fill:none; stroke:rgba(255,255,255,.07); stroke-width:8;}\n"
".gauge-fg{\n"
"  fill:none; stroke:var(--cyan); stroke-width:8; stroke-linecap:round;\n"
"  stroke-dasharray:264; stroke-dashoffset:264; transition:stroke-dashoffset .6s ease;\n"
"  filter:drop-shadow(0 0 6px rgba(0,217,255,.5));\n"
"}\n"
".gauge-fg-alt{stroke:var(--indigo); filter:drop-shadow(0 0 6px rgba(106,92,255,.5));}\n"
".gauge-fg-warn{stroke:var(--amber); filter:drop-shadow(0 0 6px rgba(255,181,69,.5));}\n"
".gauge-value{position:absolute; inset:0; display:flex; flex-direction:column; align-items:center; justify-content:center;}\n"
".gauge-value span{font-family:var(--font-mono); font-size:.95rem; font-weight:700;}\n"
".gauge-value small{font-size:.55rem; letter-spacing:.08rem; color:var(--text-1); margin-top:2px;}\n"
"\n"
".relay-grid{\n"
"  display:grid; grid-template-columns:repeat(auto-fit,minmax(240px,1fr));\n"
"  gap:1.1rem; margin-bottom:2rem;\n"
"}\n"
".relay-card{\n"
"  border-radius:var(--r-lg); padding:1.3rem 1.3rem 1.1rem; position:relative; overflow:hidden;\n"
"  transition:transform .25s ease, box-shadow .25s ease, border-color .25s ease;\n"
"}\n"
".relay-card:hover{transform:translateY(-3px); border-color:var(--cyan); box-shadow:0 14px 30px -12px rgba(0,217,255,.35);}\n"
".relay-card.on{border-color:rgba(0,255,200,.4);}\n"
".rc-top{display:flex; align-items:flex-start; justify-content:space-between; gap:.6rem;}\n"
".rc-icon{\n"
"  width:44px; height:44px; flex:none; display:flex; align-items:center; justify-content:center;\n"
"  clip-path:polygon(25% 3%,75% 3%,100% 50%,75% 97%,25% 97%,0% 50%);\n"
"  background:rgba(255,255,255,.04); border:1px solid var(--glass-border-soft); color:var(--text-1);\n"
"  transition:.3s;\n"
"}\n"
".rc-icon svg{width:22px; height:22px; stroke:currentColor; fill:none; stroke-width:1.7; stroke-linecap:round; stroke-linejoin:round;}\n"
".relay-card.on .rc-icon{color:var(--teal); border-color:rgba(0,255,200,.5); box-shadow:0 0 16px -4px rgba(0,255,200,.6);}\n"
".rc-status{width:9px; height:9px; border-radius:50%; background:var(--text-2); margin-top:6px; transition:.3s;}\n"
".relay-card.on .rc-status{background:var(--teal); box-shadow:0 0 8px var(--teal);}\n"
".rc-name-wrap{flex:1; min-width:0; margin:0 .6rem;}\n"
".rc-name{\n"
"  width:100%; background:transparent; border:none; border-bottom:1px dashed transparent;\n"
"  color:var(--text-0); font-size:.92rem; font-weight:600; padding:.15rem 0;\n"
"}\n"
".rc-name:hover,.rc-name:focus{border-bottom-color:var(--glass-border);}\n"
".rc-channel{font-size:.62rem; letter-spacing:.08rem; color:var(--text-2); text-transform:uppercase;}\n"
"\n"
".rc-toggle-row{display:flex; align-items:center; justify-content:space-between; margin-top:1.1rem;}\n"
".toggle{\n"
"  width:56px; height:30px; border-radius:20px; background:rgba(255,255,255,.06);\n"
"  border:1px solid var(--glass-border-soft); position:relative; transition:.3s;\n"
"}\n"
".toggle::after{\n"
"  content:\"\"; position:absolute; top:3px; left:3px; width:22px; height:22px; border-radius:50%;\n"
"  background:var(--text-1); transition:.3s;\n"
"}\n"
".toggle.on{background:rgba(0,255,200,.14); border-color:rgba(0,255,200,.5);}\n"
".toggle.on::after{transform:translateX(26px); background:var(--teal); box-shadow:0 0 10px var(--teal);}\n"
".rc-state-text{font-family:var(--font-mono); font-size:.72rem; letter-spacing:.06rem; color:var(--text-1);}\n"
".relay-card.on .rc-state-text{color:var(--teal);}\n"
"\n"
".rc-activity{height:3px; border-radius:3px; background:rgba(255,255,255,.06); margin-top:1rem; overflow:hidden;}\n"
".rc-activity-fill{height:100%; width:0%; background:linear-gradient(90deg,var(--teal),var(--cyan)); transition:width .4s ease;}\n"
".relay-card.on .rc-activity-fill{width:100%; animation:activityPulse 1.8s ease-in-out infinite;}\n"
"@keyframes activityPulse{0%,100%{opacity:.5} 50%{opacity:1}}\n"
".rc-meta{display:flex; justify-content:space-between; margin-top:.5rem; font-size:.62rem; color:var(--text-2); font-family:var(--font-mono);}\n"
"\n"
".rc-loading{\n"
"  position:absolute; inset:0; background:rgba(5,8,16,.55); backdrop-filter:blur(2px);\n"
"  display:flex; align-items:center; justify-content:center; opacity:0; pointer-events:none; transition:opacity .2s;\n"
"}\n"
".relay-card.loading .rc-loading{opacity:1; pointer-events:auto;}\n"
"\n"
".scenes-title{font-size:.7rem; letter-spacing:.15rem; color:var(--text-1); text-transform:uppercase; margin:0 .3rem .8rem;}\n"
".scenes-row{display:flex; gap:.7rem; flex-wrap:wrap;}\n"
".scene-chip{\n"
"  display:flex; align-items:center; gap:.55rem; padding:.65rem 1rem; border-radius:14px;\n"
"  background:var(--glass-fill); border:1px solid var(--glass-border-soft); color:var(--text-0);\n"
"  font-size:.76rem; font-weight:600; letter-spacing:.03rem; transition:.2s;\n"
"}\n"
".scene-chip svg{width:17px; height:17px; stroke:currentColor; fill:none; stroke-width:1.8; stroke-linecap:round; stroke-linejoin:round; color:var(--cyan);}\n"
".scene-chip:hover{border-color:var(--cyan); transform:translateY(-2px); box-shadow:0 10px 20px -10px rgba(0,217,255,.5);}\n"
".scene-chip.danger svg{color:var(--danger);}\n"
".scene-chip.danger:hover{border-color:var(--danger); box-shadow:0 10px 20px -10px rgba(255,59,92,.5);}\n"
".scene-chip.active{border-color:var(--teal); box-shadow:0 0 14px -4px rgba(0,255,200,.6);}\n"
".scene-chip.active svg{color:var(--teal);}\n"
"\n"
"/* ---------------- SETTINGS DRAWER ---------------- */\n"
".settings-overlay{\n"
"  position:fixed; inset:0; background:rgba(2,4,10,.55); backdrop-filter:blur(3px);\n"
"  z-index:8; opacity:0; pointer-events:none; transition:opacity .3s ease;\n"
"}\n"
".settings-overlay.open{opacity:1; pointer-events:auto;}\n"
".settings-drawer{\n"
"  position:fixed; top:0; right:0; bottom:0; width:min(400px,92vw);\n"
"  border-radius:0; border-right:none; border-top:none; border-bottom:none;\n"
"  transform:translateX(100%); transition:transform .35s ease; overflow-y:auto; padding:1.4rem 1.3rem 2rem;\n"
"}\n"
".settings-overlay.open .settings-drawer{transform:translateX(0);}\n"
".drawer-header{display:flex; align-items:center; justify-content:space-between; margin-bottom:1.4rem;}\n"
".drawer-section{margin-bottom:1.6rem;}\n"
".drawer-label{font-size:.66rem; letter-spacing:.14rem; color:var(--text-1); text-transform:uppercase; margin-bottom:.7rem;}\n"
".diag-grid{display:grid; grid-template-columns:1fr 1fr; gap:.6rem .8rem;}\n"
".diag-item{background:rgba(255,255,255,.03); border:1px solid var(--glass-border-soft); border-radius:10px; padding:.55rem .7rem;}\n"
".diag-item .k{font-size:.6rem; color:var(--text-2); text-transform:uppercase; letter-spacing:.05rem;}\n"
".diag-item .v{font-family:var(--font-mono); font-size:.82rem; margin-top:2px; word-break:break-all;}\n"
"#wifi-form input{\n"
"  width:100%; padding:.65rem .75rem; margin-bottom:.6rem; background:rgba(255,255,255,.04);\n"
"  border:1px solid var(--glass-border-soft); border-radius:8px; color:var(--text-0); font-size:.85rem;\n"
"}\n"
".drawer-actions{display:flex; flex-direction:column; gap:.6rem;}\n"
".drawer-actions button{margin-top:0;}\n"
".about{font-size:.68rem; color:var(--text-2); line-height:1.6; border-top:1px solid var(--glass-border-soft); padding-top:1rem;}\n"
"\n"
"/* ---------------- CONFIRM MODAL ---------------- */\n"
".confirm-overlay{\n"
"  position:fixed; inset:0; background:rgba(2,4,10,.6); backdrop-filter:blur(3px);\n"
"  z-index:12; display:flex; align-items:center; justify-content:center; padding:1.5rem;\n"
"  opacity:0; pointer-events:none; transition:opacity .25s ease;\n"
"}\n"
".confirm-overlay.open{opacity:1; pointer-events:auto;}\n"
".confirm-box{width:min(360px,92vw); border-radius:var(--r-md); padding:1.6rem;}\n"
".confirm-message{font-size:.9rem; line-height:1.5; margin-bottom:1.3rem;}\n"
".confirm-actions{display:flex; gap:.7rem;}\n"
".confirm-actions button{margin-top:0; flex:1;}\n"
"\n"
"/* ---------------- TOASTS ---------------- */\n"
".toast-container{position:fixed; bottom:1.2rem; right:1.2rem; z-index:20; display:flex; flex-direction:column; gap:.6rem; max-width:min(320px,90vw);}\n"
".toast{\n"
"  padding:.75rem 1rem; border-radius:12px; font-size:.78rem; font-weight:600;\n"
"  border-left:3px solid var(--cyan); animation:toastIn .3s ease, toastOut .3s ease 3.2s forwards;\n"
"}\n"
".toast.error{border-left-color:var(--danger);}\n"
"@keyframes toastIn{from{opacity:0; transform:translateX(20px);} to{opacity:1; transform:translateX(0);}}\n"
"@keyframes toastOut{to{opacity:0; transform:translateX(20px);}}\n"
"\n"
"@media (max-width:640px){\n"
"  .topbar{flex-direction:row; align-items:center;}\n"
"  .topbar-mid{order:3; width:100%; justify-content:center; margin-top:.5rem;}\n"
"  .power-cluster{flex-direction:column;}\n"
"}\n"
"\n"
"body.reduced-motion .radar-sweep,\n"
"body.reduced-motion .arc-ring-dash,\n"
"body.reduced-motion .login-ring,\n"
"body.reduced-motion .boot-logo,\n"
"body.reduced-motion .brand-dot,\n"
"body.reduced-motion .rc-activity-fill{animation:none !important;}\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<canvas id=\"particles\"></canvas>\n"
"\n"
"<div id=\"screen-boot\" class=\"screen screen-center active\">\n"
"  <div class=\"boot-logo\">JARVIS</div>\n"
"  <div class=\"boot-lines\" id=\"boot-lines\"></div>\n"
"  <div class=\"boot-bar\"><div class=\"boot-bar-fill\" id=\"boot-bar-fill\"></div></div>\n"
"</div>\n"
"\n"
"<div id=\"screen-login\" class=\"screen screen-center\">\n"
"  <div class=\"login-panel glass\">\n"
"    <div class=\"login-ring\"></div>\n"
"    <h1 class=\"hud-title\">JARVIS ACCESS TERMINAL</h1>\n"
"    <p class=\"hud-sub\">Authorization Required</p>\n"
"    <form id=\"login-form\" autocomplete=\"on\">\n"
"      <label class=\"hud-label\" for=\"login-user\">Operator ID</label>\n"
"      <input type=\"text\" id=\"login-user\" autocomplete=\"username\" required>\n"
"      <label class=\"hud-label\" for=\"login-pass\">Access Code</label>\n"
"      <input type=\"password\" id=\"login-pass\" autocomplete=\"current-password\" required>\n"
"      <button type=\"submit\" class=\"btn-primary\" id=\"login-btn\"><span>AUTHENTICATE</span></button>\n"
"      <div class=\"login-error\" id=\"login-error\"></div>\n"
"    </form>\n"
"  </div>\n"
"</div>\n"
"\n"
"<div id=\"screen-dashboard\" class=\"screen\">\n"
"  <header class=\"topbar glass\">\n"
"    <div class=\"brand\">\n"
"      <div class=\"brand-dot\" id=\"brand-dot\"></div>\n"
"      <div>\n"
"        <div class=\"brand-title\">JARVIS SMART BOARD <span class=\"brand-credit\">by Shivans</span></div>\n"
"        <div class=\"brand-sub\" id=\"conn-status\">SYSTEM ONLINE</div>\n"
"      </div>\n"
"    </div>\n"
"    <div class=\"topbar-mid\">\n"
"      <div class=\"radar\" id=\"radar\">\n"
"        <div class=\"radar-sweep\"></div>\n"
"        <div class=\"radar-rings\"></div>\n"
"      </div>\n"
"      <div class=\"clients\"><span id=\"client-count\">0</span> DEVICES LINKED</div>\n"
"    </div>\n"
"    <div class=\"topbar-right\">\n"
"      <div class=\"clock\" id=\"clock\">00:00:00</div>\n"
"      <button class=\"icon-btn\" id=\"settings-btn\" title=\"Settings\" aria-label=\"Open settings\">&#9881;</button>\n"
"    </div>\n"
"  </header>\n"
"\n"
"  <main class=\"dashboard-main\">\n"
"    <section class=\"power-cluster\">\n"
"      <button class=\"arc-reactor\" id=\"master-btn\" aria-label=\"Master Power Toggle\">\n"
"        <svg viewBox=\"0 0 200 200\" class=\"arc-svg\">\n"
"          <circle class=\"arc-ring-outer\" cx=\"100\" cy=\"100\" r=\"92\"></circle>\n"
"          <circle class=\"arc-ring-dash\" cx=\"100\" cy=\"100\" r=\"78\"></circle>\n"
"          <circle class=\"arc-ring-core\" cx=\"100\" cy=\"100\" r=\"55\"></circle>\n"
"        </svg>\n"
"        <div class=\"arc-label\">\n"
"          <div class=\"arc-power-icon\">&#9211;</div>\n"
"          <div class=\"arc-power-text\" id=\"master-text\">MASTER</div>\n"
"        </div>\n"
"      </button>\n"
"\n"
"      <div class=\"gauges\">\n"
"        <div class=\"gauge\" id=\"gauge-heap\">\n"
"          <svg viewBox=\"0 0 100 100\"><circle class=\"gauge-bg\" cx=\"50\" cy=\"50\" r=\"42\"></circle><circle class=\"gauge-fg\" id=\"gauge-heap-fg\" cx=\"50\" cy=\"50\" r=\"42\"></circle></svg>\n"
"          <div class=\"gauge-value\"><span id=\"gauge-heap-val\">--</span><small>FREE MEM</small></div>\n"
"        </div>\n"
"        <div class=\"gauge\" id=\"gauge-uptime\">\n"
"          <svg viewBox=\"0 0 100 100\"><circle class=\"gauge-bg\" cx=\"50\" cy=\"50\" r=\"42\"></circle><circle class=\"gauge-fg gauge-fg-alt\" id=\"gauge-uptime-fg\" cx=\"50\" cy=\"50\" r=\"42\"></circle></svg>\n"
"          <div class=\"gauge-value\"><span id=\"gauge-uptime-val\">--</span><small>UPTIME</small></div>\n"
"        </div>\n"
"        <div class=\"gauge\" id=\"gauge-temp\">\n"
"          <svg viewBox=\"0 0 100 100\"><circle class=\"gauge-bg\" cx=\"50\" cy=\"50\" r=\"42\"></circle><circle class=\"gauge-fg gauge-fg-warn\" id=\"gauge-temp-fg\" cx=\"50\" cy=\"50\" r=\"42\"></circle></svg>\n"
"          <div class=\"gauge-value\"><span id=\"gauge-temp-val\">--</span><small>CPU &deg;C</small></div>\n"
"        </div>\n"
"      </div>\n"
"    </section>\n"
"\n"
"    <section class=\"relay-grid\" id=\"relay-grid\"></section>\n"
"\n"
"    <section class=\"scenes\">\n"
"      <div class=\"scenes-title\">Automation Scenes</div>\n"
"      <div class=\"scenes-row\" id=\"scenes-row\"></div>\n"
"    </section>\n"
"  </main>\n"
"</div>\n"
"\n"
"<div id=\"settings-overlay\" class=\"settings-overlay\">\n"
"  <aside id=\"settings-drawer\" class=\"settings-drawer glass\">\n"
"    <div class=\"drawer-header\">\n"
"      <div class=\"hud-title small\">SYSTEM SETTINGS</div>\n"
"      <button class=\"icon-btn\" id=\"settings-close\" aria-label=\"Close settings\">&#10005;</button>\n"
"    </div>\n"
"\n"
"    <div class=\"drawer-section\">\n"
"      <div class=\"drawer-label\">Diagnostics</div>\n"
"      <div class=\"diag-grid\" id=\"diag-grid\"></div>\n"
"    </div>\n"
"\n"
"    <div class=\"drawer-section\">\n"
"      <div class=\"drawer-label\">WiFi Credentials</div>\n"
"      <form id=\"wifi-form\">\n"
"        <input type=\"text\" id=\"wifi-ssid\" placeholder=\"Network Name (SSID)\" maxlength=\"31\" required>\n"
"        <input type=\"text\" id=\"wifi-pass\" placeholder=\"Password (blank = open network)\" maxlength=\"63\">\n"
"        <button type=\"submit\" class=\"btn-secondary\">SAVE &amp; REBOOT</button>\n"
"      </form>\n"
"    </div>\n"
"\n"
"    <div class=\"drawer-section drawer-actions\">\n"
"      <button class=\"btn-outline\" id=\"btn-restart\">RESTART DEVICE</button>\n"
"      <button class=\"btn-danger\" id=\"btn-factory-reset\">FACTORY RESET</button>\n"
"      <button class=\"btn-outline\" id=\"btn-logout\">LOGOUT</button>\n"
"    </div>\n"
"\n"
"    <div class=\"drawer-section about\">\n"
"      JARVIS Smart Board &mdash; Firmware <span id=\"about-firmware\">--</span><br>\n"
"      Fully local. No cloud services. No internet connection required.\n"
"    </div>\n"
"  </aside>\n"
"</div>\n"
"\n"
"<div id=\"confirm-overlay\" class=\"confirm-overlay\">\n"
"  <div class=\"confirm-box glass\">\n"
"    <div class=\"confirm-message\" id=\"confirm-message\"></div>\n"
"    <div class=\"confirm-actions\">\n"
"      <button class=\"btn-outline\" id=\"confirm-cancel\">CANCEL</button>\n"
"      <button class=\"btn-danger\" id=\"confirm-ok\">CONFIRM</button>\n"
"    </div>\n"
"  </div>\n"
"</div>\n"
"\n"
"<div id=\"toast-container\" class=\"toast-container\"></div>\n"
"\n"
"<script>\n"
"(function(){\n"
"\"use strict\";\n"
"\n"
"/* =====================================================================\n"
"   CONSTANTS & ICONS\n"
"   ===================================================================== */\n"
"var RELAY_ICONS = [\n"
"  '<svg viewBox=\"0 0 24 24\"><path d=\"M9 18h6M10 21h4M12 3a6 6 0 0 0-3.5 10.9c.6.5 1 1.2 1 2.1h5c0-.9.4-1.6 1-2.1A6 6 0 0 0 12 3Z\"/></svg>',\n"
"  '<svg viewBox=\"0 0 24 24\"><circle cx=\"12\" cy=\"12\" r=\"1.5\"/><path d=\"M12 10.5C12 6 9 3 6 4c-1.2 3 1 6 6 6.5Zm0 3C12 18 15 21 18 20c1.2-3-1-6-6-6.5Zm-1.5-1.5C6 12 3 15 4 18c3 1.2 6-1 6.5-6Zm3 0C18 12 21 9 20 6c-3-1.2-6 1-6.5 6Z\"/></svg>',\n"
"  '<svg viewBox=\"0 0 24 24\"><rect x=\"3\" y=\"5\" width=\"18\" height=\"12\" rx=\"1.5\"/><path d=\"M8 21h8M12 17v4\"/></svg>',\n"
"  '<svg viewBox=\"0 0 24 24\"><path d=\"M9 2v6M15 2v6M7 8h10v4a5 5 0 0 1-10 0V8Zm5 9v5\"/></svg>',\n"
"  '<svg viewBox=\"0 0 24 24\"><path d=\"M13 2 4 14h6l-1 8 9-12h-6l1-8Z\"/></svg>',\n"
"  '<svg viewBox=\"0 0 24 24\"><rect x=\"7\" y=\"3\" width=\"10\" height=\"18\" rx=\"2\"/><circle cx=\"12\" cy=\"9\" r=\"1.4\"/><path d=\"M12 12.5v4\"/></svg>'\n"
"];\n"
"var SCENES = [\n"
"  {id:\"night\",    label:\"Night\",     danger:false, icon:'<svg viewBox=\"0 0 24 24\"><path d=\"M20 14.5A8.5 8.5 0 1 1 9.5 4a7 7 0 0 0 10.5 10.5Z\"/></svg>'},\n"
"  {id:\"movie\",    label:\"Movie\",     danger:false, icon:'<svg viewBox=\"0 0 24 24\"><path d=\"M3 9h18v10a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V9Zm0 0 2-5h3l-2 5m4 0 2-5h3l-2 5m4 0 2-5h2l-2 5\"/></svg>'},\n"
"  {id:\"gaming\",   label:\"Gaming\",    danger:false, icon:'<svg viewBox=\"0 0 24 24\"><path d=\"M6 6h12a4 4 0 0 1 4 4v4a3 3 0 0 1-5.2 2.1L15 14H9l-1.8 2.1A3 3 0 0 1 2 14v-4a4 4 0 0 1 4-4Z\"/><path d=\"M7 9v4M5 11h4M15.2 10.2h.01M17.2 12.2h.01\"/></svg>'},\n"
"  {id:\"sleep\",    label:\"Sleep\",     danger:false, icon:'<svg viewBox=\"0 0 24 24\"><path d=\"M4 6h8l-8 8h8M14 8h5l-5 5h5\"/></svg>'},\n"
"  {id:\"vacation\", label:\"Vacation\",  danger:false, icon:'<svg viewBox=\"0 0 24 24\"><circle cx=\"12\" cy=\"12\" r=\"4\"/><path d=\"M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1\"/></svg>'},\n"
"  {id:\"emergency\",label:\"Emergency\", danger:true,  icon:'<svg viewBox=\"0 0 24 24\"><path d=\"M12 3 2 20h20L12 3Zm0 6v5m0 3h.01\"/></svg>'}\n"
"];\n"
"var HEAP_GAUGE_CIRC = 2 * Math.PI * 42; // matches r=42 in the gauge SVGs\n"
"\n"
"/* =====================================================================\n"
"   STATE\n"
"   ===================================================================== */\n"
"var pollTimer = null;\n"
"var isFetching = false;\n"
"var reconnecting = false;\n"
"var lastClientCount = -1;\n"
"var audioCtx = null;\n"
"var pendingConfirmAction = null;\n"
"var reducedMotion = window.matchMedia && window.matchMedia(\"(prefers-reduced-motion: reduce)\").matches;\n"
"\n"
"/* =====================================================================\n"
"   BOOT SEQUENCE\n"
"   ===================================================================== */\n"
"var BOOT_LINES = [\"Scanning devices...\", \"Initializing relays...\", \"Power grid online...\", \"JARVIS activated.\"];\n"
"\n"
"function runBootSequence(){\n"
"  var container = document.getElementById(\"boot-lines\");\n"
"  var fill = document.getElementById(\"boot-bar-fill\");\n"
"  var i = 0;\n"
"  function next(){\n"
"    if(i < BOOT_LINES.length){\n"
"      var div = document.createElement(\"div\");\n"
"      div.className = \"boot-line\";\n"
"      div.textContent = BOOT_LINES[i];\n"
"      container.appendChild(div);\n"
"      i++;\n"
"      fill.style.width = Math.round((i / BOOT_LINES.length) * 100) + \"%\";\n"
"      setTimeout(next, reducedMotion ? 120 : 550);\n"
"    } else {\n"
"      setTimeout(function(){\n"
"        showScreen(\"screen-login\");\n"
"        checkSession();\n"
"      }, reducedMotion ? 100 : 450);\n"
"    }\n"
"  }\n"
"  next();\n"
"}\n"
"\n"
"/* =====================================================================\n"
"   SCREEN MANAGEMENT\n"
"   ===================================================================== */\n"
"function showScreen(id){\n"
"  var screens = document.querySelectorAll(\".screen\");\n"
"  for(var i = 0; i < screens.length; i++){\n"
"    screens[i].classList.toggle(\"active\", screens[i].id === id);\n"
"  }\n"
"}\n"
"\n"
"/* =====================================================================\n"
"   API HELPER\n"
"   ===================================================================== */\n"
"function api(path, opts){\n"
"  opts = opts || {};\n"
"  opts.credentials = \"same-origin\";\n"
"  return fetch(path, opts).then(function(res){\n"
"    return res.json().then(function(data){\n"
"      return {ok: res.ok, status: res.status, data: data};\n"
"    }).catch(function(){\n"
"      return {ok: res.ok, status: res.status, data: {}};\n"
"    });\n"
"  });\n"
"}\n"
"\n"
"function postForm(path, params){\n"
"  var body = Object.keys(params).map(function(k){\n"
"    return encodeURIComponent(k) + \"=\" + encodeURIComponent(params[k]);\n"
"  }).join(\"&\");\n"
"  return api(path, {\n"
"    method: \"POST\",\n"
"    headers: {\"Content-Type\": \"application/x-www-form-urlencoded\"},\n"
"    body: body\n"
"  });\n"
"}\n"
"\n"
"/* =====================================================================\n"
"   SESSION / LOGIN\n"
"   ===================================================================== */\n"
"function checkSession(){\n"
"  api(\"/status\").then(function(r){\n"
"    if(r.ok && r.data.authenticated){\n"
"      enterDashboard(r.data);\n"
"    } else {\n"
"      showScreen(\"screen-login\");\n"
"    }\n"
"  }).catch(function(){\n"
"    showScreen(\"screen-login\");\n"
"  });\n"
"}\n"
"\n"
"document.getElementById(\"login-form\").addEventListener(\"submit\", function(e){\n"
"  e.preventDefault();\n"
"  var btn = document.getElementById(\"login-btn\");\n"
"  var errBox = document.getElementById(\"login-error\");\n"
"  var user = document.getElementById(\"login-user\").value;\n"
"  var pass = document.getElementById(\"login-pass\").value;\n"
"\n"
"  errBox.textContent = \"\";\n"
"  errBox.classList.remove(\"shake\");\n"
"  btn.disabled = true;\n"
"  btn.innerHTML = '<span class=\"spinner\"></span>AUTHENTICATING';\n"
"  ensureAudio();\n"
"\n"
"  postForm(\"/login\", {username: user, password: pass}).then(function(r){\n"
"    btn.disabled = false;\n"
"    btn.innerHTML = \"<span>AUTHENTICATE</span>\";\n"
"    if(r.ok && r.data.success){\n"
"      document.getElementById(\"login-pass\").value = \"\";\n"
"      api(\"/status\").then(function(sr){\n"
"        if(sr.ok && sr.data.authenticated){ enterDashboard(sr.data); }\n"
"      });\n"
"    } else {\n"
"      errBox.textContent = (r.data && r.data.message) ? r.data.message : \"Access Denied\";\n"
"      errBox.classList.add(\"shake\");\n"
"      playTone(220, 0.18);\n"
"    }\n"
"  }).catch(function(){\n"
"    btn.disabled = false;\n"
"    btn.innerHTML = \"<span>AUTHENTICATE</span>\";\n"
"    errBox.textContent = \"Connection error. Try again.\";\n"
"    errBox.classList.add(\"shake\");\n"
"  });\n"
"});\n"
"\n"
"function enterDashboard(data){\n"
"  showScreen(\"screen-dashboard\");\n"
"  buildRelayCards();\n"
"  buildScenes();\n"
"  startClock();\n"
"  render(data);\n"
"  startPolling();\n"
"}\n"
"\n"
"/* =====================================================================\n"
"   POLLING / HEARTBEAT (every second)\n"
"   ===================================================================== */\n"
"function startPolling(){\n"
"  stopPolling();\n"
"  pollTimer = setInterval(refreshStatus, 1000);\n"
"}\n"
"function stopPolling(){\n"
"  if(pollTimer){ clearInterval(pollTimer); pollTimer = null; }\n"
"}\n"
"function refreshStatus(){\n"
"  if(isFetching) return;\n"
"  isFetching = true;\n"
"  api(\"/status\").then(function(r){\n"
"    isFetching = false;\n"
"    if(r.ok && r.data.authenticated){\n"
"      setReconnecting(false);\n"
"      render(r.data);\n"
"    } else if(r.status === 401){\n"
"      stopPolling();\n"
"      showToast(\"Session Expired\", true);\n"
"      showScreen(\"screen-login\");\n"
"    }\n"
"  }).catch(function(){\n"
"    isFetching = false;\n"
"    setReconnecting(true);\n"
"  });\n"
"}\n"
"function setReconnecting(state){\n"
"  if(state === reconnecting) return;\n"
"  reconnecting = state;\n"
"  document.getElementById(\"conn-status\").textContent = state ? \"RECONNECTING...\" : \"SYSTEM ONLINE\";\n"
"  document.getElementById(\"brand-dot\").classList.toggle(\"warn\", state);\n"
"}\n"
"\n"
"/* =====================================================================\n"
"   RENDER\n"
"   ===================================================================== */\n"
"function render(data){\n"
"  // Clock uses the browser's own local time (see note in dashboard build\n"
"  // steps: the ESP32 has no internet/NTP access in Access Point mode).\n"
"\n"
"  // Gauges\n"
"  setGauge(\"gauge-heap-fg\", \"gauge-heap-val\", data.heapPercent, data.heapPercent + \"%\");\n"
"  var upPct = Math.min(100, Math.round((data.uptimeSeconds % 3600) / 3600 * 100));\n"
"  setGauge(\"gauge-uptime-fg\", \"gauge-uptime-val\", upPct, formatUptimeShort(data.uptimeSeconds));\n"
"  var tempPct = Math.max(0, Math.min(100, Math.round((data.tempC / 90) * 100)));\n"
"  setGauge(\"gauge-temp-fg\", \"gauge-temp-val\", tempPct, Math.round(data.tempC) + \"&deg;\");\n"
"\n"
"  // Client count + radar\n"
"  document.getElementById(\"client-count\").textContent = data.connectedClients;\n"
"  if(data.connectedClients !== lastClientCount){\n"
"    renderRadarBlips(data.connectedClients);\n"
"    lastClientCount = data.connectedClients;\n"
"  }\n"
"\n"
"  // Relay cards\n"
"  var allOn = true, anyOn = false;\n"
"  data.relays.forEach(function(relay){\n"
"    updateRelayCard(relay);\n"
"    if(relay.state){ anyOn = true; } else { allOn = false; }\n"
"  });\n"
"  var masterBtn = document.getElementById(\"master-btn\");\n"
"  masterBtn.classList.toggle(\"on\", allOn);\n"
"  masterBtn.dataset.allOn = allOn ? \"1\" : \"0\";\n"
"  document.getElementById(\"master-text\").textContent = allOn ? \"ALL ON\" : (anyOn ? \"PARTIAL\" : \"MASTER\");\n"
"\n"
"  // Vacation chip state\n"
"  var vacChip = document.querySelector('.scene-chip[data-scene=\"vacation\"]');\n"
"  if(vacChip){ vacChip.classList.toggle(\"active\", !!data.vacationMode); }\n"
"\n"
"  // Diagnostics + about\n"
"  renderDiagnostics(data);\n"
"  document.getElementById(\"about-firmware\").textContent = data.firmware;\n"
"\n"
"  // Pre-fill WiFi form with the current SSID (once, not on every poll)\n"
"  var ssidField = document.getElementById(\"wifi-ssid\");\n"
"  if(ssidField && !ssidField.dataset.touched){\n"
"    ssidField.value = data.ssid;\n"
"  }\n"
"}\n"
"\n"
"function setGauge(fgId, valId, pct, label){\n"
"  var fg = document.getElementById(fgId);\n"
"  var offset = HEAP_GAUGE_CIRC * (1 - Math.max(0, Math.min(100, pct)) / 100);\n"
"  fg.style.strokeDasharray = HEAP_GAUGE_CIRC;\n"
"  fg.style.strokeDashoffset = offset;\n"
"  document.getElementById(valId).innerHTML = label;\n"
"}\n"
"\n"
"function formatUptimeShort(totalSeconds){\n"
"  var d = Math.floor(totalSeconds / 86400);\n"
"  var h = Math.floor((totalSeconds % 86400) / 3600);\n"
"  var m = Math.floor((totalSeconds % 3600) / 60);\n"
"  if(d > 0) return d + \"d \" + h + \"h\";\n"
"  if(h > 0) return h + \"h \" + m + \"m\";\n"
"  return m + \"m\";\n"
"}\n"
"\n"
"function renderRadarBlips(count){\n"
"  var radar = document.getElementById(\"radar\");\n"
"  var old = radar.querySelectorAll(\".radar-blip\");\n"
"  for(var i = 0; i < old.length; i++){ old[i].remove(); }\n"
"  var shown = Math.min(count, 6);\n"
"  for(var b = 0; b < shown; b++){\n"
"    var angle = (b / Math.max(shown,1)) * Math.PI * 2;\n"
"    var radius = 10 + (b % 2) * 3;\n"
"    var blip = document.createElement(\"div\");\n"
"    blip.className = \"radar-blip\";\n"
"    blip.style.left = (19 + Math.cos(angle) * radius) + \"px\";\n"
"    blip.style.top = (19 + Math.sin(angle) * radius) + \"px\";\n"
"    radar.appendChild(blip);\n"
"  }\n"
"}\n"
"\n"
"function renderDiagnostics(data){\n"
"  var grid = document.getElementById(\"diag-grid\");\n"
"  var rows = [\n"
"    [\"Chip Model\", data.chipModel + \" rev\" + data.chipRevision],\n"
"    [\"CPU Freq\", data.cpuFreqMhz + \" MHz\"],\n"
"    [\"Flash Size\", data.flashSizeMB + \" MB\"],\n"
"    [\"SDK Version\", data.sdkVersion],\n"
"    [\"Free Heap\", Math.round(data.freeHeap/1024) + \" KB / \" + Math.round(data.totalHeap/1024) + \" KB\"],\n"
"    [\"Device IP\", data.ip],\n"
"    [\"Firmware\", data.firmware]\n"
"  ];\n"
"  grid.innerHTML = rows.map(function(r){\n"
"    return '<div class=\"diag-item\"><div class=\"k\">' + r[0] + '</div><div class=\"v\">' + r[1] + '</div></div>';\n"
"  }).join(\"\");\n"
"}\n"
"\n"
"/* =====================================================================\n"
"   RELAY CARDS\n"
"   ===================================================================== */\n"
"function buildRelayCards(){\n"
"  var grid = document.getElementById(\"relay-grid\");\n"
"  if(grid.dataset.built) return;\n"
"  var html = \"\";\n"
"  for(var i = 0; i < RELAY_ICONS.length; i++){\n"
"    html += '' +\n"
"      '<div class=\"relay-card glass\" id=\"relay-card-' + i + '\" data-ch=\"' + (i+1) + '\">' +\n"
"        '<div class=\"rc-loading\"><span class=\"spinner\"></span></div>' +\n"
"        '<div class=\"rc-top\">' +\n"
"          '<div class=\"rc-icon\">' + RELAY_ICONS[i] + '</div>' +\n"
"          '<div class=\"rc-name-wrap\">' +\n"
"            '<input class=\"rc-name\" id=\"relay-name-' + i + '\" maxlength=\"24\">' +\n"
"            '<div class=\"rc-channel\">Channel ' + (i+1) + '</div>' +\n"
"          '</div>' +\n"
"          '<div class=\"rc-status\" id=\"relay-status-' + i + '\"></div>' +\n"
"        '</div>' +\n"
"        '<div class=\"rc-toggle-row\">' +\n"
"          '<button class=\"toggle\" id=\"relay-toggle-' + i + '\" aria-label=\"Toggle relay ' + (i+1) + '\"></button>' +\n"
"          '<span class=\"rc-state-text\" id=\"relay-state-text-' + i + '\">OFF</span>' +\n"
"        '</div>' +\n"
"        '<div class=\"rc-activity\"><div class=\"rc-activity-fill\"></div></div>' +\n"
"        '<div class=\"rc-meta\"><span>ACTIVITY</span><span id=\"relay-lastswitch-' + i + '\">--</span></div>' +\n"
"      '</div>';\n"
"  }\n"
"  grid.innerHTML = html;\n"
"  grid.dataset.built = \"1\";\n"
"  grid.title = \"\";\n"
"\n"
"  for(var j = 0; j < RELAY_ICONS.length; j++){\n"
"    (function(idx){\n"
"      document.getElementById(\"relay-toggle-\" + idx).addEventListener(\"click\", function(){\n"
"        toggleRelay(idx);\n"
"      });\n"
"      document.getElementById(\"relay-name-\" + idx).addEventListener(\"blur\", function(e){\n"
"        saveRelayName(idx, e.target.value);\n"
"      });\n"
"      document.getElementById(\"relay-name-\" + idx).addEventListener(\"keydown\", function(e){\n"
"        if(e.key === \"Enter\"){ e.target.blur(); }\n"
"      });\n"
"    })(j);\n"
"  }\n"
"\n"
"  // \"Simulated\" indicator — no current sensor on this hardware; the\n"
"  // activity bar reflects ON/OFF state only, not measured wattage.\n"
"  var metas = grid.querySelectorAll(\".rc-meta span:first-child\");\n"
"  metas.forEach(function(el){ el.title = \"Simulated indicator — no current sensor installed\"; });\n"
"}\n"
"\n"
"function updateRelayCard(relay){\n"
"  var idx = relay.channel - 1;\n"
"  var card = document.getElementById(\"relay-card-\" + idx);\n"
"  if(!card) return;\n"
"  card.classList.remove(\"loading\");\n"
"  card.classList.toggle(\"on\", relay.state);\n"
"  document.getElementById(\"relay-toggle-\" + idx).classList.toggle(\"on\", relay.state);\n"
"  document.getElementById(\"relay-state-text-\" + idx).textContent = relay.state ? \"ON\" : \"OFF\";\n"
"\n"
"  var nameField = document.getElementById(\"relay-name-\" + idx);\n"
"  if(document.activeElement !== nameField){ nameField.value = relay.name; }\n"
"\n"
"  document.getElementById(\"relay-lastswitch-\" + idx).textContent =\n"
"    relay.everSwitched ? formatAgo(relay.secondsAgo) : \"--\";\n"
"}\n"
"\n"
"function formatAgo(seconds){\n"
"  if(seconds < 60) return seconds + \"s ago\";\n"
"  if(seconds < 3600) return Math.floor(seconds/60) + \"m ago\";\n"
"  return Math.floor(seconds/3600) + \"h ago\";\n"
"}\n"
"\n"
"function toggleRelay(idx){\n"
"  var card = document.getElementById(\"relay-card-\" + idx);\n"
"  var currentlyOn = card.classList.contains(\"on\");\n"
"  var nextState = currentlyOn ? 0 : 1;\n"
"  card.classList.add(\"loading\");\n"
"  ensureAudio();\n"
"\n"
"  api(\"/relay?ch=\" + (idx+1) + \"&state=\" + nextState).then(function(r){\n"
"    card.classList.remove(\"loading\");\n"
"    if(r.ok && r.data.success){\n"
"      updateRelayCard({channel: idx+1, state: r.data.state, name: document.getElementById(\"relay-name-\"+idx).value, everSwitched:true, secondsAgo:0});\n"
"      showToast(r.data.message, false);\n"
"      playTone(r.data.state ? 880 : 520, 0.12);\n"
"    } else if(r.status === 401){\n"
"      handleSessionExpired();\n"
"    } else {\n"
"      showToast((r.data && r.data.message) || \"Relay command failed\", true);\n"
"    }\n"
"  }).catch(function(){\n"
"    card.classList.remove(\"loading\");\n"
"    showToast(\"Connection error — command not confirmed\", true);\n"
"  });\n"
"}\n"
"\n"
"function saveRelayName(idx, name){\n"
"  name = name.trim();\n"
"  if(name.length === 0) return;\n"
"  document.getElementById(\"relay-name-\" + idx).dataset.touched = \"1\";\n"
"  postForm(\"/rename\", {ch: idx+1, name: name}).then(function(r){\n"
"    if(r.ok && r.data.success){\n"
"      showToast(\"Device renamed\", false);\n"
"    } else if(r.status === 401){\n"
"      handleSessionExpired();\n"
"    }\n"
"  });\n"
"}\n"
"\n"
"/* =====================================================================\n"
"   MASTER POWER\n"
"   ===================================================================== */\n"
"document.getElementById(\"master-btn\").addEventListener(\"click\", function(){\n"
"  var allOn = document.getElementById(\"master-btn\").dataset.allOn === \"1\";\n"
"  ensureAudio();\n"
"  applyScene(allOn ? \"master_off\" : \"master_on\");\n"
"});\n"
"\n"
"/* =====================================================================\n"
"   SCENES\n"
"   ===================================================================== */\n"
"function buildScenes(){\n"
"  var row = document.getElementById(\"scenes-row\");\n"
"  if(row.dataset.built) return;\n"
"  row.innerHTML = SCENES.map(function(s){\n"
"    return '<button class=\"scene-chip' + (s.danger ? \" danger\" : \"\") + '\" data-scene=\"' + s.id + '\">' + s.icon + '<span>' + s.label + '</span></button>';\n"
"  }).join(\"\");\n"
"  row.dataset.built = \"1\";\n"
"\n"
"  row.querySelectorAll(\".scene-chip\").forEach(function(chip){\n"
"    chip.addEventListener(\"click\", function(){\n"
"      var id = chip.dataset.scene;\n"
"      ensureAudio();\n"
"      if(id === \"emergency\"){\n"
"        confirmAction(\"Emergency Shutdown will immediately power OFF all four channels. Continue?\", function(){\n"
"          applyScene(\"emergency\");\n"
"        });\n"
"      } else {\n"
"        applyScene(id);\n"
"      }\n"
"    });\n"
"  });\n"
"}\n"
"\n"
"function applyScene(name){\n"
"  api(\"/scene?name=\" + encodeURIComponent(name)).then(function(r){\n"
"    if(r.ok && r.data.success){\n"
"      showToast(r.data.message, false);\n"
"      playTone(660, 0.14);\n"
"      refreshStatus();\n"
"    } else if(r.status === 401){\n"
"      handleSessionExpired();\n"
"    } else {\n"
"      showToast((r.data && r.data.message) || \"Scene failed\", true);\n"
"    }\n"
"  }).catch(function(){\n"
"    showToast(\"Connection error — scene not applied\", true);\n"
"  });\n"
"}\n"
"\n"
"function handleSessionExpired(){\n"
"  stopPolling();\n"
"  showToast(\"Session Expired\", true);\n"
"  showScreen(\"screen-login\");\n"
"}\n"
"\n"
"/* =====================================================================\n"
"   SETTINGS DRAWER\n"
"   ===================================================================== */\n"
"document.getElementById(\"settings-btn\").addEventListener(\"click\", function(){\n"
"  document.getElementById(\"settings-overlay\").classList.add(\"open\");\n"
"});\n"
"document.getElementById(\"settings-close\").addEventListener(\"click\", closeSettings);\n"
"document.getElementById(\"settings-overlay\").addEventListener(\"click\", function(e){\n"
"  if(e.target.id === \"settings-overlay\") closeSettings();\n"
"});\n"
"function closeSettings(){ document.getElementById(\"settings-overlay\").classList.remove(\"open\"); }\n"
"\n"
"document.getElementById(\"wifi-form\").addEventListener(\"submit\", function(e){\n"
"  e.preventDefault();\n"
"  var ssid = document.getElementById(\"wifi-ssid\").value.trim();\n"
"  var pass = document.getElementById(\"wifi-pass\").value;\n"
"  confirmAction(\"Save new WiFi credentials and reboot? You will need to reconnect to \\\"\" + ssid + \"\\\" afterward.\", function(){\n"
"    postForm(\"/wifi\", {ssid: ssid, password: pass}).then(function(r){\n"
"      if(r.ok && r.data.success){\n"
"        showRebootOverlay(\"Applying new WiFi credentials. Reconnect to \\\"\" + ssid + \"\\\" once the device restarts.\");\n"
"        return api(\"/restart\");\n"
"      } else {\n"
"        showToast((r.data && r.data.message) || \"Could not save WiFi settings\", true);\n"
"      }\n"
"    }).catch(function(){\n"
"      showToast(\"Connection error\", true);\n"
"    });\n"
"  });\n"
"});\n"
"\n"
"document.getElementById(\"btn-restart\").addEventListener(\"click\", function(){\n"
"  confirmAction(\"Restart JARVIS Smart Board now?\", function(){\n"
"    showRebootOverlay(\"Rebooting JARVIS Smart Board...\");\n"
"    api(\"/restart\").catch(function(){});\n"
"  });\n"
"});\n"
"\n"
"document.getElementById(\"btn-factory-reset\").addEventListener(\"click\", function(){\n"
"  confirmAction(\"Factory Reset will erase saved relay states, device names and WiFi credentials. This cannot be undone. Continue?\", function(){\n"
"    showRebootOverlay(\"Restoring factory defaults...\");\n"
"    api(\"/factoryreset\").catch(function(){});\n"
"  });\n"
"});\n"
"\n"
"document.getElementById(\"btn-logout\").addEventListener(\"click\", function(){\n"
"  api(\"/logout\").then(function(){\n"
"    stopPolling();\n"
"    closeSettings();\n"
"    showScreen(\"screen-login\");\n"
"  });\n"
"});\n"
"\n"
"function showRebootOverlay(message){\n"
"  stopPolling();\n"
"  closeSettings();\n"
"  var boot = document.getElementById(\"screen-boot\");\n"
"  document.getElementById(\"boot-lines\").innerHTML = '<div class=\"boot-line\" style=\"opacity:1\">' + message + '</div>';\n"
"  document.getElementById(\"boot-bar-fill\").style.width = \"100%\";\n"
"  showScreen(\"screen-boot\");\n"
"}\n"
"\n"
"/* =====================================================================\n"
"   CONFIRM MODAL\n"
"   ===================================================================== */\n"
"function confirmAction(message, onConfirm){\n"
"  document.getElementById(\"confirm-message\").textContent = message;\n"
"  pendingConfirmAction = onConfirm;\n"
"  document.getElementById(\"confirm-overlay\").classList.add(\"open\");\n"
"}\n"
"document.getElementById(\"confirm-cancel\").addEventListener(\"click\", function(){\n"
"  document.getElementById(\"confirm-overlay\").classList.remove(\"open\");\n"
"  pendingConfirmAction = null;\n"
"});\n"
"document.getElementById(\"confirm-ok\").addEventListener(\"click\", function(){\n"
"  document.getElementById(\"confirm-overlay\").classList.remove(\"open\");\n"
"  var action = pendingConfirmAction;\n"
"  pendingConfirmAction = null;\n"
"  if(action) action();\n"
"});\n"
"\n"
"/* =====================================================================\n"
"   CLOCK  (browser's local time — the ESP32 has no NTP/internet access\n"
"   while running as an isolated Access Point, so the connected\n"
"   phone/laptop's own clock is the most accurate source available)\n"
"   ===================================================================== */\n"
"function startClock(){\n"
"  function tick(){\n"
"    var now = new Date();\n"
"    var pad = function(n){ return n < 10 ? \"0\" + n : n; };\n"
"    document.getElementById(\"clock\").textContent =\n"
"      pad(now.getHours()) + \":\" + pad(now.getMinutes()) + \":\" + pad(now.getSeconds());\n"
"  }\n"
"  tick();\n"
"  setInterval(tick, 1000);\n"
"}\n"
"\n"
"/* =====================================================================\n"
"   TOASTS\n"
"   ===================================================================== */\n"
"function showToast(message, isError){\n"
"  if(!message) return;\n"
"  var container = document.getElementById(\"toast-container\");\n"
"  var toast = document.createElement(\"div\");\n"
"  toast.className = \"toast glass\" + (isError ? \" error\" : \"\");\n"
"  toast.textContent = message;\n"
"  container.appendChild(toast);\n"
"  setTimeout(function(){ toast.remove(); }, 3600);\n"
"}\n"
"\n"
"/* =====================================================================\n"
"   WEB AUDIO — tiny notification blip (no external audio files)\n"
"   ===================================================================== */\n"
"function ensureAudio(){\n"
"  if(audioCtx) return;\n"
"  try{\n"
"    var Ctx = window.AudioContext || window.webkitAudioContext;\n"
"    audioCtx = new Ctx();\n"
"  }catch(e){ audioCtx = null; }\n"
"}\n"
"function playTone(freq, duration){\n"
"  if(!audioCtx) return;\n"
"  try{\n"
"    var osc = audioCtx.createOscillator();\n"
"    var gain = audioCtx.createGain();\n"
"    osc.type = \"sine\";\n"
"    osc.frequency.value = freq;\n"
"    gain.gain.setValueAtTime(0.08, audioCtx.currentTime);\n"
"    gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + duration);\n"
"    osc.connect(gain);\n"
"    gain.connect(audioCtx.destination);\n"
"    osc.start();\n"
"    osc.stop(audioCtx.currentTime + duration);\n"
"  }catch(e){}\n"
"}\n"
"\n"
"/* =====================================================================\n"
"   PARTICLE BACKGROUND\n"
"   ===================================================================== */\n"
"function initParticles(){\n"
"  var canvas = document.getElementById(\"particles\");\n"
"  var ctx = canvas.getContext(\"2d\");\n"
"  var particles = [];\n"
"  var count = window.innerWidth < 640 ? 26 : 50;\n"
"\n"
"  function resize(){\n"
"    canvas.width = window.innerWidth;\n"
"    canvas.height = window.innerHeight;\n"
"  }\n"
"  resize();\n"
"  window.addEventListener(\"resize\", resize);\n"
"\n"
"  for(var i = 0; i < count; i++){\n"
"    particles.push({\n"
"      x: Math.random() * canvas.width,\n"
"      y: Math.random() * canvas.height,\n"
"      vx: (Math.random() - 0.5) * 0.25,\n"
"      vy: (Math.random() - 0.5) * 0.25,\n"
"      r: Math.random() * 1.6 + 0.6\n"
"    });\n"
"  }\n"
"\n"
"  if(reducedMotion){\n"
"    ctx.fillStyle = \"rgba(0,217,255,0.4)\";\n"
"    particles.forEach(function(p){\n"
"      ctx.beginPath(); ctx.arc(p.x, p.y, p.r, 0, Math.PI*2); ctx.fill();\n"
"    });\n"
"    return;\n"
"  }\n"
"\n"
"  function frame(){\n"
"    ctx.clearRect(0, 0, canvas.width, canvas.height);\n"
"    for(var i = 0; i < particles.length; i++){\n"
"      var p = particles[i];\n"
"      p.x += p.vx; p.y += p.vy;\n"
"      if(p.x < 0) p.x = canvas.width; if(p.x > canvas.width) p.x = 0;\n"
"      if(p.y < 0) p.y = canvas.height; if(p.y > canvas.height) p.y = 0;\n"
"      ctx.fillStyle = \"rgba(0,217,255,0.5)\";\n"
"      ctx.beginPath(); ctx.arc(p.x, p.y, p.r, 0, Math.PI*2); ctx.fill();\n"
"\n"
"      for(var j = i+1; j < particles.length; j++){\n"
"        var q = particles[j];\n"
"        var dx = p.x - q.x, dy = p.y - q.y;\n"
"        var dist = Math.sqrt(dx*dx + dy*dy);\n"
"        if(dist < 110){\n"
"          ctx.strokeStyle = \"rgba(0,217,255,\" + (0.12 * (1 - dist/110)) + \")\";\n"
"          ctx.lineWidth = 1;\n"
"          ctx.beginPath(); ctx.moveTo(p.x, p.y); ctx.lineTo(q.x, q.y); ctx.stroke();\n"
"        }\n"
"      }\n"
"    }\n"
"    requestAnimationFrame(frame);\n"
"  }\n"
"  requestAnimationFrame(frame);\n"
"}\n"
"\n"
"/* =====================================================================\n"
"   INIT\n"
"   ===================================================================== */\n"
"if(reducedMotion){ document.body.classList.add(\"reduced-motion\"); }\n"
"initParticles();\n"
"runBootSequence();\n"
"\n"
"})();\n"
"</script>\n"
"</body>\n"
"</html>\n";
// =====================================================================
//  CONFIGURATION
// =====================================================================

// ---- Default Access Point credentials (overridable from Settings) ----
const char* DEFAULT_AP_SSID     = "JARVIS_BOARD";
const char* DEFAULT_AP_PASSWORD = "IronMan@2026";

// ---- Login credentials (hardcoded — see SECURITY NOTE above) ----
const char* LOGIN_USER     = "admin";
const char* LOGIN_PASSWORD = "jarvis123";

// ---- Session ----
const unsigned long SESSION_TIMEOUT_MS = 15UL * 60UL * 1000UL; // 15 minutes, sliding

// ---- Firmware info ----
const char* FIRMWARE_VERSION = "1.0.0";

// ---- Relay pins (Active LOW module) ----
const uint8_t RELAY_COUNT = 6;
const uint8_t RELAY_PIN[RELAY_COUNT] = {23, 22, 21, 19, 18, 5};
const bool    RELAY_ON_LEVEL  = LOW;
const bool    RELAY_OFF_LEVEL = HIGH;

// ---- Vacation Mode scheduler ----
const unsigned long VACATION_MIN_INTERVAL_MS = 20UL * 60UL * 1000UL; // 20 min
const unsigned long VACATION_MAX_INTERVAL_MS = 45UL * 60UL * 1000UL; // 45 min

// =====================================================================
//  GLOBAL STATE
// =====================================================================

WebServer server(80);
Preferences prefs;

bool     relayState[RELAY_COUNT];
String   deviceName[RELAY_COUNT];
unsigned long lastSwitchedMillis[RELAY_COUNT] = {0, 0, 0, 0, 0, 0}; // 0 = not switched this session

String   apSSID;
String   apPassword;

String        sessionToken     = "";
unsigned long sessionExpiresAt = 0;

bool          vacationMode        = false;
unsigned long vacationNextToggle  = 0;

// =====================================================================
//  SETUP
// =====================================================================

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("====================================="));
  Serial.println(F(" JARVIS SMART BOARD — booting..."));
  Serial.println(F("====================================="));

  randomSeed(analogRead(0) + micros());

  loadPreferences();
  setupRelayPins();
  setupAP();
  setupServer();

  Serial.println(F("[OK] System online."));
  Serial.print(F("[OK] Dashboard: http://"));
  Serial.println(WiFi.softAPIP());
  Serial.print(F("[OK] SSID: "));
  Serial.println(apSSID);
}

void loop() {
  server.handleClient();
  runVacationScheduler();
}

// =====================================================================
//  PREFERENCES (persistent storage in flash / NVS)
// =====================================================================

void loadPreferences() {
  prefs.begin("jarvis", false);

  // Relay states — restore last known ON/OFF so the board resumes
  // exactly how it was left after a power cut.
  relayState[0] = prefs.getBool("r1", false);
  relayState[1] = prefs.getBool("r2", false);
  relayState[2] = prefs.getBool("r3", false);
  relayState[3] = prefs.getBool("r4", false);
  relayState[4] = prefs.getBool("r5", false);
  relayState[5] = prefs.getBool("r6", false);

  // Device names — user-editable, default to sensible appliance labels.
  deviceName[0] = prefs.getString("n1", "Room Light");
  deviceName[1] = prefs.getString("n2", "Ceiling Fan");
  deviceName[2] = prefs.getString("n3", "Television");
  deviceName[3] = prefs.getString("n4", "Power Socket");
  deviceName[4] = prefs.getString("n5", "Device 5");
  deviceName[5] = prefs.getString("n6", "Device 6");

  // AP credentials — fall back to firmware defaults on first boot.
  apSSID     = prefs.getString("ap_ssid", DEFAULT_AP_SSID);
  apPassword = prefs.getString("ap_pass", DEFAULT_AP_PASSWORD);

  prefs.end();
}

// =====================================================================
//  RELAY CONTROL
// =====================================================================

void setupRelayPins() {
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    pinMode(RELAY_PIN[i], OUTPUT);
    // Apply the restored state immediately so hardware matches memory.
    digitalWrite(RELAY_PIN[i], relayState[i] ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
  }
}

// Sets a single relay (ch is 0-5), persists it, and stamps the time.
void setRelay(uint8_t ch, bool on) {
  if (ch >= RELAY_COUNT) return;
  relayState[ch] = on;
  digitalWrite(RELAY_PIN[ch], on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
  lastSwitchedMillis[ch] = millis();

  prefs.begin("jarvis", false);
  switch (ch) {
    case 0: prefs.putBool("r1", on); break;
    case 1: prefs.putBool("r2", on); break;
    case 2: prefs.putBool("r3", on); break;
    case 3: prefs.putBool("r4", on); break;
    case 4: prefs.putBool("r5", on); break;
    case 5: prefs.putBool("r6", on); break;
  }
  prefs.end();
}

// Applies a named preset scene across all six relays.
// Returns a human-readable message describing what happened,
// which the frontend displays directly as the toast notification.
String applyScene(const String& scene) {
  if (scene == "master_on") {
    for (uint8_t i = 0; i < RELAY_COUNT; i++) setRelay(i, true);
    return "All Systems Activated";
  }
  if (scene == "master_off") {
    for (uint8_t i = 0; i < RELAY_COUNT; i++) setRelay(i, false);
    return "All Systems Deactivated";
  }
  if (scene == "emergency") {
    for (uint8_t i = 0; i < RELAY_COUNT; i++) setRelay(i, false);
    vacationMode = false;
    return "Emergency Shutdown Executed";
  }
  if (scene == "night") {
    setRelay(0, false); // Light off
    setRelay(1, true);  // Fan on
    setRelay(2, false); // TV off
    setRelay(3, true);  // Socket on (charging)
    setRelay(4, false); // Device 5 off (edit to taste)
    setRelay(5, false); // Device 6 off (edit to taste)
    return "Night Mode Engaged";
  }
  if (scene == "movie") {
    setRelay(0, false); // Light off
    setRelay(1, true);  // Fan on
    setRelay(2, true);  // TV on
    setRelay(3, true);  // Socket on
    setRelay(4, false); // Device 5 off (edit to taste)
    setRelay(5, false); // Device 6 off (edit to taste)
    return "Movie Mode Engaged";
  }
  if (scene == "gaming") {
    setRelay(0, true);  // Light on
    setRelay(1, true);  // Fan on
    setRelay(2, true);  // TV on
    setRelay(3, true);  // Socket on
    setRelay(4, false); // Device 5 off (edit to taste)
    setRelay(5, false); // Device 6 off (edit to taste)
    return "Gaming Mode Engaged";
  }
  if (scene == "sleep") {
    for (uint8_t i = 0; i < RELAY_COUNT; i++) setRelay(i, false);
    vacationMode = false;
    return "Sleep Mode Engaged";
  }
  if (scene == "vacation") {
    vacationMode = !vacationMode;
    if (vacationMode) {
      scheduleNextVacationToggle();
      return "Vacation Mode Engaged";
    } else {
      return "Vacation Mode Disengaged";
    }
  }
  return "";
}

// Non-blocking scheduler: while Vacation Mode is active, randomly
// toggles the light relay every 20-45 minutes to simulate occupancy.
void scheduleNextVacationToggle() {
  unsigned long span = VACATION_MAX_INTERVAL_MS - VACATION_MIN_INTERVAL_MS;
  unsigned long delayMs = VACATION_MIN_INTERVAL_MS + (unsigned long) random(0, span);
  vacationNextToggle = millis() + delayMs;
}

void runVacationScheduler() {
  if (!vacationMode) return;
  if ((long)(millis() - vacationNextToggle) >= 0) {
    setRelay(0, !relayState[0]); // flip the light relay
    scheduleNextVacationToggle();
  }
}

// =====================================================================
//  WIFI ACCESS POINT
// =====================================================================

void setupAP() {
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(apSSID.c_str(), apPassword.c_str());
  if (!ok) {
    Serial.println(F("[ERROR] Failed to start Access Point. Retrying with defaults..."));
    WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASSWORD);
  }
  delay(300); // let the AP interface settle before clients can connect
}

// =====================================================================
//  SESSION / AUTH HELPERS
// =====================================================================

String generateSessionToken() {
  const char hexChars[] = "0123456789abcdef";
  String token = "";
  for (uint8_t i = 0; i < 24; i++) {
    token += hexChars[random(0, 16)];
  }
  return token;
}

// Extracts a named value from a raw "Cookie" header string.
String getCookieValue(const String& cookieHeader, const String& key) {
  int start = cookieHeader.indexOf(key + "=");
  if (start == -1) return "";
  start += key.length() + 1;
  int end = cookieHeader.indexOf(';', start);
  if (end == -1) end = cookieHeader.length();
  return cookieHeader.substring(start, end);
}

// Checks whether the incoming request carries a valid, unexpired
// session cookie. Valid sessions are refreshed (sliding timeout).
bool isAuthenticated() {
  if (sessionToken.length() == 0) return false;
  String cookieHeader = server.header("Cookie");
  if (cookieHeader.length() == 0) return false;

  String presented = getCookieValue(cookieHeader, "session");
  if (presented.length() == 0 || presented != sessionToken) return false;

  if ((long)(millis() - sessionExpiresAt) > 0) {
    sessionToken = ""; // expired — invalidate
    return false;
  }

  sessionExpiresAt = millis() + SESSION_TIMEOUT_MS; // slide the timeout
  return true;
}

// Sends a standard 401 JSON response for protected routes.
void rejectUnauthorized() {
  server.send(401, "application/json", "{\"authenticated\":false,\"message\":\"Session expired or invalid\"}");
}

// =====================================================================
//  JSON HELPERS
// =====================================================================

String jsonEscape(const String& input) {
  String out;
  out.reserve(input.length() + 4);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    if (c == '"' || c == '\\') out += '\\';
    if (c == '\n') { out += "\\n"; continue; }
    out += c;
  }
  return out;
}

// =====================================================================
//  ROUTES
// =====================================================================

void setupServer() {
  const char* headerKeys[] = {"Cookie"};
  server.collectHeaders(headerKeys, 1);

  server.on("/",             HTTP_GET,  handleRoot);
  server.on("/login",        HTTP_POST, handleLogin);
  server.on("/logout",       HTTP_GET,  handleLogout);
  server.on("/status",       HTTP_GET,  handleStatus);
  server.on("/relay",        HTTP_GET,  handleRelay);
  server.on("/scene",        HTTP_GET,  handleScene);
  server.on("/restart",      HTTP_GET,  handleRestart);
  server.on("/factoryreset", HTTP_GET,  handleFactoryReset);
  server.on("/wifi",         HTTP_POST, handleWifiChange);
  server.on("/rename",       HTTP_POST, handleRename);
  server.onNotFound(handleNotFound);

  server.begin();
}

// ---- GET / ----
// Always serves the same static single-page app. The page itself
// decides (via a /status call) whether to show the login screen or
// the dashboard — keeping the server side template-free and fast.
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

// ---- POST /login ----
void handleLogin() {
  String user = server.arg("username");
  String pass = server.arg("password");

  if (user == LOGIN_USER && pass == LOGIN_PASSWORD) {
    sessionToken = generateSessionToken();
    sessionExpiresAt = millis() + SESSION_TIMEOUT_MS;

    server.sendHeader("Set-Cookie", "session=" + sessionToken + "; Path=/; HttpOnly");
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Access Granted\"}");
    Serial.println(F("[AUTH] Login successful."));
  } else {
    server.send(401, "application/json", "{\"success\":false,\"message\":\"Access Denied — invalid credentials\"}");
    Serial.println(F("[AUTH] Login failed."));
  }
}

// ---- GET /logout ----
void handleLogout() {
  sessionToken = "";
  server.sendHeader("Set-Cookie", "session=; Path=/; HttpOnly; Max-Age=0");
  server.send(200, "application/json", "{\"success\":true}");
}

// ---- GET /status ----
// Polled by the dashboard every second ("heartbeat"). Returns full
// live telemetry. Protected — returns 401 if not authenticated so
// the frontend knows to fall back to the login screen.
void handleStatus() {
  if (!isAuthenticated()) { rejectUnauthorized(); return; }

  uint32_t freeHeap  = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();
  uint8_t  heapPct    = totalHeap > 0 ? (uint8_t)((freeHeap * 100UL) / totalHeap) : 0;

  unsigned long upSeconds = millis() / 1000UL;

  String json = "{";
  json += "\"authenticated\":true,";
  json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"chipModel\":\"" + String(ESP.getChipModel()) + "\",";
  json += "\"chipRevision\":" + String(ESP.getChipRevision()) + ",";
  json += "\"cpuFreqMhz\":" + String(getCpuFrequencyMhz()) + ",";
  json += "\"flashSizeMB\":" + String(ESP.getFlashChipSize() / (1024 * 1024)) + ",";
  json += "\"sdkVersion\":\"" + String(ESP.getSdkVersion()) + "\",";
  json += "\"tempC\":" + String(temperatureRead(), 1) + ",";
  json += "\"freeHeap\":" + String(freeHeap) + ",";
  json += "\"totalHeap\":" + String(totalHeap) + ",";
  json += "\"heapPercent\":" + String(heapPct) + ",";
  json += "\"uptimeSeconds\":" + String(upSeconds) + ",";
  json += "\"connectedClients\":" + String(WiFi.softAPgetStationNum()) + ",";
  json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"ssid\":\"" + jsonEscape(apSSID) + "\",";
  json += "\"vacationMode\":" + String(vacationMode ? "true" : "false") + ",";
  json += "\"relays\":[";
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    if (i > 0) json += ",";
    unsigned long secondsAgo = lastSwitchedMillis[i] == 0 ? 0 : (millis() - lastSwitchedMillis[i]) / 1000UL;
    json += "{";
    json += "\"channel\":" + String(i + 1) + ",";
    json += "\"name\":\"" + jsonEscape(deviceName[i]) + "\",";
    json += "\"state\":" + String(relayState[i] ? "true" : "false") + ",";
    json += "\"everSwitched\":" + String(lastSwitchedMillis[i] == 0 ? "false" : "true") + ",";
    json += "\"secondsAgo\":" + String(secondsAgo);
    json += "}";
  }
  json += "]";
  json += "}";

  server.send(200, "application/json", json);
}

// ---- GET /relay?ch=1&state=1 ----
void handleRelay() {
  if (!isAuthenticated()) { rejectUnauthorized(); return; }

  if (!server.hasArg("ch") || !server.hasArg("state")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing ch or state parameter\"}");
    return;
  }

  int ch = server.arg("ch").toInt();
  int state = server.arg("state").toInt();

  if (ch < 1 || ch > RELAY_COUNT || (state != 0 && state != 1)) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid channel or state\"}");
    return;
  }

  uint8_t idx = ch - 1;
  setRelay(idx, state == 1);

  String message = deviceName[idx] + (state == 1 ? " Activated" : " Deactivated");
  String json = "{\"success\":true,\"channel\":" + String(ch) +
                ",\"state\":" + String(state == 1 ? "true" : "false") +
                ",\"message\":\"" + jsonEscape(message) + "\"}";
  server.send(200, "application/json", json);
}

// ---- GET /scene?name=movie ----
void handleScene() {
  if (!isAuthenticated()) { rejectUnauthorized(); return; }

  if (!server.hasArg("name")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing scene name\"}");
    return;
  }

  String scene = server.arg("name");
  String message = applyScene(scene);

  if (message.length() == 0) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Unknown scene\"}");
    return;
  }

  String json = "{\"success\":true,\"message\":\"" + jsonEscape(message) +
                "\",\"vacationMode\":" + String(vacationMode ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

// ---- POST /rename  (ch=1&name=Living+Room+Light) ----
void handleRename() {
  if (!isAuthenticated()) { rejectUnauthorized(); return; }

  if (!server.hasArg("ch") || !server.hasArg("name")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing ch or name\"}");
    return;
  }

  int ch = server.arg("ch").toInt();
  String name = server.arg("name");
  name.trim();

  if (ch < 1 || ch > RELAY_COUNT || name.length() == 0 || name.length() > 24) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid channel or name length\"}");
    return;
  }

  uint8_t idx = ch - 1;
  deviceName[idx] = name;

  prefs.begin("jarvis", false);
  switch (idx) {
    case 0: prefs.putString("n1", name); break;
    case 1: prefs.putString("n2", name); break;
    case 2: prefs.putString("n3", name); break;
    case 3: prefs.putString("n4", name); break;
    case 4: prefs.putString("n5", name); break;
    case 5: prefs.putString("n6", name); break;
  }
  prefs.end();

  server.send(200, "application/json", "{\"success\":true,\"message\":\"Device renamed\"}");
}

// ---- POST /wifi  (ssid=...&password=...) ----
// Saves new AP credentials to flash. NOT applied live (changing the
// SoftAP config mid-request would drop the very connection handling
// this request) — applied on the next restart instead.
void handleWifiChange() {
  if (!isAuthenticated()) { rejectUnauthorized(); return; }

  String newSsid = server.arg("ssid");
  String newPass = server.arg("password");
  newSsid.trim();

  if (newSsid.length() < 1 || newSsid.length() > 31) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"SSID must be 1-31 characters\"}");
    return;
  }
  if (newPass.length() > 0 && newPass.length() < 8) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Password must be 8+ characters (or blank for open network)\"}");
    return;
  }

  prefs.begin("jarvis", false);
  prefs.putString("ap_ssid", newSsid);
  prefs.putString("ap_pass", newPass);
  prefs.end();

  server.send(200, "application/json", "{\"success\":true,\"message\":\"WiFi credentials saved. Restart to apply.\"}");
}

// ---- GET /restart ----
void handleRestart() {
  if (!isAuthenticated()) { rejectUnauthorized(); return; }

  server.send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting...\"}");
  Serial.println(F("[SYSTEM] Restart requested."));
  delay(500); // let the response flush before the reboot tears down WiFi
  ESP.restart();
}

// ---- GET /factoryreset ----
void handleFactoryReset() {
  if (!isAuthenticated()) { rejectUnauthorized(); return; }

  server.send(200, "application/json", "{\"success\":true,\"message\":\"Factory reset. Rebooting...\"}");
  Serial.println(F("[SYSTEM] Factory reset requested."));

  prefs.begin("jarvis", false);
  prefs.clear();
  prefs.end();

  delay(500);
  ESP.restart();
}

// ---- 404 ----
void handleNotFound() {
  server.send(404, "application/json", "{\"success\":false,\"message\":\"Route not found\"}");
}
