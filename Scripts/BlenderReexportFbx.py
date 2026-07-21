"""
Blender batch re-export: convert animation-only FBX files (whose joints are
exported as plain Null transform nodes) into proper armature/skeleton FBX files
bound to the mesh's skeleton, so the engine imports them without the per-bone
twist.

Why this is needed
------------------
The Malbers horse mesh FBX exports joints as `LimbNode` (real skeleton nodes);
the animation-only FBX files export them as `Null` transform nodes. Measured in
Blender, the two share the SAME bone-local orientation convention (offsets
between mesh rest and a near-rest animation frame are ~2 deg). So the ~90 deg
per-bone twist seen in-engine is NOT an inherent mesh/anim mismatch -- it is the
engine importing the `Null` nodes differently from `LimbNode` bones.

Re-exporting each animation as a real armature (LimbNode) that is retargeted onto
the mesh's skeleton makes the engine import it through the same working path as
the mesh, eliminating the twist. The mesh is re-exported too so the mesh skeleton
and the animations share one Blender-derived orientation.

Method
------
  1. Import the mesh once -> reference armature ARM; re-export the mesh.
  2. For each animation FBX:
       - import it (joints come in as an Empty hierarchy with animation),
       - constrain each ARM pose bone to Copy Transforms (world) from the
         matching Empty,
       - visual-bake the pose over the clip's frame range (clears constraints),
       - export ARM (armature only) with the baked action,
       - clear the baked action and the imported empties.

Both the new mesh and the new animations must be re-imported into the engine.

Usage
-----
  "C:/Program Files/Blender Foundation/Blender 5.1/blender.exe" \
      --background --factory-startup \
      --python Scripts/BlenderReexportFbx.py -- \
      [--mesh <mesh.fbx>] [--anim-dir <dir>] [--out <dir>] [--limit N] [--skip-mesh]
"""

import argparse
import glob
import os
import sys

import bpy

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_MESH = os.path.join(
    REPO_ROOT, "AppleJamEngine", "Content", "Mesh", "Horse",
    "MalbersHorse_noArmour_noLow.fbx")
DEFAULT_ANIM_DIR = os.path.join(
    REPO_ROOT, "AppleJamEngine", "Content", "Mesh", "Horse", "Animation")
DEFAULT_OUT_DIR = os.path.join(
    REPO_ROOT, "AppleJamEngine", "Content", "Mesh", "Horse", "Reexport")


def log(msg):
    print("[reexport] " + msg, flush=True)


def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--mesh", default=DEFAULT_MESH)
    p.add_argument("--anim-dir", default=DEFAULT_ANIM_DIR)
    p.add_argument("--out", default=DEFAULT_OUT_DIR)
    p.add_argument("--limit", type=int, default=0,
                   help="process at most N animations (0 = all)")
    p.add_argument("--skip-mesh", action="store_true")
    return p.parse_args(argv)


def ensure_fbx_addon():
    import addon_utils
    addon_utils.enable("io_scene_fbx", default_set=False, persistent=False)
    if not hasattr(bpy.ops.import_scene, "fbx"):
        raise RuntimeError("FBX add-on (io_scene_fbx) could not be enabled.")


# Import/export options shared so the mesh skeleton and the animation skeletons
# land in one consistent orientation / axis convention.
#
# automatic_bone_orientation MUST be False: the animation joints import as Empties
# that keep the raw source (Null) orientation regardless of this flag, so the mesh
# armature bones must also keep the source (LimbNode) orientation to match them.
# Measured, source LimbNode and Null local frames coincide (~2 deg at rest); auto
# orientation would re-roll the long bones and reintroduce a twist.
_IMPORT_OPTS = dict(automatic_bone_orientation=False, ignore_leaf_bones=True)
_EXPORT_OPTS = dict(
    use_selection=True,
    add_leaf_bones=False,
    primary_bone_axis="Y",
    secondary_bone_axis="X",
    apply_unit_scale=True,
    apply_scale_options="FBX_SCALE_NONE",
    axis_forward="-Z",
    axis_up="Y",
    path_mode="COPY",
)


def select_only(objects):
    bpy.ops.object.select_all(action="DESELECT")
    objs = [o for o in objects if o is not None]
    for o in objs:
        o.select_set(True)
    if objs:
        bpy.context.view_layer.objects.active = objs[0]


def scene_armature():
    for o in bpy.context.scene.objects:
        if o.type == "ARMATURE":
            return o
    return None


def scene_empties():
    return {o.name: o for o in bpy.context.scene.objects if o.type == "EMPTY"}


def delete_objects(objs):
    objs = [o for o in objs if o is not None]
    if not objs:
        return
    select_only(objs)
    bpy.ops.object.delete()


def purge_orphans():
    for coll in (bpy.data.actions, bpy.data.objects, bpy.data.meshes,
                 bpy.data.armatures, bpy.data.materials, bpy.data.images):
        for b in list(coll):
            if b.users == 0:
                coll.remove(b)


def empties_frame_range(empties):
    lo, hi = None, None
    for o in empties.values():
        ad = o.animation_data
        if ad and ad.action:
            fr = ad.action.frame_range
            lo = fr[0] if lo is None else min(lo, fr[0])
            hi = fr[1] if hi is None else max(hi, fr[1])
    if lo is None:
        lo, hi = bpy.context.scene.frame_start, bpy.context.scene.frame_end
    return int(round(lo)), int(round(hi))


