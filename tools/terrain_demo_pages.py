"""Author static HTTP page fixtures without allocating a whole heightfield.

The portable runtime/worker owns format validation and generation. This is demo
content, not an engine source provider or a requirement for application loaders.
"""
import argparse,json,math,struct,zlib
from pathlib import Path

def height(x,z):
    px=x*.5-256;pz=z*.5-256
    return 2+1.1*math.sin(px*.035)*math.cos(pz*.045)+2*math.exp(-((px-8)**2+(pz+6)**2)/90)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    samples=1025;cells=24;columns=(samples-2)//cells+1
    for page in range(columns*columns):
        x=(page%columns)*cells;z=(page//columns)*cells
        left=max(0,x-2);top=max(0,z-2);right=min(samples-1,x+cells+2);bottom=min(samples-1,z+cells+2)
        count=(right-left+1)*(bottom-top+1)
        out=bytearray(struct.pack('<IIIIIfffIfffIIIII',0x50544557,1,samples,samples,cells,.5,.5,3,3,240,120,60,page,1,0,count,count*4))
        weights=bytearray()
        for gz in range(top,bottom+1):
            for gx in range(left,right+1):
                h=height(gx,gz);out.extend(struct.pack('<f',h))
                px=gx*.5-256;pz=gz*.5-256
                soil=max(0,min(1,1.4-abs(pz-3*math.sin(px*.12))))
                rock=max(0,min(1,(h-3)/2));sand=max(0,min(1,1-abs(h-1.3)))*(1-soil)
                weights.extend(int(max(0,min(255,v*255))) for v in [(1-rock)*(1-soil)*(1-sand),sand*(1-rock),rock*(1-soil),soil])
        out.extend(weights);out.extend(struct.pack('<I',zlib.crc32(out)));target=args.output/f'{page}.weterrain'
        if not target.exists() or target.read_bytes()!=out:target.write_bytes(out)
    (args.output/'layout.json').write_text(json.dumps({'samplesX':samples,'samplesZ':samples,'chunkCells':cells,'spacing':[.5,.5],'textureMeters':3,'revision':1,'pages':columns*columns,'format':'WETP v1, clipped two-sample halo'},indent=2))

if __name__=='__main__':main()
