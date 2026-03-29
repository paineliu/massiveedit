# Architecture Blueprint

## Layers

1. File Access Layer
- `LargeFileBackend` wraps buffered random reads.
- `ChunkCache` adds LRU chunk caching for streaming document access.

2. Text Model Layer
- `PieceTable` references original file ranges plus append-only add buffer.
- Original bytes are fetched on-demand via callback; no full-file load.

3. Index Layer
- `LineIndexer` incrementally scans bytes and lazily materializes line starts.

4. Search Layer
- `SearchEngine` provides basic plain/regex matches.
- `SearchThreadPool` executes chunked background searches.

5. Session Layer
- `DocumentSession` coordinates backend/model/index lifecycle.
- Exposes UI-friendly APIs (`lines`, `lineAt`, `saveAs`, `undo/redo`, `startSearch`).

6. UI Layer
- `MainWindow` wires menus and status.
- `LargeFileView` implements viewport-based rendering for visible lines only.

## Milestones

## M1 - Real Large File Open
- Done: replaced preview mode with chunked streaming reads + file-backed piece table.
- Done: added LRU cache for original file chunks.

## M2 - Editing Correctness
- Done: transactional edit commands and undo/redo stacks.
- Next: persistent durable operation log across restarts.

## M3 - Search/Replace at Scale
- Done: worker thread pool and chunked background search.
- Next: match navigation/highlighting and replace workflows.

## M4 - Platform Hardening
- CRLF/LF normalization policy.
- File lock conflict handling.
- Recovery and crash-safe temp files.
