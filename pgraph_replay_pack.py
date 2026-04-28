#!/usr/bin/env python3

import argparse
import re
import os
import shutil
import json
import hashlib
import struct
from pathlib import Path
from dataclasses import dataclass
from typing import List, Dict, Any, Optional

_HEX_VALUE = r"0x[0-9a-fA-F]+"
_PGRAPH_METHOD_RE = re.compile(
    r"nv2a_pgraph_method (\d+):\s+(" + _HEX_VALUE + r") -> (" + _HEX_VALUE + r")\s+(?:(\S+)\s+)?\(?(" + _HEX_VALUE + r")\)?"
)

# Commands that take VRAM offsets and their short names
ADDRESS_COMMANDS = {
    0x0210: "SCO", # NV097_SET_SURFACE_COLOR_OFFSET
    0x0214: "SZO", # NV097_SET_SURFACE_ZETA_OFFSET
    0x0308: "S2S", # NV062_SET_OFFSET_SOURCE
    0x030C: "S2D", # NV062_SET_OFFSET_DESTIN
    0x1B00: "TX0", 0x1B40: "TX1", 0x1B80: "TX2", 0x1BC0: "TX3", # NV097_SET_TEXTURE_OFFSET
    0x1C00: "TX4", 0x1C40: "TX5", 0x1C80: "TX6", 0x1CC0: "TX7",
    0x1D00: "TX8", 0x1D40: "TX9", 0x1D80: "TX10", 0x1DC0: "TX11",
    0x1E00: "TX12", 0x1E40: "TX13", 0x1E80: "TX14", 0x1EC0: "TX15",
    0x1720: "VA0", 0x1724: "VA1", 0x1728: "VA2", 0x172C: "VA3", 0x1730: "VA4", 0x1734: "VA5", 0x1738: "VA6", 0x173C: "VA7", # NV097_SET_VERTEX_DATA_ARRAY_OFFSET
    0x1740: "VA8", 0x1744: "VA9", 0x1748: "VA10", 0x174C: "VA11", 0x1750: "VA12", 0x1754: "VA13", 0x1758: "VA14", 0x175C: "VA15",
    0x17C0: "IBO", # NV097_SET_INDEX_BUFFER_OFFSET (Note: 0x17C0 is also NV097_SET_LOGIC_OP)
    0x1D60: "PAL", # NV097_SET_TEXTURE_PALETTE_OFFSET
    0x1D6C: "SEM", # NV097_SET_SEMAPHORE_OFFSET
}

RESOURCE_CATEGORIES = {
    "SCO": "surf",
    "S2S": "surf",
    "S2D": "surf",
    "SZO": "surf",
    "TX0": "tex", "TX1": "tex", "TX2": "tex", "TX3": "tex",
    "TX4": "tex", "TX5": "tex", "TX6": "tex", "TX7": "tex",
    "TX8": "tex", "TX9": "tex", "TX10": "tex", "TX11": "tex",
    "TX12": "tex", "TX13": "tex", "TX14": "tex", "TX15": "tex",
    "VA0": "vbuf", "VA1": "vbuf", "VA2": "vbuf", "VA3": "vbuf",
    "VA4": "vbuf", "VA5": "vbuf", "VA6": "vbuf", "VA7": "vbuf",
    "VA8": "vbuf", "VA9": "vbuf", "VA10": "vbuf", "VA11": "vbuf",
    "VA12": "vbuf", "VA13": "vbuf", "VA14": "vbuf", "VA15": "vbuf",
    "IBO": "ibuf",
    "PAL": "pal",
    "SEM": "sem",
}

@dataclass
class PGRAPHMethod:
    nv_class: int
    nv_op: int
    nv_param: int
    nv_op_name: str

def parse_pgraph_txt(path: str) -> List[PGRAPHMethod]:
    methods = []
    if not os.path.exists(path):
        return methods
    with open(path, 'r') as f:
        for line in f:
            match = _PGRAPH_METHOD_RE.match(line.strip())
            if match:
                methods.append(PGRAPHMethod(
                    nv_class=int(match.group(2), 16),
                    nv_op=int(match.group(3), 16),
                    nv_param=int(match.group(5), 16),
                    nv_op_name=match.group(4)
                ))
    return methods

def get_file_hash(path: str) -> str:
    hasher = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hasher.update(chunk)
    return hasher.hexdigest()

