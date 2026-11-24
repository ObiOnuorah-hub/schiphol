// painted line chains (centre of every 0.3 m strip), in metres
const L=require('./lib.js');
function build(){
 const T=L.loadPaint().filter(t=>t[6]===0);
 const S=L.centreSegments(T).filter(s=>s[4]<0.6&&Math.hypot(s[0]-s[2],s[1]-s[3])>0.01);
 // weld
 const nodes=[],map=new Map();const q=0.06;
 const id=(x,y)=>{const i=Math.round(x/q),j=Math.round(y/q);for(let a=-1;a<=1;a++)for(let b=-1;b<=1;b++){const v=map.get((i+a)+','+(j+b));if(v!==undefined&&Math.hypot(nodes[v][0]-x,nodes[v][1]-y)<q)return v;}const n=nodes.length;nodes.push([x,y]);map.set(i+','+j,n);return n;};
 const adj=[];const add=(a,b)=>{if(a===b)return;(adj[a]=adj[a]||new Set()).add(b);(adj[b]=adj[b]||new Set()).add(a);};
 for(const s of S)add(id(s[0],s[1]),id(s[2],s[3]));
 for(let i=0;i<nodes.length;i++)adj[i]=adj[i]||new Set();
 // trace: through degree-2 nodes, and through junction nodes straight on
 const used=new Set();const ek=(a,b)=>a<b?a*1e6+b:b*1e6+a;const chains=[];
 const dir=(a,b)=>{const dx=nodes[b][0]-nodes[a][0],dy=nodes[b][1]-nodes[a][1],l=Math.hypot(dx,dy)||1;return [dx/l,dy/l];};
 function extend(list){for(;;){const n=list.length;const b=list[n-1],a=list[n-2];const d=dir(a,b);let best=-1,bc=0.7;for(const c of adj[b]){if(c===a||used.has(ek(b,c)))continue;const e=dir(b,c);const k=d[0]*e[0]+d[1]*e[1];if(k>bc){bc=k;best=c;}}if(best<0)return;used.add(ek(b,best));list.push(best);if(list.length>200000)return;}}
 // longest edges first so that traces start on clear line pieces
 const E=[];for(let a=0;a<nodes.length;a++)for(const b of adj[a])if(a<b)E.push([a,b,Math.hypot(nodes[a][0]-nodes[b][0],nodes[a][1]-nodes[b][1])]);
 E.sort((p,q)=>q[2]-p[2]);
 for(const [a,b] of E){if(used.has(ek(a,b)))continue;used.add(ek(a,b));const f=[a,b];extend(f);const r=[b,a];extend(r);const list=r.reverse().slice(0,-2).concat(f);chains.push(list.map(i=>nodes[i]));}
 return chains.map(c=>({p:c,len:c.reduce((s,p,i)=>i?s+Math.hypot(p[0]-c[i-1][0],p[1]-c[i-1][1]):0,0)}));
}
module.exports={build};
if(require.main===module){const C=build();console.log('chains',C.length,'km',C.reduce((a,c)=>a+c.len,0)/1000);const h={};C.forEach(c=>{const k=c.len<1?'<1':c.len<3?'<3':c.len<10?'<10':c.len<30?'<30':c.len<100?'<100':c.len<500?'<500':'>500';h[k]=h[k]||[0,0];h[k][0]++;h[k][1]+=c.len;});console.log(h);
 const big=C.filter(c=>c.len>=3);let ang=0,n=0;for(const c of big){for(let i=1;i<c.p.length-1;i++){const a=c.p[i-1],b=c.p[i],d=c.p[i+1];const t=Math.abs(Math.atan2((b[0]-a[0])*(d[1]-b[1])-(b[1]-a[1])*(d[0]-b[0]),(b[0]-a[0])*(d[0]-b[0])+(b[1]-a[1])*(d[1]-b[1])));if(t>0.35)ang++;n++;}}console.log('vertices',n,'kinks>20deg',ang);}
