const zlib=require('zlib'),fs=require('fs');
class Img{constructor(x0,y0,x1,y1,w){this.x0=x0;this.y0=y0;this.x1=x1;this.y1=y1;this.s=w/(x1-x0);this.w=w;this.h=Math.round((y1-y0)*this.s);this.d=Buffer.alloc(this.w*this.h*3,0);for(let i=0;i<this.w*this.h;i++){this.d[i*3]=70;this.d[i*3+1]=72;this.d[i*3+2]=76;}}
 px(X,Y,c){if(X<0||Y<0||X>=this.w||Y>=this.h)return;const o=(Y*this.w+X)*3;this.d[o]=c[0];this.d[o+1]=c[1];this.d[o+2]=c[2];}
 pt(x,y,c,r=0){const X=Math.floor((x-this.x0)*this.s),Y=Math.floor((y-this.y0)*this.s);for(let i=-r;i<=r;i++)for(let j=-r;j<=r;j++)this.px(X+i,Y+j,c);}
 line(x0,y0,x1,y1,c,t=0){const n=Math.ceil(Math.max(Math.abs(x1-x0),Math.abs(y1-y0))*this.s)+1;if(n>20000)return;for(let i=0;i<=n;i++)this.pt(x0+(x1-x0)*i/n,y0+(y1-y0)*i/n,c,t);}
 tri(t,c){const s=this.s;const ax=(t[0]-this.x0)*s,ay=(t[1]-this.y0)*s,bx=(t[2]-this.x0)*s,by=(t[3]-this.y0)*s,cx=(t[4]-this.x0)*s,cy=(t[5]-this.y0)*s;
  const X0=Math.max(0,Math.floor(Math.min(ax,bx,cx))),X1=Math.min(this.w-1,Math.ceil(Math.max(ax,bx,cx))),Y0=Math.max(0,Math.floor(Math.min(ay,by,cy))),Y1=Math.min(this.h-1,Math.ceil(Math.max(ay,by,cy)));
  if(X0>X1||Y0>Y1)return;const D=(bx-ax)*(cy-ay)-(cx-ax)*(by-ay);if(Math.abs(D)<1e-9)return;
  for(let Y=Y0;Y<=Y1;Y++)for(let X=X0;X<=X1;X++){const px=X+0.5,py=Y+0.5;const u=((px-ax)*(cy-ay)-(cx-ax)*(py-ay))/D,v=((bx-ax)*(py-ay)-(px-ax)*(by-ay))/D;if(u>=-0.02&&v>=-0.02&&u+v<=1.02)this.px(X,Y,c);}
  this.line(t[0],t[1],t[2],t[3],c);this.line(t[2],t[3],t[4],t[5],c);}
 text(x,y,str,c,k=2){const F={'0':'111101101101111','1':'010110010010111','2':'111001111100111','3':'111001111001111','4':'101101111001001','5':'111100111001111','6':'111100111101111','7':'111001010010010','8':'111101111101111','9':'111101111001111','-':'000000111000000','.':'000000000000010','A':'111101111101101','B':'110101110101110','C':'111100100100111','D':'110101101101110','E':'111100111100111','G':'111100101101111','H':'101101111101101','L':'100100100100111','P':'111101111100100','R':'110101110101101','S':'111100111001111','W':'101101101111101','Y':'101101010010010','T':'111010010010010','U':'101101101101111','K':'101101110101101','O':'111101101101111','I':'111010010010111','F':'111100111100100','M':'101111111101101','N':'111101101101101','V':'101101101101010','X':'101101010101101','Z':'111001010100111','J':'001001001101111','Q':'111101101111001'};
  let X=Math.floor((x-this.x0)*this.s)+3,Y=Math.floor((y-this.y0)*this.s)+3;for(const ch of String(str).toUpperCase()){const g=F[ch];if(g)for(let r=0;r<5;r++)for(let q=0;q<3;q++)if(g[r*3+q]==='1')for(let a=0;a<k;a++)for(let b=0;b<k;b++)this.px(X+q*k+a,Y+r*k+b,c);X+=4*k;}}
 save(f){const raw=Buffer.alloc((this.w*3+1)*this.h);for(let y=0;y<this.h;y++){raw[y*(this.w*3+1)]=0;this.d.copy(raw,y*(this.w*3+1)+1,y*this.w*3,(y+1)*this.w*3);}
  const crcT=[];for(let n=0;n<256;n++){let c=n;for(let k=0;k<8;k++)c=c&1?0xedb88320^(c>>>1):c>>>1;crcT[n]=c>>>0;}const crc=b=>{let c=0xffffffff;for(const v of b)c=crcT[(c^v)&255]^(c>>>8);return (c^0xffffffff)>>>0;};
  const ch=(t,d)=>{const b=Buffer.alloc(12+d.length);b.writeUInt32BE(d.length,0);b.write(t,4);d.copy(b,8);b.writeUInt32BE(crc(b.slice(4,8+d.length)),8+d.length);return b;};
  const ih=Buffer.alloc(13);ih.writeUInt32BE(this.w,0);ih.writeUInt32BE(this.h,4);ih[8]=8;ih[9]=2;fs.writeFileSync(f,Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),ch('IHDR',ih),ch('IDAT',zlib.deflateSync(raw)),ch('IEND',Buffer.alloc(0))]));}
}
module.exports=Img;