def generate_resource_manifest(dump_dir: str, out_dir: str, move_files: bool = False, verbose: bool = False):
    out_path = Path(out_dir)
    res_dir = out_path / "resources"
    res_dir.mkdir(parents=True, exist_ok=True)

    manifest = {
        "resources": {},        # hash -> { filename, size, usages: [{frame, draw, address, id}] }
        "command_buffers": {},   # filename -> { patches: [{offset, resource_id}] }
        "frames": [],
        "initial_state": None
    }

    # Copy initial-state.bin if it exists
    initial_state_src = os.path.join(dump_dir, "initial-state.bin")
    if os.path.exists(initial_state_src):
        initial_state_dst = res_dir / "initial-state"
        if move_files:
            shutil.move(initial_state_src, initial_state_dst)
        else:
            shutil.copy2(initial_state_src, initial_state_dst)
        manifest["initial_state"] = "initial-state"

    # Tracking unique resource data to avoid duplicates
    data_hashes = {} # hash -> filename
    # Persistent mapping of addresses to categories
    addr_to_category = {}

    frames = sorted([d for d in os.listdir(dump_dir) if d.startswith("frame_")])
    
    for f_idx, frame in enumerate(frames):
        frame_path = os.path.join(dump_dir, frame)
        draws = sorted([d for d in os.listdir(frame_path) if d.startswith("draw_")])
        
        frame_info = {
            "name": frame,
            "draws": [],
            "terminal_buffer": None
        }

        # Resources found in this frame/draw: (addr) -> target_filename
        addr_to_filename = {}

        for d_idx, draw in enumerate(draws):
            draw_path = os.path.join(frame_path, draw)
            
            # Pre-parse PGRAPH to find address types for categorization
            pgraph_methods = parse_pgraph_txt(os.path.join(draw_path, "pgraph-draw.txt"))
            for m in pgraph_methods:
                if m.nv_op in ADDRESS_COMMANDS:
                    short_name = ADDRESS_COMMANDS[m.nv_op]
                    category = RESOURCE_CATEGORIES[short_name]
                    addr_to_category[m.nv_param] = category

            if not pgraph_methods:
                print(f"WARNING: No pgraph methods found for draw {draw_path}")
                continue

            # 1. Discover and process resources in this draw
            for filename in os.listdir(draw_path):
                if filename.endswith(".bin"):
                    match = re.search(r"0x([0-9a-fA-F]{8})", filename)
                    if match:
                        addr = int(match.group(1), 16)
                        if addr not in addr_to_category:
                            print(f"Warning, no reference to {filename} in PGRAPH file")
                            continue
                        source_res_path = os.path.join(draw_path, filename)
                        
                        file_hash = get_file_hash(source_res_path)
                        file_size = os.path.getsize(source_res_path)
                        
                        category = addr_to_category[addr]
                        
                        if file_hash in data_hashes:
                            target_filename = data_hashes[file_hash]
                        else:
                            target_filename = f"{category}_{file_hash}"
                            data_hashes[file_hash] = target_filename

                        addr_to_filename[addr] = target_filename

                        if target_filename not in manifest["resources"]:
                            target_path = res_dir / target_filename
                            
                            if move_files:
                                shutil.move(source_res_path, target_path)
                            else:
                                shutil.copy2(source_res_path, target_path)
                            
                            manifest["resources"][target_filename] = {
                                "size": file_size,
                                "usages": [],
                                "category": category
                            }
                        else:
                            existing_category = manifest["resources"][target_filename]["category"]
                            if existing_category != category:
                                print(f"WARNING: Resource {file_hash} used as {category}, but already categorized as {existing_category}")
                        
                        manifest["resources"][target_filename]["usages"].append({
                            "frame": f_idx,
                            "draw": d_idx,
                            "address": addr
                        })

            # 2. Process PGRAPH commands for this draw
            methods = pgraph_methods # Reuse already parsed methods
            if methods:
                buffer_name = f"frame{f_idx}_draw{d_idx}_pgraph"
                patches = []
                binary_data = bytearray()
                
                for m in methods:
                    op_offset = len(binary_data)
                    param_offset = op_offset + 8
                    
                    if m.nv_op in ADDRESS_COMMANDS:
                        if m.nv_param in addr_to_filename:
                            filename = addr_to_filename[m.nv_param]
                            patches.append({"offset": param_offset, "resource": filename})

                    binary_data.extend(struct.pack("<III", m.nv_class, m.nv_op, m.nv_param))
                
                with open(res_dir / buffer_name, "wb") as bf:
                    bf.write(binary_data)
                
                manifest["command_buffers"][buffer_name] = {"patches": patches}
                frame_info["draws"].append({
                    "name": draw,
                    "buffer": buffer_name
                })

        # 3. Handle terminal PGRAPH commands for the frame
        terminal_pgraph_path = os.path.join(frame_path, "pgraph-terminator.txt")
        if os.path.exists(terminal_pgraph_path):
            methods = parse_pgraph_txt(terminal_pgraph_path)
            if methods:
                buffer_name = f"frame{f_idx}_terminal_pgraph"
                binary_data = bytearray()
                for m in methods:
                    if m.nv_op in ADDRESS_COMMANDS:
                        short_name = ADDRESS_COMMANDS[m.nv_op]
                        addr_to_category[m.nv_param] = RESOURCE_CATEGORIES[short_name]
                    binary_data.extend(struct.pack("<III", m.nv_class, m.nv_op, m.nv_param))
                
                with open(res_dir / buffer_name, "wb") as bf:
                    bf.write(binary_data)
                
                manifest["command_buffers"][buffer_name] = {"patches": []} # Terminal usually has no patches or we don't track them yet
                frame_info["terminal_buffer"] = buffer_name

        manifest["frames"].append(frame_info)

    # Save manifest
    manifest["pack"] = "resources.bin"
    with open(res_dir / "manifest.json", "w") as f:
        json.dump(manifest, f, indent=2)

    # Pack resources
    create_resource_pack(res_dir)

    print(f"Manifest and resources generated in {out_dir}")
    
    if verbose:
        num_unique = len(manifest["resources"])
        total_saved_bytes = sum((len(res["usages"]) - 1) * res["size"] for res in manifest["resources"].values())
        print(f"Unique items: {num_unique}")
        print(f"Total bytes saved: {total_saved_bytes} ({total_saved_bytes / 1024 / 1024:.2f} MiB)")

