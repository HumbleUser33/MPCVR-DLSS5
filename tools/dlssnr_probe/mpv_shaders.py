"""The translator of mpv user shaders now lives with the renderer's shaders, in
Shaders/mpv/mpv_shaders.py; this runs it, for the harness's own shaders:

    python mpv_shaders.py <shader> <output dir> [--tools <dir with glslang and spirv-cross>]
"""

import os
import runpy

runpy.run_path(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Shaders", "mpv", "mpv_shaders.py"),
               run_name="__main__")
