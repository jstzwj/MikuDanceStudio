"""Inspect/freeze PMM v2 comparison inputs without starting MMD or a device.

Disk widths follow pmm_save.cpp / pmm_io_common.hpp, not runtime sizeof.
Only the global physics byte is changed. A frame change requires a real seek
and save, so this tool rejects a source saved at a different requested frame.
Model/effect assets stay local; the manifest records their paths and hashes.
"""

import argparse
import configparser
import hashlib
import json
from pathlib import Path
import struct


def sha256(data):
    return hashlib.sha256(data).hexdigest()


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, size):
        if size < 0 or self.pos + size > len(self.data):
            raise ValueError(f"truncated PMM at byte {self.pos}, requested {size}")
        result = self.data[self.pos:self.pos + size]
        self.pos += size
        return result

    def values(self, fmt):
        return list(struct.unpack("<" + fmt, self.take(struct.calcsize("<" + fmt))))

    def one(self, fmt):
        return self.values(fmt)[0]

    def count(self):
        count = self.one("i")
        if not 0 <= count <= len(self.data):
            raise ValueError(f"invalid PMM count {count} at {self.pos - 4}")
        return count

    def text(self, size=None):
        return self.take(self.one("B") if size is None else size).split(b"\0", 1)[0].decode("cp932")

    def track(self, record_size, dense=1):
        start = self.pos
        self.take(record_size * dense)
        sparse = self.count()
        self.take((record_size + 4) * sparse)
        return {"base_records": dense, "additional_records": sparse,
                "sha256": sha256(self.data[start:self.pos])}


def inspect_pmm(data):
    r = Reader(data)
    if r.take(30).split(b"\0", 1)[0] != b"Polygon Movie maker 0002":
        raise ValueError("expected PMM v2")
    result = {"render_size": r.values("ii"), "sidebar_width": r.one("i"),
              "camera_fov": r.one("f"), "ui_flags": r.values("7B"),
              "selected_model": r.one("B")}
    models = []
    for _ in range(r.one("B")):
        m = {"slot": r.one("B"), "name": r.text(), "english_name": r.text(),
             "path": r.text(256), "group_count": r.one("B")}
        bones = r.count()
        for _ in range(bones):
            r.text()
        morphs = r.count()
        for _ in range(morphs):
            r.text()
        iks = r.count()
        r.take(iks * 4)
        selectors = r.count()
        r.take(selectors * 4)
        m.update(bones=bones, morphs=morphs, ik_chains=iks, parent_selectors=selectors,
                 combo_selection=r.one("B"), load_complete=r.one("B"), selected_bone=r.one("i"),
                 selected_morphs=r.values("4i"))
        r.take(r.one("B"))  # Expanded display groups.
        m.update(bone_list_position=r.one("i"), max_frame=r.one("i"))
        m["bone_keys"] = r.track(58, bones)
        m["morph_keys"] = r.track(17, morphs)
        m["display_keys"] = r.track(14 + iks + selectors * 8)
        m["saved_pose_sha256"] = sha256(r.take(bones * 31 + morphs * 4 + iks + selectors * 16))
        m.update(post_load_flag2=r.one("B"), edge_scale=r.one("f"), toon_enabled=r.one("B"),
                 display_order=r.one("B"))
        models.append(m)
    result["models"] = models
    camera_start = r.pos
    result["camera_keys"] = r.track(78)
    result["camera_base"] = dict(zip(
        ("frame", "previous", "next", "distance", "position", "rotation", "parent_model", "parent_bone"),
        (*struct.unpack_from("<iiif", data, camera_start),
         list(struct.unpack_from("<3f", data, camera_start + 16)),
         list(struct.unpack_from("<3f", data, camera_start + 28)),
         *struct.unpack_from("<ii", data, camera_start + 40))))
    result["camera_base"].update(zip(("perspective", "fov", "selected"),
                                     struct.unpack_from("<BiB", data, camera_start + 72)))
    result["camera_current"] = {"position": r.values("3f"), "view_offset": r.values("2f"),
                                "distance": r.one("f"), "rotation": r.values("3f"),
                                "perspective": r.one("B")}
    light_start = r.pos
    result["light_keys"] = r.track(37)
    result["light_base"] = {"frame": struct.unpack_from("<i", data, light_start)[0],
                            "color": list(struct.unpack_from("<3f", data, light_start + 12)),
                            "direction": list(struct.unpack_from("<3f", data, light_start + 24))}
    result["light_current"] = {"color": r.values("3f"), "direction": r.values("3f")}
    result["selected_accessory"] = r.one("B")
    result["object_list_scroll"] = r.one("i")
    accessory_count = r.one("B")
    result["accessory_list"] = [r.text(100) for _ in range(accessory_count)]
    accessories = []
    for _ in range(accessory_count):
        a = {"slot": r.one("B"), "name": r.text(100), "path": r.text(256),
             "display_order": r.one("B"), "keys": r.track(51),
             "visibility_opacity_byte": r.one("B"), "parent_model": r.one("i"),
             "parent_bone": r.one("i"), "rotation": r.values("3f"), "scale": r.one("f"),
             "position": r.values("3f"), "shadow": r.one("B"), "additive": r.one("B")}
        accessories.append(a)
    result["accessories"] = accessories
    result.update(current_frame=r.one("i"), timeline_start=r.one("i"),
                  last_registered_frame=r.one("i"), edit_mode=r.one("i"),
                  camera_reference_mode=r.one("B"), playback_flags=r.values("3B"),
                  playback_range=r.values("2i"), wave_enabled=r.one("B"), wave_path=r.text(256))
    result["avi"] = {"offset_scale": r.values("iif"), "path": r.text(256), "enabled": r.one("i")}
    result["picture"] = {"offset_scale": r.values("iif"), "path": r.text(256), "enabled": r.one("B")}
    result.update(fps_overlay=r.one("B"), ground_grid=r.one("B"), ground_shadow=r.one("B"),
                  fps_limit=r.one("f"), capture_mode=r.one("i"), accessory_split_order=r.one("i"),
                  shadow_ambient=r.one("f"), shadow_blend=r.one("B"))
    result["physics_mode_offset"] = r.pos
    result["physics_mode"] = r.one("B")
    if result["physics_mode"] not in range(4):
        raise ValueError("invalid physics mode")
    result["gravity"] = {"magnitude": r.one("f"), "noise": r.one("i"),
                         "direction": r.values("3f"), "noise_enabled": r.one("B")}
    result["gravity_keys"] = r.track(34)
    result["self_shadow"] = {"enabled": r.one("B"), "distance": r.one("f"), "keys": r.track(18)}
    result.update(outline_color=r.values("3f"), black_background=r.one("B"),
                  camera_parent_model=r.one("i"), camera_parent_bone=r.one("i"),
                  camera_parent_basis=r.values("16f"), follow_camera=r.one("B"),
                  suppress_attachment_transform=r.one("B"), floor_visible=r.one("B"),
                  goto_frame=r.one("i"))
    if r.one("B") != 1:
        raise ValueError("unsupported PMM model-tail marker")
    result["registration_selection"] = [r.values("Bi") for _ in models]
    if r.pos != len(data):
        raise ValueError(f"unexpected trailing PMM data at {r.pos}/{len(data)}")
    return result


