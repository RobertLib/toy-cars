"""gen_icon.py - renders the app icon from a toy car model."""
import bpy, sys, os, math
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
from lib import tcmesh as T      # noqa: E402
from lib import tcrender as R    # noqa: E402
from mathutils import Euler, Vector  # noqa: E402

OUT = os.path.abspath(os.path.join(_HERE, "..", "icon"))

src = open(os.path.join(_HERE, "gen_cars.py")).read().replace("\nmain()\n", "\n")
mod = {"__file__": os.path.join(_HERE, "gen_cars.py")}
exec(compile(src, "gen_cars.py", "exec"), mod)


def main():
    T.reset_scene()
    spec = dict(next(c for c in mod["CARS"] if c["id"] == "bumble"))
    root, _ = mod["build_car"](spec)
    root.rotation_euler = (0, 0, math.radians(-32))
    root.location = (0, 0, 0)

    R.setup_studio(bg='#4ab8ff', strength=1.25)
    T.box("Ground", (200, 200, 0.4), (0, 0, -0.2),
          T.material("IconGround", "#7ed957", roughness=0.9))
    # a low strip of road under the car so it clearly reads as racing
    T.box("Road", (200, 5.0, 0.06), (0, 0.2, 0.02),
          T.material("IconRoad", "#4a4f58", roughness=0.75))
    for k in range(-8, 9):
        T.box("Dash", (0.9, 0.16, 0.02), (k * 2.2, 0.2, 0.06),
              T.material("IconDash", "#ffffff", roughness=0.5))

    os.makedirs(OUT, exist_ok=True)
    R.render(os.path.join(OUT, "icon.png"),
             cam_loc=(3.2, -4.7, 3.5), look_at=(0.0, 0.35, 0.42),
             res=(1024, 1024), lens=54)
    print("ICON OK")


main()
