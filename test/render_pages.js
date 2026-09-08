// Assemble the PROGMEM pages the way the C preprocessor would, then check each
// one: tag balance, JS syntax, and that every getElementById target exists.
//
// Catches in seconds what would otherwise cost a 30 s build-and-flash cycle and
// a page that silently does nothing in a browser.  Pair it with id_collide.js:
//     node test/render_pages.js && node test/id_collide.js
//
// Bench tool -- it emulates the preprocessor, it is not the preprocessor. A
// clean run here is not a substitute for .\build.ps1.
const fs = require('fs'), path = require('path'), vm = require('vm'), os = require('os');
const ROOT = process.argv[2] || path.join(__dirname, '..');
const OUT  = process.argv[3] || path.join(os.tmpdir(), 'pumpsaver-pages');
fs.mkdirSync(OUT, { recursive: true });

const rawRe = /R"([A-Za-z0-9_]*)\(([\s\S]*?)\)\1"/g;

// macros from ui_common.h:  #define NAME R"D(...)D"
function macros() {
  const src = fs.readFileSync(path.join(ROOT, 'ui_common.h'), 'utf8');
  const m = {};
  const re = /#define\s+([A-Z_0-9]+)\s+R"([A-Za-z0-9_]*)\(([\s\S]*?)\)\2"/g;
  let x; while ((x = re.exec(src))) m[x[1]] = x[3];
  return m;
}

// const char NAME[] PROGMEM = <raw literal | MACRO>+ ;
function page(file, M) {
  const src = fs.readFileSync(path.join(ROOT, file), 'utf8');
  const start = src.indexOf('PROGMEM = ');
  if (start < 0) throw new Error('no PROGMEM literal in ' + file);
  const body = src.slice(start + 'PROGMEM = '.length);
  let out = '', i = 0, guard = 0;
  while (i < body.length && guard++ < 500) {
    const rest = body.slice(i);
    if (/^\s*;/.test(rest)) break;
    let m = /^\s*R"([A-Za-z0-9_]*)\(/.exec(rest);
    if (m) {
      const close = ')' + m[1] + '"';
      const end = body.indexOf(close, i + m[0].length);
      if (end < 0) throw new Error('unterminated raw string in ' + file);
      out += body.slice(i + m[0].length, end);
      i = end + close.length;
      continue;
    }
    m = /^\s*([A-Z_0-9]+)/.exec(rest);
    if (m) {
      if (!(m[1] in M)) throw new Error('unknown macro ' + m[1] + ' in ' + file);
      out += M[m[1]];
      i += m[0].length;
      continue;
    }
    throw new Error('unparsed at ' + JSON.stringify(rest.slice(0, 40)));
  }
  return out;
}

const M = macros();
console.log('macros:', Object.keys(M).join(', '));

let bad = 0;
for (const [file, name] of [['page_home.h','home'],['page_pump.h','pump'],['page_cal.h','cal'],['page_sim.h','sim'],['page_net.h','net'],['page_system.h','system']]) {
  const html = page(file, M);
  fs.writeFileSync(path.join(OUT, name + '.html'), html);

  // structural sanity
  const problems = [];
  for (const t of ['html','head','body','style','script','main'])
    if ((html.match(new RegExp('<' + t + '[ >]', 'g')) || []).length !==
        (html.match(new RegExp('</' + t + '>', 'g')) || []).length)
      problems.push('unbalanced <' + t + '>');
  if (html.includes(')HTML"')) problems.push('leftover raw-string delimiter');

  // every id referenced bare in JS must exist in the markup
  const ids = new Set([...html.matchAll(/\bid="([A-Za-z0-9_]+)"/g)].map(m => m[1]));

  // javascript
  const js = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]).join('\n');
  try { new vm.Script(js, { filename: name + '.js' }); }
  catch (e) { problems.push('JS syntax: ' + e.message); }

  // getElementById targets
  for (const m of js.matchAll(/getElementById\('([^']+)'\)/g))
    if (!ids.has(m[1])) problems.push('getElementById("' + m[1] + '") has no such id');

  console.log('\n' + name.padEnd(5), (html.length / 1024).toFixed(1) + ' KB',
              ' ids:' + ids.size, ' js:' + (js.length / 1024).toFixed(1) + ' KB');
  if (problems.length) { bad++; problems.forEach(p => console.log('   !! ' + p)); }
  else console.log('   ok');
}
process.exit(bad ? 1 : 0);
