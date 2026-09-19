# Semcode Indexer (`semcode_indexer`)

## Goals
The `semcode_indexer` class implements a high-performance, embedded SQLite indexing engine for C/C++ repositories (such as the Linux kernel). It ingests symbol dumps (functions, types, enums, structs, typedefs, and outgoing call relationships) from the `semcode` static analysis tool, writes them to persistent SQLite B-tree databases (`.turbostar/semcode_<git_head>.db`), and provides sub-millisecond query interfaces for symbol definitions, type hierarchies, and outgoing call graphs.

## Architecture and Constraints
- **Zero-Latency In-Process Queries**: Replaces slow per-turn CLI scraping with direct SQLite C API lookups, accelerating symbol resolution from 2,500ms down to <1ms.
- **High-Throughput Streaming Ingestion**:
  - Ingests streaming JSON outputs from `semcode` CLI tools using a single transactional batch.
  - Tuned with SQLite optimizations: WAL journal mode (`PRAGMA journal_mode=WAL`), memory synchronous mode (`PRAGMA synchronous=OFF`), and expanded 64MB memory page caches (`PRAGMA cache_size=-64000`), achieving ingestion speeds exceeding 170,000 records/second.
- **Thread Safety & Resource Lifecycle**:
  - Guarded by `mutable std::mutex mutex_` for thread-safe concurrent queries by multiple background agent threads.
  - Prepared statement cache: reuses compiled statements (`sqlite3_stmt*`) for common lookup patterns (`query_function_by_name`, `query_types_by_name`, `query_outgoing_calls`) to avoid query compilation overhead.
- **Automatic Stale Database Pruning**:
  - Implements LRU pruning (`max_dbs`) to automatically clean up obsolete database files as developers switch git branches, preventing disk bloat.

## Lessons Learned
- **SQLite Parameter Rebinding**: Reusing prepared SQLite statements without calling `sqlite3_reset()` and `sqlite3_clear_bindings()` can cause parameter leakage and subtle query errors. Always reset statements immediately after row iteration.
- **Batch Transaction Wrapping**: Inserting 1,000,000+ symbol records individually without wrapping them in `BEGIN TRANSACTION` and `COMMIT` incurs enormous filesystem fsync overhead. Single-transaction batch ingestion reduces index generation time from minutes to under 10 seconds.
- **Underlying Type Separation**: Restricting `underlying_type` strictly to `typedef` and `alias` kinds prevents member types from being incorrectly treated as underlying type definitions, keeping codemap symbol resolution accurate.
