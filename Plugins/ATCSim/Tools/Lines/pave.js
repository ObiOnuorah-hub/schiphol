// pavement mask 0.5 m
const L=require('./lib.js');
const b=L.fs.readFileSync(L.dir+'pave.bin');const T=[];for(let i=0;i<b.length/28;i++){const o=i*28;T.push([0,1,2,3,4,5].map(k=>b.readFloatLE(o+k*4)/100).concat([b.readInt32LE(o+24)]));}
const X0=-4200,Y0=-6500,X1=3400,Y1=3000,RES=0.5;const W=(X1-X0)/RES,H=(Y1-Y0)/RES;
let M;const f=L.dir+'pave_mask.bin';
if(L.fs.existsSync(f)){M=L.fs.readFileSync(f);}
else{M=Buffer.alloc(W*H);for(const t of T){const ax=(t[0]-X0)/RES,ay=(t[1]-Y0)/RES,bx=(t[2]-X0)/RES,by=(t[3]-Y0)/RES,cx=(t[4]-X0)/RES,cy=(t[5]-Y0)/RES;const D=(bx-ax)*(cy-ay)-(cx-ax)*(by-ay);if(Math.abs(D)<1e-9)continue;
 const x0=Math.max(0,Math.floor(Math.min(ax,bx,cx))),x1=Math.min(W-1,Math.ceil(Math.max(ax,bx,cx))),y0=Math.max(0,Math.floor(Math.min(ay,by,cy))),y1=Math.min(H-1,Math.ceil(Math.max(ay,by,cy)));
 for(let y=y0;y<=y1;y++)for(let x=x0;x<=x1;x++){const px=x+0.5,py=y+0.5;const u=((px-ax)*(cy-ay)-(cx-ax)*(py-ay))/D,v=((bx-ax)*(py-ay)-(px-ax)*(by-ay))/D;if(u>=-0.001&&v>=-0.001&&u+v<=1.001)M[y*W+x]=t[6];}}
 L.fs.writeFileSync(f,M);}
module.exports={T,paved:(x,y)=>{const i=Math.floor((x-X0)/RES),j=Math.floor((y-Y0)/RES);return i>=0&&j>=0&&i<W&&j<H?M[j*W+i]:0;}};