def create_resource_pack(res_dir: Path, pack_filename: str = "resources.bin"):
    """
    Packs all files in res_dir (except manifest.json and the pack itself) into a single binary file.
    Each item is aligned to a 16-byte boundary.
    """
    files_to_pack = [f for f in res_dir.iterdir() if f.is_file() and f.name != "manifest.json" and f.name != pack_filename]
    if not files_to_pack:
        return
    
    files_to_pack.sort(key=lambda x: x.name)
    
    pack_path = res_dir / pack_filename
    header_fmt = "<4sIIII" # Magic, Version, ItemCount, DataOffset, DataSize
    index_entry_fmt = "<128sII" # Name, Offset, Size
    
    magic = b"XPAK"
    version = 1
    item_count = len(files_to_pack)
    
    index_entries = []
    data_to_write = []
    
    # Calculate initial offset for the first data item
    # Offset starts after header and the entire index table
    header_size = struct.calcsize(header_fmt)
    index_table_size = item_count * struct.calcsize(index_entry_fmt)
    data_start_offset = (header_size + index_table_size + 15) & ~15
    current_offset = data_start_offset
    
    last_data_end = data_start_offset
    for f_path in files_to_pack:
        with open(f_path, "rb") as f:
            data = f.read()
        
        size = len(data)
        # Store entry in index (offset is relative to data_start_offset)
        name_bytes = f_path.name.encode("ascii")
        relative_offset = current_offset - data_start_offset
        index_entries.append(struct.pack(index_entry_fmt, name_bytes, relative_offset, size))
        
        data_to_write.append(data)
        last_data_end = current_offset + size
        # Advance offset to next 16-byte boundary
        current_offset = (last_data_end + 15) & ~15
        
    total_data_size = last_data_end - data_start_offset

    with open(pack_path, "wb") as pf:
        # Write header
        pf.write(struct.pack(header_fmt, magic, version, item_count, data_start_offset, total_data_size))
        # Write index table
        for entry in index_entries:
            pf.write(entry)
        
        # Write data blocks with alignment
        for data in data_to_write:
            # Align current position to 16 bytes
            curr_pos = pf.tell()
            padding = (curr_pos + 15) & ~15
            if padding > curr_pos:
                pf.write(b'\0' * (padding - curr_pos))
            pf.write(data)
            
    # Cleanup original files
    for f_path in files_to_pack:
        f_path.unlink()
    
    print(f"Packed {item_count} files into {pack_filename}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="PGRAPH Dump Resource Manifest Generator")
    parser.add_argument("dump_dir", help="Path to the pgraph_dump directory")
    parser.add_argument("out_dir", help="Path where the resources and manifest should be created")
    parser.add_argument("--move", action="store_true", help="Move resource files instead of copying them")
    parser.add_argument("-v", "--verbose", action="store_true", help="Print summary statistics")
    args = parser.parse_args()
    
    generate_resource_manifest(args.dump_dir, args.out_dir, args.move, args.verbose)
