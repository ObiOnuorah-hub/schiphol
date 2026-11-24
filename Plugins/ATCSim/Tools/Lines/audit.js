const L=require('./lib.js');
const T=L.loadPaint().filter(t=>t[6]===0);const R=L.loadRoutes(process.env.ROUTES||undefined);
const S=L.centreSegments(T).filter(s=>s[4]<0.6&&Math.hypot(s[0]-s[2],s[1]-s[3])>0.02);
const G=new L.Grid(3);S.forEach((s,i)=>G.addSeg(s,i));
S.forEach(s=>{const l=Math.hypot(s[2]-s[0],s[3]-s[1]);s.dir=[(s[2]-s[0])/l,(s[3]-s[1])/l];});
// runways (map m)
const RW=[['09/27',2351,-1211,-1106,-1017,22.5],['06/24',988,345,-1926,2175,22.5],['18R/36L',-3645,-2343,-3430,-6136,30],['04/22',1435,783,2759,-731,22.5]];
function onRunway(x,y,extra){for(const r of RW){const dx=r[3]-r[1],dy=r[4]-r[2],l=Math.hypot(dx,dy);const a=((x-r[1])*dx+(y-r[2])*dy)/l,s=Math.abs((x-r[1])*-dy+(y-r[2])*dx)/l;if(a>-60&&a<l+60&&s<r[5]+(extra||0))return [r[0],s];}return null;}
function nearest(x,y,dx,dy,r,cos){let best=1e9,bi=-1;G.query(x,y,r,i=>{const s=S[i];if(Math.abs(s.dir[0]*dx+s.dir[1]*dy)<cos)return;const d=L.segDist(x,y,s)[0];if(d<best){best=d;bi=i;}});return [best<=r?best:1e9,bi];}
module.exports={L,R,S,G,nearest,onRunway,RW};
if(require.main===module){
const hist=new Array(12).fill(0);const bins=[0.15,0.3,0.5,0.75,1,1.5,2,3,5,8,12];let tot=0;const perKind={};
for(const r of R){
  const kind=r.tag.replace(/^Route\.EHAM\.[^.]+\./,'').replace(/^.*?Route\.EHAM\.[^.]+\./,'');
  const n=r.pts.length;r.off=new Float32Array(n);r.rw=new Uint8Array(n);
  for(let i=0;i<n;i++){const a=r.pts[Math.max(0,i-1)],b=r.pts[Math.min(n-1,i+1)];const l=Math.hypot(b[0]-a[0],b[1]-a[1])||1;const dx=(b[0]-a[0])/l,dy=(b[1]-a[1])/l;const p=r.pts[i];
    const rw=onRunway(p[0],p[1],0);r.rw[i]=rw?1:0;
    r.off[i]=nearest(p[0],p[1],dx,dy,12,Math.cos(25*Math.PI/180))[0];
    if(!r.rw[i]){let k=bins.findIndex(v=>r.off[i]<=v);if(k<0)k=11;hist[k]++;tot++;const pk=perKind[kind]||(perKind[kind]={n:0,o1:0,o2:0,none:0});pk.n++;if(r.off[i]>0.5&&r.off[i]<=12)pk.o1++;if(r.off[i]>1.5&&r.off[i]<=12)pk.o2++;if(r.off[i]>12)pk.none++;}
  }
}
console.log('samples off runway',tot);bins.concat(['>12/none']).forEach((b,i)=>console.log('<=',b,hist[i],(100*hist[i]/tot).toFixed(2)+'%'));
console.log(perKind);
}
