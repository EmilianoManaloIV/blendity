# Lighting in Blendity: baked, mixed and realtime

Blendity lights the rasterized views the way Unity does. Light that bounces between surfaces is either
**baked** ahead of time with the path tracer, or computed **live** by Realtime GI (voxel-based global
illumination). Baked light costs almost nothing to show. Live GI follows every change but costs time on
every frame that re-renders a view.

## The short version: a scene that doesn't lag
1. **Static objects:** select everything that never moves (floors, walls, buildings, props) and tick
   **MeshRenderer > Contribute GI**. Lightmap UVs are made for you (Generate Lightmap UVs).
2. **Lights:** set each light's **Mode**:
   - **Baked:** its direct light, shadows and bounces are all in the lightmaps. Nothing is computed
     live. Best for lights that never move.
   - **Mixed:** its bounced light is baked; its direct light and shadows stay live, so moving objects
     still cast shadows. The usual choice for the sun.
   - **Realtime:** nothing is baked. Use it for lights that move or change.
3. **Bake:** open **Window > Rendering > Lighting** and press **Generate Lighting**. The path tracer
   traces the sky, the Baked and Mixed lights and their bounces into lightmaps. A baked lightmap adds
   about 1 ms to a 1080p frame.
4. **Moving objects** (characters, props you drag) can't have lightmaps. Add a **Probe Volume**
   component (Global, or a box around the area) and bake again: they then take their bounced light from
   the probes, so they match the baked scene.
5. **Keep it up to date without thinking about it:** turn on **Auto Generate**. A second after you stop
   editing, it bakes again in the background. The status bar shows "Lighting out of date" until then.
6. **Save the scene:** the bake is saved next to it, in a folder with the scene's name.

## What Realtime GI is for
Realtime GI shows bounced light while you're still arranging lights and objects, with nothing to bake.
It only does live work for what the bake doesn't hold. The Lighting window shows a "Live GI:" line that
says which case applies:
- **nothing baked yet:** everything is live;
- **objects or lights changed since the bake:** live until the next bake (Auto Generate does it for you);
- **a Realtime Directional Light:** its bounce can't be baked, so it is live;
- **otherwise idle:** a current bake covers the scene and nothing is traced per frame, even with Realtime
  GI ticked.

### Realtime lightmaps, and why GI can look blocky
Contribute GI objects get **realtime lightmaps**: Realtime GI lights a low-resolution map on them
(**Realtime Resolution**, 2 texels per metre by default, as in Unity), updated a slice at a time
whenever the lighting changes. Moving the camera then costs a texture lookup, and they look the same
from any distance.

Everything else is traced per pixel at a lower resolution than the screen (**GI Resolution**: Half or
Quarter), then smoothed. Zoomed out, an object is only a few of those cells tall, so you can see blocks.
To avoid them:
- tick **Contribute GI** on objects that don't move, so they get a realtime lightmap;
- or set **GI Resolution = Half** (it costs more).

**Realtime Resolution = 0** turns the maps off, so everything is traced per pixel.

## On the GPU
With a Vulkan GPU (NVIDIA, AMD or Intel on Windows and Linux), two things run there, 4 to 8 times faster:
- **Realtime GI's gathers:** realtime lightmaps and live probes. **GI Device: Auto** (the default) uses
  the GPU when there is one, and a whole update fits in a frame.
- **Generate Lighting:** set **Render > Device** to **GPU Compute** (as for F12), and the lightmaps and
  probes are traced on the GPU.

The CPU gives the same results (within the noise and rounding), and it takes over by itself if the GPU
fails.

## Settings that matter
| Setting | What it does | When to change it |
|---|---|---|
| Lightmap Resolution | Texels per metre (Unity's default is 40) | Lower for big scenes and faster bakes, higher for sharp contact shadows |
| Indirect Samples | Rays per texel for bounces and the sky | Raise if lightmaps look noisy; denoising helps |
| Bounces | How many times light bounces | 2 is usually enough; interiors like 3 or 4 |
| Indirect Intensity | Scales the baked bounce | Artistic control |
| Realtime GI | Live voxel GI | Off for final work on a baked scene (idle anyway when the bake covers it) |
| GI Resolution | Half or Quarter of the screen, for objects without a realtime lightmap | Half for a cleaner Scene view |
| Realtime Resolution | Realtime lightmaps' texels per metre (2, as Unity) | Higher for detail in small rooms; 0 traces everything per pixel |
| GI Device | Where realtime GI gathers: Auto, CPU or GPU | CPU only to compare |
| Voxel Resolution | 64, 128 or 256 voxels across the scene | Higher for small detail in big scenes |
| Probe Min / Max Spacing | Probe density near geometry / in open space | Lower min spacing for small rooms |

## Path tracing
The Scene view rasterizes, so editing stays fast. To see the full path-traced picture:
- **F12** or the **Render** window renders the Main Camera;
- the **Camera Preview's Rendered** toggle path-traces a small preview of the selected camera.
