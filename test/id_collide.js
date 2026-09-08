// Enforces the CLAUDE.md rule: "Never give an element an id that shadows a
// global."  A bare id is reachable as a window property, but a REAL window
// property of that name wins -- id="alert" resolves to window.alert, not the
// element.  That trap once stopped the Cmd tile updating.
//
// Run after test/render_pages.js, which writes the assembled pages:
//     node test/render_pages.js && node test/id_collide.js
//
// NOTE for anyone editing this file: build these patterns with String.raw or
// escaped backslashes.  In a plain JS string '\b' is a BACKSPACE character,
// not a word boundary -- an earlier version of this script used '\b' and so
// every pattern demanded a literal 0x08 after the id, matched nothing, and
// reported every page clean while checking precisely nothing.
const fs = require('fs'), path = require('path'), os = require('os');

const DIR = process.argv[2] || path.join(os.tmpdir(), 'pumpsaver-pages');

const WIN = new Set(Object.getOwnPropertyNames(globalThis));
// node's globalThis is not a browser Window, so add the DOM/window names that
// actually shadow named-element access.
for (const k of ('alert confirm prompt status name length top self parent frames opener closed ' +
  'location history navigator screen document open close focus blur print scroll scrollTo ' +
  'scrollBy stop origin external performance crypto caches indexedDB localStorage ' +
  'sessionStorage innerWidth innerHeight outerWidth outerHeight event menubar toolbar ' +
  'locationbar personalbar scrollbars statusbar frameElement customElements speechSynthesis ' +
  'onload onerror onmessage visualViewport devicePixelRatio screenX screenY pageXOffset ' +
  'pageYOffset matchMedia getComputedStyle postMessage fetch').split(' ')) WIN.add(k);

// Used bare (not as .prop, not inside a string): a real reference to the global.
const usedBare = (js, id) =>
  new RegExp(String.raw`(^|[^.\w$'"])` + id + String.raw`\b`).test(js);
const declaredInJs = (js, id) =>
  new RegExp(String.raw`\b(function|let|const|var)\s+` + id + String.raw`\b`).test(js);

let bad = 0, checked = 0;
for (const f of ['home', 'pump', 'sim', 'net', 'system']) {
  const file = path.join(DIR, f + '.html');
  if (!fs.existsSync(file)) {
    console.log(f.padEnd(6), 'MISSING -- run test/render_pages.js first');
    bad++;
    continue;
  }
  const html = fs.readFileSync(file, 'utf8');
  const ids = [...html.matchAll(/\bid="([A-Za-z0-9_]+)"/g)].map(m => m[1]);
  const js = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]).join('\n');

  const hits = [], notes = [];
  for (const id of new Set(ids)) {
    if (!usedBare(js, id)) continue;          // never referenced bare: harmless
    checked++;
    // A real window property beats named-element access outright. Always a bug.
    if (WIN.has(id)) hits.push(id + '  <-- shadowed by window.' + id);
    // A same-name function/let/const is only a bug if the code then expects the
    // bare name to BE the element (the Cmd-tile bug in DESIGN_NOTES). Assigning
    // it from $('id') first, as this codebase does, is fine -- so: a note.
    else if (declaredInJs(js, id))
      notes.push(id + '  also declared in JS (fine if reached via $(\'' + id + '\'))');
  }
  console.log(f.padEnd(6), hits.length ? 'PROBLEMS' : 'clean');
  hits.forEach(h => { console.log('   !! ' + h); bad++; });
  notes.forEach(n => console.log('    - ' + n));
}
console.log(`\n${checked} bare id reference(s) examined`);
if (!checked) { console.log('!! examined nothing -- the matcher is broken'); bad++; }
process.exit(bad ? 1 : 0);
