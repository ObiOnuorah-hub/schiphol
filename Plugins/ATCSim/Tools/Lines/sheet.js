// node sheet.js out.png cols tile x,y,half x,y,half ...   [env ROUTES=file, FILTER=regex]
const A=require('./audit.js');const L=A.L;const Img=require('./png.js');const PV=require('./pave.js');const PC={0:[38,58,34],1:[92,92,94],2:[80,80,82],3:[66,66,68],4:[112,112,110],5:[74,74,80]};
const out=process.argv[2],cols=+process.argv[3],tile=+process.argv[4];
const spots=process.argv.slice(5).map(s=>s.split(',').map(Number));
const R=A.R;const filter=process.env.FILTER?new RegExp(process.env.FILTER):null;
const T=L.loadPaint();
let W=[];try{const b=L.fs.readFileSync(L.dir+'white.bin');for(let i=0;i<b.length/28;i++){const o=i*28;W.push([0,1,2,3,4,5].map(k=>b.readFloatLE(o+k*4)/100));}}catch(e){}
const geo=(lat,lon)=>{};
let osm=[];try{const j=JSON.parse(L.fs.readFileSync(__dirname+'/../../Data/OSM/EHAM.json','utf8'));const R0=6378137;const la0=52.3075170,lo0=4.7624393;
 for(const e of j.elements||[]){const k=e.tags&&e.tags.aeroway;if(!e.geometry||!(k==='taxiway'||k==='taxilane'||k==='parking_position'))continue;osm.push([k,e.geometry.map(g=>[(g.lon-lo0)*Math.PI/180*R0*Math.cos(la0*Math.PI/180),-(g.lat-la0)*Math.PI/180*R0])]);}}catch(e){console.log(e.message)}
const rows=Math.ceil(spots.length/cols);
const big=new Img(0,0,cols*tile,rows*tile,cols*tile);
const cos=Math.cos(25*Math.PI/180);
spots.forEach((s,idx)=>{const [cx,cy,h]=s;const im=new Img(cx-h,cy-h,cx+h,cy+h,tile-4);
 for(let Y=0;Y<im.h;Y++)for(let X=0;X<im.w;X++)im.px(X,Y,PC[PV.paved(im.x0+(X+0.5)/im.s,im.y0+(Y+0.5)/im.s)]);const inside=(x,y,m=20)=>x>cx-h-m&&x<cx+h+m&&y>cy-h-m&&y<cy+h+m;
 for(const [k,g] of osm)for(let i=1;i<g.length;i++)if(inside(g[i][0],g[i][1],300)||inside(g[i-1][0],g[i-1][1],300))im.line(g[i-1][0],g[i-1][1],g[i][0],g[i][1],k==='parking_position'?[60,100,60]:[60,95,130]);
 for(const t of W)if(inside(t[0],t[1],400)||inside(t[2],t[3],400)||inside(t[4],t[5],400))im.tri(t,[235,235,235]);
 for(const t of T)if(inside(t[0],t[1]))im.tri(t,t[6]?[200,120,30]:[240,205,40]);
 const step=h>120?50:h>50?20:10;for(let x=Math.ceil((cx-h)/step)*step;x<cx+h;x+=step)im.text(x,cy-h,Math.round(x),[200,200,200],1);for(let y=Math.ceil((cy-h)/step)*step;y<cy+h;y+=step)im.text(cx-h,y,Math.round(y),[200,200,200],1);
 for(const r of R){if(filter&&!filter.test(r.tag))continue;const n=r.pts.length;const push=/Pushback/.test(r.tag);for(let i=1;i<n;i++){const p=r.pts[i],q=r.pts[i-1];if(!inside(p[0],p[1],2))continue;const l=Math.hypot(p[0]-q[0],p[1]-q[1])||1;const o=A.nearest(p[0],p[1],(p[0]-q[0])/l,(p[1]-q[1])/l,12,cos)[0];im.line(q[0],q[1],p[0],p[1],push?[0,220,255]:o<0.8?[40,220,60]:o<100?[255,60,40]:[255,60,255]);}}
 im.text(cx-h+h*0.04,cy-h+h*0.06,idx+' '+cx+' '+cy,[255,255,255],2);
 const ox=(idx%cols)*tile,oy=Math.floor(idx/cols)*tile;for(let y=0;y<im.h;y++)for(let x=0;x<im.w;x++){const a=(y*im.w+x)*3,b=((oy+y)*big.w+ox+x)*3;big.d[b]=im.d[a];big.d[b+1]=im.d[a+1];big.d[b+2]=im.d[a+2];}
});
big.save(out);
