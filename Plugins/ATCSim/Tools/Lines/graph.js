const L=require('./lib.js');const PV=require('./pave.js');const CH=require('./chains.js');
const STEP=1.0;
// resample a polyline every STEP m
function resample(p,step=STEP){const out=[[p[0][0],p[0][1]]];let acc=0;for(let i=1;i<p.length;i++){const a=p[i-1],b=p[i];const l=Math.hypot(b[0]-a[0],b[1]-a[1]);let t=step-acc;while(t<=l){out.push([a[0]+(b[0]-a[0])*t/l,a[1]+(b[1]-a[1])*t/l]);t+=step;}acc=l-(t-step);}
 const e=p[p.length-1],o=out[out.length-1];if(Math.hypot(e[0]-o[0],e[1]-o[1])>0.3*step)out.push([e[0],e[1]]);else if(out.length>1)out[out.length-1]=[e[0],e[1]];return out;}
function loadOsm(){const R0=6378137,la0=52.3075170,lo0=4.7624393;const j=JSON.parse(L.fs.readFileSync(__dirname+'/../../Data/OSM/EHAM.json','utf8'));const o=[];for(const e of j.elements||[]){const k=e.tags&&e.tags.aeroway;if(!e.geometry||!(k==='taxiway'||k==='taxilane'||k==='parking_position')||(e.tags.area==='yes'))continue;o.push([k,e.geometry.map(g=>[(g.lon-lo0)*Math.PI/180*R0*Math.cos(la0*Math.PI/180),-(g.lat-la0)*Math.PI/180*R0]),e.tags.ref]);}return o;}
function build(){
 const chains=CH.build().filter(c=>c.len>=3.0).map((c,i)=>{const p=resample(c.p);const t=p.map((q,k)=>{const a=p[Math.max(0,k-2)],b=p[Math.min(p.length-1,k+2)];const l=Math.hypot(b[0]-a[0],b[1]-a[1])||1;return [(b[0]-a[0])/l,(b[1]-a[1])/l];});return {id:i,p,t,len:c.len};});
 // taxiways without any paint: the OSM centreline
 {const cg=new L.Grid(10);const cs=[];for(const c of chains)for(let i=1;i<c.p.length;i+=2){const s=[c.p[i-1][0],c.p[i-1][1],c.p[i][0],c.p[i][1]];const l=Math.hypot(s[2]-s[0],s[3]-s[1]);if(l<0.1)continue;s.d=[(s[2]-s[0])/l,(s[3]-s[1])/l];cg.addSeg(s,cs.length);cs.push(s);}
  for(const [k,ways] of loadOsm()){if(k==='parking_position')continue;const p=resample(ways);let run=[];const flush=()=>{if(run.length>=20){const q=run;const t=q.map((z,i)=>{const a=q[Math.max(0,i-2)],b=q[Math.min(q.length-1,i+2)];const l=Math.hypot(b[0]-a[0],b[1]-a[1])||1;return [(b[0]-a[0])/l,(b[1]-a[1])/l];});chains.push({id:chains.length,p:q,t,len:q.length-1,virtual:true});}run=[];};
   for(let i=0;i<p.length;i++){const a=p[Math.max(0,i-2)],b=p[Math.min(p.length-1,i+2)];const l=Math.hypot(b[0]-a[0],b[1]-a[1])||1;const dx=(b[0]-a[0])/l,dy=(b[1]-a[1])/l;let hit=false;cg.query(p[i][0],p[i][1],22,j=>{if(hit)return;const s=cs[j];if(Math.abs(s.d[0]*dx+s.d[1]*dy)<0.85)return;if(L.segDist(p[i][0],p[i][1],s)[0]<22)hit=true;});if(!hit&&PV.paved(p[i][0],p[i][1]))run.push(p[i]);else flush();}
   flush();}}
 // nodes
 const N=[];for(const c of chains){c.first=N.length;c.p.forEach((q,k)=>N.push({x:q[0],y:q[1],tx:c.t[k][0],ty:c.t[k][1],c:c.id,k}));}
 const cell=4;const grid=new Map();N.forEach((n,i)=>{const k=Math.floor(n.x/cell)*100003+Math.floor(n.y/cell);let a=grid.get(k);if(!a)grid.set(k,a=[]);a.push(i);});
 const near=(x,y,r,f)=>{const i0=Math.floor((x-r)/cell),i1=Math.floor((x+r)/cell),j0=Math.floor((y-r)/cell),j1=Math.floor((y+r)/cell);for(let i=i0;i<=i1;i++)for(let j=j0;j<=j1;j++){const a=grid.get(i*100003+j);if(a)for(const v of a){const d=Math.hypot(N[v].x-x,N[v].y-y);if(d<=r)f(v,d);}}};
 return {chains,N,near};
}
// evidence per chain
function classify(G,routes){
 const {chains,N,near}=G;
 const osm=loadOsm();const og=new L.Grid(10);const os=[];for(const [k,g] of osm)for(let i=1;i<g.length;i++){const s=[g[i-1][0],g[i-1][1],g[i][0],g[i][1]];const l=Math.hypot(s[2]-s[0],s[3]-s[1]);if(l<0.1)continue;s.d=[(s[2]-s[0])/l,(s[3]-s[1])/l];s.k=k;og.addSeg(s,os.length);os.push(s);}
 const used=new Uint8Array(N.length);
 for(const r of routes){if(/Pushback/.test(r.tag))continue;const n=r.pts.length;for(let i=1;i<n-1;i++){const p=r.pts[i],a=r.pts[i-1],b=r.pts[i+1];const l=Math.hypot(b[0]-a[0],b[1]-a[1])||1;const dx=(b[0]-a[0])/l,dy=(b[1]-a[1])/l;near(p[0],p[1],1.2,(v,d)=>{if(d<1.2&&Math.abs(N[v].tx*dx+N[v].ty*dy)>0.95)used[v]=1;});}}
 for(const c of chains){let u=0,o=0,stand=0,e=0;const m=c.p.length;
  for(let k=0;k<m;k++){const n=N[c.first+k];if(used[c.first+k])u++;
   let best=99,bk='';og.query(n.x,n.y,9,i=>{const s=os[i];if(Math.abs(s.d[0]*n.tx+s.d[1]*n.ty)<0.82)return;const d=L.segDist(n.x,n.y,s)[0];if(d<best){best=d;bk=s.k;}});
   if(best<7.5){o++;if(bk==='parking_position')stand++;}
   // grass on one side within 9.5 m
   let l=0,r=0;for(const q of [3,6,9]){if(!PV.paved(n.x-n.ty*q,n.y+n.tx*q))l=1;if(!PV.paved(n.x+n.ty*q,n.y-n.tx*q))r=1;}if(l!==r)e++;}
  c.used=u/m;c.osm=o/m;c.stand=stand/m;c.edge=e/m;}
 G.used=used;
}
module.exports={build,classify,resample,loadOsm};
if(require.main===module){
 const G=build();const R=L.loadRoutes();classify(G,R);
 const Img=require('./png.js');
 console.log('chains',G.chains.length,'nodes',G.N.length);
 const t={};for(const c of G.chains){const k=(c.used>0.3?'used':c.used>0?'part':'no')+(c.osm>0.5?' osm':'')+(c.edge>0.4?' edge':'')+(c.stand>0.5?' stand':'');t[k]=t[k]||[0,0];t[k][0]++;t[k][1]+=c.len;}
 console.log(t);
 const box=process.argv.slice(2,5).map(Number);if(box.length===3){const im=new Img(box[0]-box[2],box[1]-box[2],box[0]+box[2],box[1]+box[2],1800);for(let Y=0;Y<im.h;Y++)for(let X=0;X<im.w;X++){const p=PV.paved(im.x0+(X+0.5)/im.s,im.y0+(Y+0.5)/im.s);im.px(X,Y,p?[70,70,72]:[30,46,28]);}
  for(const c of G.chains){const col=c.used>0.05?[60,220,80]:c.osm>0.5&&c.edge<0.4?[80,160,255]:c.edge>0.4?[160,60,50]:c.osm>0.5?[200,120,255]:[230,200,40];for(let i=1;i<c.p.length;i++)im.line(c.p[i-1][0],c.p[i-1][1],c.p[i][0],c.p[i][1],col,c.len<10?0:1);im.pt(c.p[0][0],c.p[0][1],[255,255,255],2);im.pt(c.p[c.p.length-1][0],c.p[c.p.length-1][1],[255,255,255],2);}
  im.save('g.png');}
}
