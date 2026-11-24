const A=require('./audit.js');const {R,nearest,onRunway}=A;const Img=require('./png.js');const L=A.L;
const cos=Math.cos(25*Math.PI/180);
const cells=new Map();const cs=4;
for(const r of R){const n=r.pts.length;const push=/Pushback/.test(r.tag);if(push)continue;
  for(let i=0;i<n;i++){const a=r.pts[Math.max(0,i-1)],b=r.pts[Math.min(n-1,i+1)];const l=Math.hypot(b[0]-a[0],b[1]-a[1])||1;const p=r.pts[i];
    if(onRunway(p[0],p[1],0))continue;
    const o=nearest(p[0],p[1],(b[0]-a[0])/l,(b[1]-a[1])/l,12,cos)[0];
    if(o<=0.8)continue;
    const k=Math.floor(p[0]/cs)+','+Math.floor(p[1]/cs);let c=cells.get(k);if(!c){c={x:0,y:0,n:0,max:0,none:0,tags:new Set(),k};cells.set(k,c);}
    c.x+=p[0];c.y+=p[1];c.n++;if(o<100)c.max=Math.max(c.max,o);else c.none++;c.tags.add(r.tag);}}
// components
const seen=new Set();const comps=[];
for(const [k,c] of cells){if(seen.has(k))continue;const st=[k];seen.add(k);const comp={cells:[],tags:new Set(),n:0,max:0,none:0,x0:1e9,y0:1e9,x1:-1e9,y1:-1e9};
  while(st.length){const q=st.pop();const d=cells.get(q);comp.cells.push(d);d.tags.forEach(t=>comp.tags.add(t));comp.n+=d.n;comp.max=Math.max(comp.max,d.max);comp.none+=d.none;const x=d.x/d.n,y=d.y/d.n;comp.x0=Math.min(comp.x0,x);comp.x1=Math.max(comp.x1,x);comp.y0=Math.min(comp.y0,y);comp.y1=Math.max(comp.y1,y);
    const [i,j]=q.split(',').map(Number);for(let a=-2;a<=2;a++)for(let b=-2;b<=2;b++){const z=(i+a)+','+(j+b);if(cells.has(z)&&!seen.has(z)){seen.add(z);st.push(z);}}}
  comps.push(comp);}
comps.sort((a,b)=>b.n-a.n);
console.log('components',comps.length,'cells',cells.size,'unique metres ~',cells.size*cs);
comps.slice(0,70).forEach((c,i)=>console.log(i,`at ${((c.x0+c.x1)/2).toFixed(0)},${((c.y0+c.y1)/2).toFixed(0)} size ${(c.x1-c.x0).toFixed(0)}x${(c.y1-c.y0).toFixed(0)} cells ${c.cells.length} samples ${c.n} none ${c.none} max ${c.max.toFixed(1)} routes ${c.tags.size} ${[...c.tags].slice(0,3).map(t=>t.replace('Route.EHAM.','')).join(' ')}`));
module.exports={comps};