def inspect_emm(data):
    ini = configparser.ConfigParser(interpolation=None, delimiters=("=",))
    ini.optionxform = str
    ini.read_string(data.decode("utf-8-sig") if data.startswith(b"\xef\xbb\xbf") else data.decode("cp932"))
    if ini.getint("Info", "Version") != 3:
        raise ValueError("expected EMM v3")
    return {name: dict(ini[name]) for name in ini.sections()}


def freeze(pmm_path, output_path, frame):
    pmm_path, output_path = Path(pmm_path), Path(output_path)
    if output_path.suffix.lower() != ".pmm":
        raise ValueError("output must have a .pmm extension")
    if pmm_path.resolve() == output_path.resolve():
        raise ValueError("output must differ from source")
    data = pmm_path.read_bytes()
    before = inspect_pmm(data)
    if before["current_frame"] != frame:
        raise ValueError(f"source is saved at frame {before['current_frame']}, requested {frame}; seek and save first")
    frozen = bytearray(data)
    frozen[before["physics_mode_offset"]] = 0  # PhysicsMode::Disabled, not on/off mode (2).
    after = inspect_pmm(frozen)
    unchanged = dict(before, physics_mode=0)
    if after != unchanged:
        raise ValueError("unexpected change outside the global physics mode")
    emm = pmm_path.with_suffix(".emm").read_bytes()
    mapping = inspect_emm(emm)
    assets = set()
    for obj in after["models"] + after["accessories"]:
        assets.add(obj["path"])
    for section, entries in mapping.items():
        for key, path in entries.items():
            if section == "Object" or (section.startswith("Effect") and not key.endswith(".show") and path != "none"):
                assets.add(path)
    # PMM/EMM paths are relative to the scene directory when not absolute.
    # Moving a relative scene requires a real path rebase, never a silent copy.
    if output_path.resolve().parent != pmm_path.resolve().parent and any(not Path(p).is_absolute() for p in assets):
        raise ValueError("relative asset paths require output in the source scene directory")
    fingerprints = []
    for asset in sorted(assets):
        path = Path(asset)
        if not path.is_absolute():
            path = pmm_path.parent / path
        if not path.is_file():
            raise ValueError(f"missing comparison asset: {path}")
        fingerprints.append({"path": str(path.resolve()), "sha256": sha256(path.read_bytes())})
    manifest = {"version": 1, "source": str(pmm_path.resolve()), "source_sha256": sha256(data),
                "source_physics_mode": before["physics_mode"], "pmm_sha256": sha256(frozen),
                "emm_sha256": sha256(emm), "scene": after, "effect_mapping": mapping,
                "assets": fingerprints,
                "scope": "Disk inputs verified only; runtime mode, seeks, deterministic captures and effect acceptance still require paired execution."}
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(frozen)
    output_path.with_suffix(".emm").write_bytes(emm)
    output_path.with_suffix(".json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pmm", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--frame", type=int, default=0)
    args = parser.parse_args()
    result = freeze(args.pmm, args.output, args.frame) if args.output else inspect_pmm(args.pmm.read_bytes())
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
