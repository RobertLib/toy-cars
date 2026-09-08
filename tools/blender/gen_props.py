"""gen_props.py - standalone models of the collectible items."""
import bpy, sys, os, math

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
from lib import tcmesh as T      # noqa: E402
from lib import tcprops as P     # noqa: E402
from lib import tcrender as R    # noqa: E402

OUT = os.path.abspath(os.path.join(_HERE, "..", "..", "ToyCars", "Assets3D"))
PREVIEW = os.path.abspath(os.path.join(_HERE, "..", "preview"))


def palette():
    M = T.material
    return dict(
        can=M("CanRed", "#ff4d3d", roughness=0.34, metallic=0.28),
        can_dark=M("CanDark", "#39404d", roughness=0.5),
        can_light=M("CanLight", "#ffe066", roughness=0.4),
    )


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    T.reset_scene()
    T.clear_material_cache()
    pal = palette()
    can = P.pickup_canister(pal)
    T.bevel(can, 0.012, 2)
    T.apply_modifiers(can)
    T.shade_smooth(can, angle=math.radians(40))
    can.name = "Canister"
    path = os.path.join(OUT, "Props", "pickup_canister.usdz")
    bad = T.check_normals(can)
    T.export_usdz(path)
    print("PROP canister tris=%d normals=%s -> %s"
          % (T.tri_count(), "OK" if bad == 0 else "%d BAD" % bad, path))
    if "--preview" in argv:
        R.setup_studio()
        R.add_ground()
        R.render(os.path.join(PREVIEW, "canister.png"),
                 cam_loc=(1.5, -1.8, 1.2), look_at=(0, 0, 0.4),
                 res=(420, 360), lens=60)
    print("DONE props")


main()
