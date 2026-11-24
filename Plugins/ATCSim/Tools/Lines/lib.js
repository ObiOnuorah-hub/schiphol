const fs=require('fs');
const dir=(process.env.ATC_LINES_DIR||__dirname+'/../../../../Saved/ATC/Lines')+'/';
function loadPaint(){
  const b=fs.readFileSync(dir+'paint.bin'); const n=b.length/28; const T=[]; const seen=new Set();
  for(let i=0;i<n;i++){const o=i*28;const t=[b.readFloatLE(o)/100,b.readFloatLE(o+4)/100,b.readFloatLE(o+8)/100,b.readFloatLE(o+12)/100,b.readFloatLE(o+16)/100,b.readFloatLE(o+20)/100,b.readInt32LE(o+24)];const v=[0,1,2].map(k=>t[k*2].toFixed(2)+','+t[k*2+1].toFixed(2)).sort().join('|');if(seen.has(v))continue;seen.add(v);T.push(t);}
  return T;
}
function loadRoutes(file){
  const R=[];let r=null;
  for(const l of fs.readFileSync(file||dir+'routes.txt','utf8').split('\n')){
    if(l[0]==='R'){const p=l.split(' ');r={tag:p[1],len:+p[2]/100,label:p[4],pts:[],ctrl:[],stops:[]};R.push(r);}
    else if(l[0]==='C'){const p=l.split(' ');r.ctrl.push([+p[1]/100,+p[2]/100,+p[3]/100]);}
    else if(l[0]==='S'){const p=l.split(' ');r.stops.push([p[1],+p[2]/100]);}
    else if(l.length>3){const p=l.split(' ');r.pts.push([+p[0]/100,+p[1]/100,+p[2]/100]);}
  }
  return R;
}
// medial segments of strip triangles
function centreSegments(T){
  const key=(x,y)=>Math.round(x*50)+','+Math.round(y*50);
  const edges=new Map();
  T.forEach((t,i)=>{for(let e=0;e<3;e++){const a=e*2,c=((e+1)%3)*2;let k1=key(t[a],t[a+1]),k2=key(t[c],t[c+1]);if(k1===k2)continue;const k=k1<k2?k1+'|'+k2:k2+'|'+k1;let m=edges.get(k);if(!m){m=[];edges.set(k,m);}m.push(i);}});
  const segs=[];
  T.forEach((t,i)=>{
    const sh=[],bd=[];
    for(let e=0;e<3;e++){const a=e*2,c=((e+1)%3)*2;let k1=key(t[a],t[a+1]),k2=key(t[c],t[c+1]);if(k1===k2)continue;const k=k1<k2?k1+'|'+k2:k2+'|'+k1;
      const mid=[(t[a]+t[c])/2,(t[a+1]+t[c+1])/2,Math.hypot(t[a]-t[c],t[a+1]-t[c+1])];
      if(edges.get(k).length>1)sh.push(mid);else bd.push(mid);}
    const area=Math.abs((t[2]-t[0])*(t[5]-t[1])-(t[4]-t[0])*(t[3]-t[1]))/2;
    const lens=[Math.hypot(t[0]-t[2],t[1]-t[3]),Math.hypot(t[2]-t[4],t[3]-t[5]),Math.hypot(t[0]-t[4],t[1]-t[5])];
    const L=Math.max(...lens); const w=L>0?2*area/L:0;
    let A,B;
    if(sh.length===2){A=sh[0];B=sh[1];}
    else if(sh.length===1&&bd.length===2){A=sh[0];B=bd[0][2]<bd[1][2]?bd[0]:bd[1];}
    else if(sh.length===0&&bd.length===3){bd.sort((p,q)=>p[2]-q[2]);A=bd[0];B=bd[2];}
    else if(sh.length===3){sh.sort((p,q)=>q[2]-p[2]);A=sh[0];B=sh[1];}
    else return;
    segs.push([A[0],A[1],B[0],B[1],w,t[6],L,i]);
  });
  return segs;
}
class Grid{constructor(c){this.c=c;this.m=new Map();}
 k(i,j){return i*100003+j;}
 addSeg(s,idx){const c=this.c;const n=Math.max(1,Math.ceil(Math.hypot(s[0]-s[2],s[1]-s[3])/(c*0.5)));let last=null;for(let i=0;i<=n;i++){const x=s[0]+(s[2]-s[0])*i/n,y=s[1]+(s[3]-s[1])*i/n;const k=this.k(Math.floor(x/c),Math.floor(y/c));if(k!==last){let a=this.m.get(k);if(!a){a=[];this.m.set(k,a);}if(a[a.length-1]!==idx)a.push(idx);last=k;}}}
 query(x,y,r,f){const c=this.c;const i0=Math.floor((x-r)/c),i1=Math.floor((x+r)/c),j0=Math.floor((y-r)/c),j1=Math.floor((y+r)/c);for(let i=i0;i<=i1;i++)for(let j=j0;j<=j1;j++){const a=this.m.get(this.k(i,j));if(a)for(const v of a)f(v);}}
}
function segDist(x,y,s){const dx=s[2]-s[0],dy=s[3]-s[1];const l=dx*dx+dy*dy;let t=l>0?((x-s[0])*dx+(y-s[1])*dy)/l:0;t=Math.max(0,Math.min(1,t));const px=s[0]+dx*t,py=s[1]+dy*t;return [Math.hypot(x-px,y-py),px,py,t];}
module.exports={loadPaint,loadRoutes,centreSegments,Grid,segDist,dir,fs};
