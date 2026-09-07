#pragma once

/*
  Shared chrome for the three screens.  These are #defines, not const char[],
  so the preprocessor pastes them INSIDE each page's raw string literal and
  adjacent-literal concatenation folds the whole page into one PROGMEM array
  at compile time.  One copy of the CSS in the source, three in flash --
  flash is 16 MB and we are using well under 1 % of it.

  Everything here lives in a .h for the same reason page.h does: Arduino
  scans .ino files for things that look like function definitions and does
  not understand raw string literals.  See DESIGN_NOTES.md.
*/

// ------------------------------------------------------------------ palette
#define CSS_BASE R"CSS(
:root{
  --bg:#eef1f4; --card:#fff; --ink:#17212e; --mut:#6b7785; --line:#e3e7eb;
  --acc:#1f6f8b; --acc2:#17576d; --bad:#b4531a; --ok:#1d7a4c; --warn:#c9821a;
}
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:0;
  background:var(--bg);color:var(--ink);-webkit-text-size-adjust:100%}
nav{background:var(--acc2);color:#fff;display:flex;align-items:center;
  gap:2px;padding:0 10px;position:sticky;top:0;z-index:9;flex-wrap:wrap}
nav .brand{font-weight:700;font-size:15px;padding:11px 8px 11px 2px;letter-spacing:.02em}
nav a{color:#cfe3ea;text-decoration:none;font-size:14px;padding:11px 13px;
  border-bottom:3px solid transparent}
nav a:hover{color:#fff}
nav a.on{color:#fff;border-bottom-color:#7fd0e8;font-weight:600}
nav .ver{margin-left:auto;font-size:11px;opacity:.7;padding:11px 4px;font-variant-numeric:tabular-nums}
.sim{background:var(--bad);color:#fff;padding:8px 14px;font-weight:700;
  font-size:12px;letter-spacing:.05em;text-align:center}
main{padding:12px;max-width:640px;margin:auto}
section{background:var(--card);border-radius:10px;padding:13px 14px;margin-bottom:11px;
  box-shadow:0 1px 2px rgba(23,33,46,.06)}
h2{font-size:11px;margin:0 0 9px;color:var(--mut);text-transform:uppercase;
  letter-spacing:.09em;font-weight:700}
button{padding:10px 15px;border:0;border-radius:7px;background:var(--acc);color:#fff;
  font-size:14px;font-weight:600;cursor:pointer;font-family:inherit}
button:active{transform:translateY(1px)}
button.red{background:var(--bad)}
button.grey{background:#7a8592}
button.ghost{background:#eaeef1;color:var(--ink)}
input,select{font-family:inherit;font-size:15px;padding:7px 8px;border:1px solid #c4ccd4;
  border-radius:6px;background:#fff;color:var(--ink);width:100%}
input:focus,select:focus{outline:2px solid var(--acc);outline-offset:-1px;border-color:var(--acc)}
.row{display:flex;gap:7px;flex-wrap:wrap}
.mut{color:var(--mut)}
.ok{color:var(--ok)}.no{color:var(--bad)}.warn{color:var(--warn);font-weight:700}
.pill{display:inline-block;padding:3px 10px;border-radius:99px;font-size:12px;
  font-weight:700;letter-spacing:.03em;background:#e7edf1;color:var(--acc2)}
.pill.run{background:#dcefe3;color:var(--ok)}
.pill.slp{background:#e6e9ed;color:var(--mut)}
.pill.bad{background:#f8e2d9;color:var(--bad)}
.note{font-size:12px;color:var(--mut);line-height:1.5;margin:6px 0 0}
pre{background:#f2f4f6;border-radius:6px;padding:9px;font-size:12px;
  white-space:pre-wrap;margin:9px 0 0;color:#33404f}
)CSS"

// The version and active tab are filled in by JS so the same markup serves
// every page and nothing has to be templated on the device.
#define NAV_HTML R"NAV(
<nav>
<span class="brand">PumpSaver</span>
<a href="/">Easy</a><a href="/adv">Advanced</a><a href="/wifi">Wi-Fi</a>
<span class="ver" id="ver"></span>
</nav>
<div class="sim" id="simbar" style="display:none">SIMULATION &mdash; NOT CONTROLLING REAL HARDWARE</div>
)NAV"

#define NAV_JS R"NAVJS(
for(const a of document.querySelectorAll('nav a'))
  if(a.getAttribute('href')==location.pathname)a.className='on';
)NAVJS"
