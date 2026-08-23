from __future__ import annotations

import json
from html import escape
from pathlib import Path
from typing import Dict


def write_preview(motion: Dict[str, object], destination: Path) -> None:
    payload = json.dumps(motion, ensure_ascii=False, separators=(",", ":"))
    title = escape(str(motion["source_prompt"]))
    html = f"""<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><link rel="icon" href="data:,">
<title>Sesame 动作模拟：{title}</title>
<style>
body{{margin:0;background:#0b1018;color:#edf3ff;font:15px system-ui,sans-serif}}
main{{max-width:1050px;margin:auto;padding:24px}} h1{{font-size:24px;margin:0 0 6px}}
.muted{{color:#91a2ba}} .grid{{display:grid;grid-template-columns:minmax(480px,2fr) minmax(300px,1fr);gap:18px;margin-top:18px}}
.card{{background:#131c28;border:1px solid #26364b;border-radius:14px;padding:16px}}
canvas{{width:100%;height:auto;background:radial-gradient(circle,#172638,#0d151f);border-radius:10px}}
button{{background:#3b82f6;color:white;border:0;border-radius:8px;padding:9px 15px;font-weight:700}}
input{{width:100%}} table{{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}}
td{{padding:6px 3px;border-bottom:1px solid #26364b}} td:last-child{{text-align:right;color:#71d3ff}}
.pill{{display:inline-block;background:#1d3348;color:#80d7ff;padding:4px 8px;border-radius:999px;margin-right:5px}}
@media(max-width:800px){{.grid{{grid-template-columns:1fr}}}}
</style></head><body><main>
<h1>自然语言 → 8 舵机：{title}</h1>
<div class="muted">动作基元：dog_paddle（狗刨式划水） · 蓝色为机身 · 线段为四条两关节腿</div>
<div class="grid"><section class="card"><canvas id="robot" width="680" height="500"></canvas>
<p><button id="toggle">暂停</button> <span id="clock" class="pill"></span><span id="stage" class="pill"></span></p>
<input id="timeline" type="range" min="0" max="{len(motion['frames']) - 1}" value="0"></section>
<aside class="card"><h2 style="margin-top:0">实时角度</h2><table id="angles"></table>
<p class="muted">这是运动学草图，不含重力、碰撞、扭矩和结构干涉。实体执行前需替换每个舵机的实测限位、零位和方向。</p></aside></div>
<section class="card" style="margin-top:18px"><b>模型判断：</b>{escape(str(motion['intent']['rationale']))}</section>
</main><script>
const motion={payload}; const canvas=document.querySelector('#robot'); const ctx=canvas.getContext('2d');
const slider=document.querySelector('#timeline'); let index=0,playing=true,last=performance.now();
const order=motion.servo_order, idx=Object.fromEntries(order.map((n,i)=>[n,i]));
const legs=[
  {{hip:'L1',knee:'L3',anchor:[270,170],base:Math.PI,name:'左前'}},
  {{hip:'R1',knee:'R3',anchor:[410,170],base:0,name:'右前'}},
  {{hip:'L2',knee:'L4',anchor:[270,330],base:Math.PI,name:'左后'}},
  {{hip:'R2',knee:'R4',anchor:[410,330],base:0,name:'右后'}}];
function limb(leg,a){{
  const hipRaw=a[idx[leg.hip]], kneeRaw=a[idx[leg.knee]];
  const side=leg.base===0?1:-1;
  const q1=leg.base+side*(hipRaw-90)*Math.PI/360;
  const q2=q1+side*(kneeRaw-90)*Math.PI/270;
  const p1=[leg.anchor[0]+72*Math.cos(q1),leg.anchor[1]+72*Math.sin(q1)];
  const p2=[p1[0]+62*Math.cos(q2),p1[1]+62*Math.sin(q2)];
  ctx.strokeStyle='#75d6ff';ctx.lineWidth=12;ctx.lineCap='round';ctx.beginPath();ctx.moveTo(...leg.anchor);ctx.lineTo(...p1);ctx.lineTo(...p2);ctx.stroke();
  ctx.fillStyle='#fbbf24';for(const p of [leg.anchor,p1,p2]){{ctx.beginPath();ctx.arc(...p,7,0,7);ctx.fill();}}
  ctx.fillStyle='#9fb1c8';ctx.fillText(leg.name,p2[0]-14,p2[1]-12);
}}
function draw(){{
  const f=motion.frames[index],a=f.angles;ctx.clearRect(0,0,canvas.width,canvas.height);
  ctx.fillStyle='#263e5a';ctx.strokeStyle='#75d6ff';ctx.lineWidth=3;ctx.beginPath();ctx.roundRect(270,130,140,240,28);ctx.fill();ctx.stroke();
  ctx.fillStyle='#edf3ff';ctx.font='bold 18px system-ui';ctx.textAlign='center';ctx.fillText('前',340,155);ctx.fillText('SESAME V3',340,255);ctx.textAlign='left';ctx.font='13px system-ui';
  legs.forEach(l=>limb(l,a));
  document.querySelector('#clock').textContent=`${{f.t_ms}} ms`;document.querySelector('#stage').textContent=f.stage;
  document.querySelector('#angles').innerHTML=order.map((n,i)=>`<tr><td>${{i}} · ${{n}}</td><td>${{a[i]}}°</td></tr>`).join('');slider.value=index;
}}
function tick(now){{if(playing&&now-last>=motion.frame_ms){{index=(index+1)%motion.frames.length;last=now;draw()}}requestAnimationFrame(tick)}}
document.querySelector('#toggle').onclick=e=>{{playing=!playing;e.target.textContent=playing?'暂停':'播放'}};
slider.oninput=e=>{{index=Number(e.target.value);playing=false;document.querySelector('#toggle').textContent='播放';draw()}};
draw();requestAnimationFrame(tick);
</script></body></html>"""
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(html, encoding="utf-8")
