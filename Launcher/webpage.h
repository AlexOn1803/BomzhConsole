#pragma once
// Сторінка, яку консоль віддає телефону по Wi-Fi (http://192.168.4.1).
static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="uk"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Кишенькова консоль</title>
<style>
:root{--bg:#0e1218;--card:#161c25;--line:#253041;--txt:#e8eaee;--sub:#9aa5b5;--amber:#ffb21a;--red:#ff4d4f;--green:#4cd964}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--txt);font:15px/1.45 system-ui,-apple-system,Roboto,sans-serif;padding:16px;max-width:560px;margin:auto}
h1{font-size:22px;margin:4px 0 2px}h1 b{color:var(--amber)}h2{font-size:13px;letter-spacing:.08em;text-transform:uppercase;color:var(--sub);margin:22px 0 8px}
p.sub{color:var(--sub);margin:0 0 8px;font-size:13px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px}
.row{display:flex;align-items:center;gap:10px;padding:9px 0;border-bottom:1px solid var(--line)}.row:last-child{border:0}
.row .n{flex:1;min-width:0;overflow-wrap:anywhere}.row .s{color:var(--sub);font-size:12px;font-variant-numeric:tabular-nums}
button,.btn{font:inherit;font-size:13px;border:1px solid var(--line);background:#1d2530;color:var(--txt);border-radius:8px;padding:6px 10px;cursor:pointer;text-decoration:none}
button.pri{background:var(--amber);color:#1a1200;border-color:var(--amber);font-weight:600}
button.del{color:var(--red)}
.drop{display:block;cursor:pointer;border:2px dashed var(--line);border-radius:10px;padding:18px;text-align:center;color:var(--sub)}
.drop.on{border-color:var(--amber);color:var(--txt)}
progress{width:100%;height:10px;accent-color:var(--amber)}
#msg{min-height:20px;font-size:13px;margin-top:8px}.ok{color:var(--green)}.err{color:var(--red)}
.empty{color:var(--sub);font-size:13px;padding:6px 0}
</style></head><body>
<h1>Кишенькова <b>консоль</b></h1>
<p class="sub" id="sd">…</p>

<h2>Завантажити</h2>
<div class="card">
  <label class="drop" id="drop" for="f">Натисни, щоб вибрати файли гри (.bin) — або перетягни сюди.<br>
  Назву, опис та іконку дає файл з такою ж назвою .txt (рядок 1 — назва, 2 — опис, далі іконка).</label>
  <input type="file" id="f" multiple accept=".bin,.txt" hidden>
  <div id="prog" hidden><progress id="pb" max="100" value="0"></progress></div>
  <div id="msg"></div>
</div>

<h2>Ігри на карті</h2>
<div class="card" id="games"><div class="empty">Завантажую список…</div></div>

<h2>Збереження</h2>
<div class="card" id="saves"><div class="empty">…</div></div>

<h2>Прошивка консолі</h2>
<div class="card">
  <p class="sub" id="fwv">…</p>
  <label class="drop" for="fw">Натисни, щоб вибрати нову прошивку лаунчера — файл <b>Launcher.ino.bin</b> (не merged).</label>
  <input type="file" id="fw" accept=".bin" hidden>
  <div id="fprog" hidden><progress id="fpb" max="100" value="0"></progress></div>
  <div id="fmsg"></div>
</div>

<script>
const $=s=>document.querySelector(s);
const kb=n=>n>=1e9?(n/1e9).toFixed(1)+' ГБ':n>=1e6?(n/1e6).toFixed(1)+' МБ':Math.ceil(n/1e3)+' КБ';
function say(t,c){$('#msg').textContent=t;$('#msg').className=c||''}
async function load(){
  try{
    const r=await fetch('/api/list');const d=await r.json();
    $('#sd').textContent=`SD-карта: вільно ${kb(d.total-d.used)} з ${kb(d.total)}`;
    $('#fwv').textContent=`Зараз стоїть прошивка від ${d.fw}.`+(d.fwPending?' Нова вже на карті — натисни OK на консолі.':'');
    rows('#games',d.games,'games','Поки що жодної гри.');
    rows('#saves',d.saves,'saves','Збережень ще немає.');
  }catch(e){$('#sd').textContent='Немає зв\'язку з консоллю.'}
}
function rows(sel,list,dir,empty){
  const el=$(sel);el.innerHTML='';
  if(!list.length){el.innerHTML=`<div class="empty">${empty}</div>`;return}
  for(const f of list){
    const r=document.createElement('div');r.className='row';
    r.innerHTML=`<span class="n"></span><span class="s">${kb(f.size)}</span>
      <a class="btn" href="/dl?path=/${dir}/${encodeURIComponent(f.name)}">Скачати</a>
      <button class="del">Видалити</button>`;
    r.querySelector('.n').textContent=f.name;
    r.querySelector('.del').onclick=async()=>{
      if(!confirm('Видалити '+f.name+'?'))return;
      await fetch('/delete?path=/'+dir+'/'+encodeURIComponent(f.name),{method:'POST'});load();
    };
    el.appendChild(r);
  }
}
function upOne(file){
  return new Promise((ok,bad)=>{
    const fd=new FormData();fd.append('file',file,file.name);
    const x=new XMLHttpRequest();x.open('POST','/upload?dir=games');
    x.upload.onprogress=e=>{if(e.lengthComputable)$('#pb').value=e.loaded/e.total*100};
    x.onload=()=>x.status==200?ok():bad(x.responseText||('помилка '+x.status));
    x.onerror=()=>bad('зв\'язок обірвався');x.send(fd);
  });
}
async function upload(files){
  files=[...files].filter(f=>/\.(bin|txt)$/i.test(f.name));
  if(!files.length){say('Вибери файли .bin або .txt','err');return}
  $('#prog').hidden=false;
  for(const [i,f] of files.entries()){
    say(`Надсилаю ${f.name} (${i+1}/${files.length})…`);$('#pb').value=0;
    try{await upOne(f)}catch(e){say(f.name+': '+e,'err');$('#prog').hidden=true;load();return}
  }
  $('#prog').hidden=true;say('Готово! Гра з\'явиться в меню консолі.','ok');load();
}
$('#f').onchange=e=>upload(e.target.files);
$('#fw').onchange=e=>{
  const file=e.target.files[0]; if(!file)return;
  const m=$('#fmsg'); m.className=''; m.textContent='Надсилаю '+file.name+'…'; $('#fprog').hidden=false;
  const fd=new FormData();fd.append('file',file,file.name);
  const x=new XMLHttpRequest();x.open('POST','/upload?dir=firmware');
  x.upload.onprogress=ev=>{if(ev.lengthComputable)$('#fpb').value=ev.loaded/ev.total*100};
  x.onload=()=>{$('#fprog').hidden=true;
    if(x.status==200){m.className='ok';m.textContent='Прошивку отримано й перевірено. Натисни OK на консолі, щоб оновити (близько 20 с, не вимикай).'}
    else{m.className='err';m.textContent=x.responseText||('помилка '+x.status)}
    load();e.target.value=''};
  x.onerror=()=>{$('#fprog').hidden=true;m.className='err';m.textContent='Зв\'язок обірвався'};
  x.send(fd);
};
const d=$('#drop');
['dragenter','dragover'].forEach(t=>d.addEventListener(t,e=>{e.preventDefault();d.classList.add('on')}));
['dragleave','drop'].forEach(t=>d.addEventListener(t,e=>{e.preventDefault();d.classList.remove('on')}));
d.addEventListener('drop',e=>upload(e.dataTransfer.files));
load();
</script></body></html>)HTML";
