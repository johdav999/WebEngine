"""Verify material delivery on browser WebGPU/WebGL2 (Playwright and Pillow required).

python tools/verify_browser_material_delivery.py --url http://127.0.0.1:8080 --evidence build/evidence/material-delivery
"""
import argparse
import json
import os
from pathlib import Path
import sys
from PIL import Image, ImageChops
from playwright.sync_api import sync_playwright

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--url',default='http://127.0.0.1:8080')
parser.add_argument('--evidence',type=Path,default=Path('build/evidence/material-delivery-browser'))
args=parser.parse_args()
evidence=args.evidence.resolve()
evidence.mkdir(exist_ok=True,parents=True)

def changed(a,b,box=None):
    x=Image.open(a).convert('RGB');y=Image.open(b).convert('RGB')
    if box:x=x.crop(box);y=y.crop(box)
    return sum(sum(p)>12 for p in ImageChops.difference(x,y).getdata())

with sync_playwright() as p:
    browser=p.chromium.launch(channel='chrome',args=['--enable-unsafe-webgpu','--enable-unsafe-swiftshader'])
    results=[]
    try:
        for backend in ('webgpu','webgl2'):
            page=browser.new_page(viewport={'width':1440,'height':1000},device_scale_factor=1)
            errors=[]
            page.on('pageerror',lambda error:errors.append(str(error)))
            page.on('console',lambda message:errors.append(message.text) if message.type=='error' else None)
            page.route('https://fonts.googleapis.com/**',lambda route:route.fulfill(status=200,content_type='text/css',body=''))
            page.route('**/favicon.ico',lambda route:route.fulfill(status=204,body=''))
            page.on('response',lambda response:print('resource',response.status,response.url,flush=True) if response.status>=400 else None)
            page.goto(args.url.rstrip('/')+'/?materialDeliveryTest=1&renderer='+backend+'&gpuTiming=off')
            try:
                page.wait_for_function("document.body.dataset.defaultModel==='ready' && Number(document.body.dataset.frames)>8",timeout=45000)
            except Exception:
                print('startup',backend,page.locator('#status').text_content(),page.locator('body').evaluate('(b)=>({...b.dataset})'),errors,flush=True)
                page.screenshot(path=evidence/(backend+'-startup-error.png'),full_page=True)
                raise
            assert page.locator('body').get_attribute('data-engine')=='ready'
            def capture(enabled,seconds,label,maps=False):
                assert page.evaluate('(args)=>Module._engine_material_delivery_fixture(args[0],args[1],args[2])',[enabled,seconds,int(maps)])==1
                page.wait_for_timeout(350)
                path=evidence/(backend+'-'+label+'.png')
                page.locator('#viewport').screenshot(path=path)
                assert page.locator('body').get_attribute('data-engine')=='ready'
                return path
            baseline=capture(0,0,'disabled')
            enabled=capture(1,0,'enabled')
            animated=capture(1,1,'animated')
            restored=capture(1,0,'restored')
            mapped=capture(1,0,'maps',True)
            metrics={'surfaceChanges':changed(baseline,enabled),'animationChanges':changed(enabled,animated),'restoreChanges':changed(enabled,restored)}
            width,height=Image.open(enabled).size
            for index,name in enumerate(('clearcoat','glass','skin','foliage','sheen','anisotropy','uv','wind')):
                x=width/2+((index%4)-1.5)*3.1*height/9
                y=height/2-(1.7 if index<4 else -1.7)*.993*height/9
                radius=height*.11
                box=(int(x-radius),int(y-radius),int(x+radius),int(y+radius))
                metrics[name]=changed(baseline,animated if index>=6 else enabled,box)
                assert metrics[name]>8,(backend,name,metrics[name])
                if index<6:
                    metrics[name+'Maps']=changed(enabled,mapped,box)
                    assert metrics[name+'Maps']>8,(backend,name+'Maps',metrics[name+'Maps'])
            assert metrics['surfaceChanges']>100,metrics
            assert metrics['animationChanges']>100,metrics
            assert metrics['restoreChanges']<40,metrics
            assert not errors,errors
            result={'backend':backend,'browser':browser.version,'metrics':metrics,'errors':errors,'passed':True}
            results.append(result);print(json.dumps(result),flush=True)
            page.close()
    finally:
        (evidence/'results.json').write_text(json.dumps(results,indent=2))
        browser.close()
