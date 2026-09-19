"""Embed the MME icon in the host without colliding with MMD's icon images.

An RC ICON statement assigns small automatic RT_ICON IDs, even when its
GROUP_ICON ID is large. Give both the group and every image an explicit ID.
"""
from pathlib import Path
import argparse
import re
import struct

from gen_resources import load_ico_images, render_res, RT_ICON, RT_GROUP_ICON

def build_icon(icon, header):
    definitions = dict(re.findall(r'^#define\s+(\w+)\s+(\d+)\s*$',
                                 header.read_text(encoding='utf-8'), re.M))
    first = int(definitions['IDI_MME_IMAGE_FIRST'])
    group_id = int(definitions['IDI_MME_APP'])
    images, group = load_ico_images(str(icon))
    group = bytearray(group)
    records = []
    for index, (_, image) in enumerate(images):
        image_id = first + index
        struct.pack_into('<H', group, 6 + index * 14 + 12, image_id)
        records.append((RT_ICON, image_id, 0, image))
    records.append((RT_GROUP_ICON, group_id, 0, bytes(group)))
    return render_res(records)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('icon', type=Path)
    parser.add_argument('header', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(build_icon(args.icon, args.header))
