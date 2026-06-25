#!/usr/bin/env python3
"""Patch chunkingTool.c so files larger than INT_MAX do not overflow int lengths."""
from __future__ import annotations

import sys
from pathlib import Path

HELPER = """
static int chunk_tool_invoke_len(size_t nbytes) {
\tif (nbytes > (size_t)INT_MAX) {
\t\treturn INT_MAX;
\t}
\treturn (int)nbytes;
}
"""

RUN_CHUNKING_OLD = """\twhile (offset < buffer_size) {
\t\tint remaining = (int)(buffer_size - offset);
\t\tint chunk_size = run->chunk_fn(buffer + offset, remaining);

\t\tif (chunk_size <= 0 || chunk_size > remaining) {
\t\t\tCHUNK_TOOL_ERROR("Invalid chunk size %d at offset %zu, remaining=%d",
\t\t\t\t\tchunk_size,
\t\t\t\t\toffset,
\t\t\t\t\tremaining);"""

RUN_CHUNKING_NEW = """\twhile (offset < buffer_size) {
\t\tsize_t remaining = buffer_size - offset;
\t\tint chunk_size = run->chunk_fn(buffer + offset, chunk_tool_invoke_len(remaining));

\t\tif (chunk_size <= 0 || (size_t)chunk_size > remaining) {
\t\t\tCHUNK_TOOL_ERROR("Invalid chunk size %d at offset %zu, remaining=%zu",
\t\t\t\t\tchunk_size,
\t\t\t\t\toffset,
\t\t\t\t\tremaining);"""

BATCH_ACTIVE_OLD = "\t\t\t\tactive_sizes[active_count] = (int)(buffer_sizes[i] - offsets[i]);"
BATCH_ACTIVE_NEW = "\t\t\t\tactive_sizes[active_count] = chunk_tool_invoke_len(buffer_sizes[i] - offsets[i]);"

BATCH_SIMPLE_OLD = """\t\t\tint file_index = active_index[i];
\t\t\tint remaining = active_sizes[i];
\t\t\tint chunk_size = chunk_sizes[i];

\t\t\tif (chunk_size <= 0 || chunk_size > remaining) {
\t\t\t\tCHUNK_TOOL_ERROR("Invalid chunk size %d for file %s, remaining=%d",
\t\t\t\t\t\tchunk_size,
\t\t\t\t\t\tpaths->items[batch_start + file_index],
\t\t\t\t\t\tremaining);"""

BATCH_SIMPLE_NEW = """\t\t\tint file_index = active_index[i];
\t\t\tint chunk_size = chunk_sizes[i];
\t\t\tsize_t remaining = buffer_sizes[file_index] - offsets[file_index];

\t\t\tif (chunk_size <= 0 || (size_t)chunk_size > remaining) {
\t\t\t\tCHUNK_TOOL_ERROR("Invalid chunk size %d for file %s, remaining=%zu",
\t\t\t\t\t\tchunk_size,
\t\t\t\t\t\tpaths->items[batch_start + file_index],
\t\t\t\t\t\tremaining);"""

BATCH_SEGMENT_OLD = """\t\t\tint file_index = active_index[i];
\t\t\tint remaining = active_sizes[i];
\t\t\tint local_count = run->chunk_segment_batch_fn ? boundary_counts[i] : 1;
\t\t\tint j;

\t\t\tfor (j = 0; j < local_count; j++) {
\t\t\t\tint chunk_size = chunk_sizes[i * boundary_stride + j];

\t\t\t\tif (chunk_size <= 0 || chunk_size > remaining) {
\t\t\t\t\tCHUNK_TOOL_ERROR("Invalid chunk size %d for file %s, remaining=%d",
\t\t\t\t\t\t\tchunk_size,
\t\t\t\t\t\t\tpaths->items[batch_start + file_index],
\t\t\t\t\t\t\tremaining);"""

BATCH_SEGMENT_NEW = """\t\t\tint file_index = active_index[i];
\t\t\tint local_count = run->chunk_segment_batch_fn ? boundary_counts[i] : 1;
\t\t\tint j;

\t\t\tfor (j = 0; j < local_count; j++) {
\t\t\t\tint chunk_size = chunk_sizes[i * boundary_stride + j];
\t\t\t\tsize_t remaining = buffer_sizes[file_index] - offsets[file_index];

\t\t\t\tif (chunk_size <= 0 || (size_t)chunk_size > remaining) {
\t\t\t\t\tCHUNK_TOOL_ERROR("Invalid chunk size %d for file %s, remaining=%zu",
\t\t\t\t\t\t\tchunk_size,
\t\t\t\t\t\t\tpaths->items[batch_start + file_index],
\t\t\t\t\t\t\tremaining);"""


def insert_helper(text: str) -> str:
    if "chunk_tool_invoke_len" in text:
        return text
    marker = "static void chunk_tool_progress_clear(void)"
    if marker not in text:
        raise SystemExit("patch_chunking_tool_largefile: helper insert point not found")
    return text.replace(marker, HELPER + marker, 1)


def apply_replacements(text: str) -> str:
    replacements = [
        (RUN_CHUNKING_OLD, RUN_CHUNKING_NEW),
        (BATCH_ACTIVE_OLD, BATCH_ACTIVE_NEW),
        (BATCH_SEGMENT_OLD, BATCH_SEGMENT_NEW),
        (BATCH_SIMPLE_OLD, BATCH_SIMPLE_NEW),
    ]
    for old, new in replacements:
        if old in text:
            text = text.replace(old, new, 1)
    return text


def patch_file(path: Path) -> bool:
    original = path.read_text(encoding="utf-8")
    updated = insert_helper(original)
    updated = apply_replacements(updated)
    if updated == original:
        if "chunk_tool_invoke_len" not in updated:
            raise SystemExit(f"patch_chunking_tool_largefile: no changes applied to {path}")
        return False
    path.write_text(updated, encoding="utf-8")
    return True


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: patch_chunking_tool_largefile.py CHUNKING_TOOL.c ...", file=sys.stderr)
        return 2
    changed = False
    for arg in sys.argv[1:]:
        if patch_file(Path(arg)):
            changed = True
            print(f"[patch] large-file int fix applied: {arg}")
    return 0 if changed or len(sys.argv) > 1 else 1


if __name__ == "__main__":
    raise SystemExit(main())