def import_and_export_mesh(mesh_path, out_path):
    bpy.ops.import_scene.fbx(filepath=mesh_path, **_IMPORT_OPTS)
    arm = scene_armature()
    if arm is None:
        raise RuntimeError("mesh import produced no armature")
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    select_only([arm] + meshes)
    bpy.ops.export_scene.fbx(filepath=out_path, object_types={"ARMATURE", "MESH"},
                             bake_anim=False, use_mesh_modifiers=True, **_EXPORT_OPTS)
    return arm


def retarget_and_export_anim(arm, anim_path, out_path):
    # Import the animation -> Empty hierarchy carrying the joint animation.
    before = set(bpy.context.scene.objects)
    bpy.ops.import_scene.fbx(filepath=anim_path, **_IMPORT_OPTS)
    imported = [o for o in bpy.context.scene.objects if o not in before]
    empties = {o.name: o for o in imported if o.type == "EMPTY"}
    if not empties:
        raise RuntimeError("animation import produced no empties")

    frame_start, frame_end = empties_frame_range(empties)
    bpy.context.scene.frame_start = frame_start
    bpy.context.scene.frame_end = frame_end

    # Constrain each matching pose bone to follow its Empty in world space. The
    # mesh and animation share the same bone-local convention, so a world-space
    # copy reproduces the pose without introducing twist.
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="POSE")
    matched = 0
    for pbone in arm.pose.bones:
        emp = empties.get(pbone.name)
        if emp is None:
            continue
        con = pbone.constraints.new("COPY_TRANSFORMS")
        con.target = emp
        con.mix_mode = "REPLACE"
        con.target_space = "WORLD"
        con.owner_space = "WORLD"
        matched += 1

    # Visual-bake the constrained pose to keyframes and clear the constraints.
    bpy.ops.pose.select_all(action="SELECT")
    bpy.ops.nla.bake(
        frame_start=frame_start,
        frame_end=frame_end,
        step=1,
        only_selected=False,
        visual_keying=True,
        clear_constraints=True,
        clear_parents=False,
        use_current_action=True,
        bake_types={"POSE"},
    )
    bpy.ops.object.mode_set(mode="OBJECT")

    # Name the baked action after the clip (drives the engine's anim sequence name).
    baked = arm.animation_data.action if arm.animation_data else None
    if baked is not None:
        baked.name = os.path.splitext(os.path.basename(anim_path))[0]

    select_only([arm])
    bpy.ops.export_scene.fbx(filepath=out_path, object_types={"ARMATURE"},
                             bake_anim=True, bake_anim_use_all_actions=False,
                             bake_anim_use_nla_strips=False, bake_anim_step=1.0,
                             bake_anim_simplify_factor=0.0, **_EXPORT_OPTS)

    # Clean up: remove imported empties and the baked action so the next clip
    # starts from the reference armature only.
    delete_objects(list(empties.values()))
    if arm.animation_data and arm.animation_data.action:
        act = arm.animation_data.action
        arm.animation_data.action = None
        if act.users == 0:
            bpy.data.actions.remove(act)
    purge_orphans()
    return matched


def main():
    args = parse_args()
    ensure_fbx_addon()

    out_mesh_dir = args.out
    out_anim_dir = os.path.join(args.out, "Animation")
    os.makedirs(out_mesh_dir, exist_ok=True)
    os.makedirs(out_anim_dir, exist_ok=True)

    # Fresh scene.
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    purge_orphans()

    # Mesh (also the source of the reference armature).
    if not os.path.isfile(args.mesh):
        raise RuntimeError("mesh not found: " + args.mesh)
    log("mesh: " + os.path.basename(args.mesh))
    out_mesh = os.path.join(out_mesh_dir, os.path.basename(args.mesh))
    arm = import_and_export_mesh(args.mesh, out_mesh if not args.skip_mesh else out_mesh)
    if args.skip_mesh:
        log("(mesh re-exported anyway; armature is required as the retarget target)")

    anim_files = sorted(p for p in glob.glob(os.path.join(args.anim_dir, "*"))
                        if p.lower().endswith(".fbx"))
    if args.limit > 0:
        anim_files = anim_files[:args.limit]
    log("found %d animation FBX files" % len(anim_files))

    failures = []
    for i, path in enumerate(anim_files, 1):
        name = os.path.basename(path)
        try:
            matched = retarget_and_export_anim(arm, path, os.path.join(out_anim_dir, name))
            log("[%d/%d] ok: %s (%d bones)" % (i, len(anim_files), name, matched))
        except Exception as exc:
            failures.append((name, str(exc)))
            log("[%d/%d] FAIL: %s -- %s" % (i, len(anim_files), name, exc))
            # Best-effort recovery so one bad clip does not abort the batch.
            try:
                if bpy.context.object and bpy.context.object.mode != "OBJECT":
                    bpy.ops.object.mode_set(mode="OBJECT")
                delete_objects([o for o in bpy.context.scene.objects if o.type == "EMPTY"])
                if arm.animation_data and arm.animation_data.action:
                    arm.animation_data.action = None
                purge_orphans()
            except Exception:
                pass

    log("done. exported to %s (mesh + %d/%d anims)"
        % (args.out, len(anim_files) - len(failures), len(anim_files)))
    if failures:
        log("FAILURES:")
        for name, err in failures:
            log("  %s: %s" % (name, err))
        sys.exit(1)


if __name__ == "__main__":
    main()
