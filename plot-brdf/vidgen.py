import sys,os
import numpy as np

fps = 60
dur = 5
cnt = dur * fps

gen_wi = False
gen_wo = False
gen_ggx_cmap = False
gen_ggx_samples_merl = False
gen_ggx_samples_ggx = False
gen_parametric_merl = True
gen_parametric_ggx = True

def smoothstep(u):
    return 3 * u**2 - 2 * u**3

def smootherstep(u):
    return 6 * u**5 - 15 * u**4 + 10 * u**3

# render and export images
for i in range(cnt):
    u = float(i) / float(cnt - 1)
    # render the angle
    tmp = np.sin(smootherstep(u) * 2.0 * np.pi)
    ang = 45 + tmp * 30
    alpha = 0.3 + tmp * 0.25

    def renderToFrame(prefix):
        bmpFile = 'capture_00_000000000.bmp'
        frameName = prefix + '_' + format(i, '09')
        frameBmpName = frameName + '.bmp'
        cmd = 'mv ' + bmpFile + ' ' + frameBmpName
        os.system(cmd)
        cmd = 'mogrify -format png ' + frameBmpName + ' && rm ' + frameBmpName
        os.system(cmd)

    if (gen_wi):
        cmd = \
'./plot-brdf --frame-limit 1 --no-hud --hidden --record \
--shader-dir ../plot-brdf/shaders/ --disable-sphere-samples \
--dir ' + str(ang) + ' 255 --color 0.1 0.5 0.1 0.25' 
        os.system(cmd)
        renderToFrame('wi')
    
    if (gen_wo):
        cmd = \
'./plot-brdf --frame-limit 1 --no-hud --hidden --record \
--shader-dir ../plot-brdf/shaders/ --disable-sphere-wi-helper \
--dir ' + str(ang) + ' 255 --color 0.0 0.2 0.7 0.25' 
        os.system(cmd)
        renderToFrame('wo')

    if (gen_ggx_cmap):
        cmd = \
'./plot-brdf --frame-limit 1 --no-hud --hidden --record \
--shader-dir ../plot-brdf/shaders/ --disable-sphere-wi-helper \
--disable-sphere-samples --dir ' + str(ang) + ' 255 --color 0.1 0.5 0.1 0.7 \
--shading-cmap --alpha 0.3 --cmap ../plot-brdf/cmaps/divergent.png' 
        os.system(cmd)
        renderToFrame('ggx_cmap')

    if (gen_ggx_samples_merl):
        cmd = \
'./plot-brdf --frame-limit 1 --no-hud --hidden --record --scheme-merl \
--shader-dir ../plot-brdf/shaders/ --disable-sphere-wi-helper \
--enable-sphere-samples --dir 45 255 --color 0.1 0.5 0.1 0.7 \
--shading-cmap --alpha ' + str(alpha) + ' --cmap ../plot-brdf/cmaps/divergent.png' 
        os.system(cmd)
        renderToFrame('ggx_samples_merl')
  
    if (gen_ggx_samples_ggx):
        cmd = \
'./plot-brdf --frame-limit 1 --no-hud --hidden --record  --scheme-ggx \
--shader-dir ../plot-brdf/shaders/ --disable-sphere-wi-helper \
--enable-sphere-samples --dir 45 255 --color 0.1 0.5 0.1 0.7 \
--shading-cmap --alpha ' + str(alpha) + ' --cmap ../plot-brdf/cmaps/divergent.png' 
        os.system(cmd)
        renderToFrame('ggx_samples_ggx')

    if (gen_parametric_merl):
        cmd = \
'./plot-brdf --frame-limit 1 --no-hud --hidden --record --scheme-merl \
--shader-dir ../plot-brdf/shaders/ --disable-sphere-wi-helper \
--enable-parametric --dir 45 255 --color 0.1 0.5 0.1 0.7 \
--shading-cmap --alpha ' + str(alpha) + ' --cmap ../plot-brdf/cmaps/divergent.png' 
        os.system(cmd)
        renderToFrame('parametric_merl')
  
    if (gen_parametric_ggx):
        cmd = \
'./plot-brdf --frame-limit 1 --no-hud --hidden --record  --scheme-ggx \
--shader-dir ../plot-brdf/shaders/ --disable-sphere-wi-helper \
--enable-parametric --dir 45 255 --color 0.1 0.5 0.1 0.7 \
--shading-cmap --alpha ' + str(alpha) + ' --cmap ../plot-brdf/cmaps/divergent.png' 
        os.system(cmd)
        renderToFrame('parametric_ggx')

if (gen_wi):
    cmd = 'avconv -y -r ' + str(fps) + ' -f image2 -i wi_%09d.png -c:v libx264 -crf 20 -pix_fmt yuv420p video_wi.mp4'
    os.system(cmd)
if (gen_wo):
    cmd = 'avconv -y -r ' + str(fps) + ' -f image2 -i wo_%09d.png -c:v libx264 -crf 20 -pix_fmt yuv420p video_wo.mp4'
    os.system(cmd)
if (gen_ggx_cmap):
    cmd = 'avconv -y -r ' + str(fps) + ' -f image2 -i ggx_cmap_%09d.png -c:v libx264 -crf 20 -pix_fmt yuv420p video_ggx_cmap.mp4'
    os.system(cmd)
if (gen_ggx_samples_merl):
    cmd = 'avconv -y -r ' + str(fps) + ' -f image2 -i ggx_samples_merl_%09d.png -c:v libx264 -crf 20 -pix_fmt yuv420p video_ggx_samples_merl.mp4'
    os.system(cmd)
if (gen_ggx_samples_ggx):
    cmd = 'avconv -y -r ' + str(fps) + ' -f image2 -i ggx_samples_ggx_%09d.png -c:v libx264 -crf 20 -pix_fmt yuv420p video_ggx_samples_ggx.mp4'
    os.system(cmd)
if (gen_parametric_merl):
    cmd = 'avconv -y -r ' + str(fps) + ' -f image2 -i parametric_merl_%09d.png -c:v libx264 -crf 20 -pix_fmt yuv420p video_parametric_merl.mp4'
    os.system(cmd)
if (gen_parametric_ggx):
    cmd = 'avconv -y -r ' + str(fps) + ' -f image2 -i parametric_ggx_%09d.png -c:v libx264 -crf 20 -pix_fmt yuv420p video_parametric_ggx.mp4'
    os.system(cmd)

