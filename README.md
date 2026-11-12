# aether-objects
Object system used in Aether library


## Raw telemetry integration

`AE_OBJECTS_RAW_TELE=ON` connects the tagged internal `AE_LOG_MACRO_TAGGED`
checkpoints to the `aether-tele` raw runtime. It adds a dependency on
`aether::tele`; local development can supply it with
`-DCPM_aether-tele_SOURCE=/path/to/aether-tele`.

Object telemetry uses its own `"objects"` space and stable explicit callsite
indices. Its public sink is declared in `aether-objects/telemetry.h`:

```cpp
auto& objects = ae::objects_tele::ObjectTeleSink::Instance();
objects.SetBackend(backend);  // shared_ptr<ae::tele::raw::IBackend>
objects.BindWriter(0);       // stable ID for this owner thread, before logging
// Run object/domain operations.
bool saved = objects.DrainCurrent();
```

The runtime mask independently controls logs, console, counts, timing and fields
for this space. The existing debug boundary is retained: `NDEBUG` or
`AE_NO_DEBUG_LOG=1` removes internal log calls, including argument evaluation.
A custom `AE_LOG_MACRO` supplied through `AE_LOGGER_INCLUDE` retains precedence.
The standalone default leaves raw telemetry disabled and keeps the old console
logger. No object IDs, stored object data or pointer ownership rules change.

Each thread owns its writer. `DrainAll()` serializes each checkpoint with its
writer and includes buffered data left by exited threads. A shared backend must
serialize complete blocks and implement all-or-retry acknowledgement. Bind
stable writer IDs to restore their statistics across restarts. When replacing
a backend, use `SetBackend`'s migration callback to transfer its accepted history
before the new binding is published. Do not recursively save the same domain
from its telemetry drain callback; telemetry storage and domain operations must
have independent lifetimes and IO paths.


## Transactional filesystem saves

The domain calls `IDomainStorageWriter::Commit()` after a version serializes
successfully, before notifying `Saved()`. Its default implementation preserves
custom/RAM writers. Direct users of the writer API must call `Commit()` when
using filesystem storage.

`FileSystemStdStorage` writes to an exclusively created temporary sibling and
publishes it with an atomic replacement only after flushing the complete file.
Abandoning a writer or failing serialization removes its own temporary file and
keeps the previous committed version. A crash can leave a temporary sibling;
ordinary readers ignore it and another writer never removes it. POSIX also
synchronizes the parent directory after replacement. If that final sync fails,
commit reports an error; the visible file is still a complete version, but its
durability acknowledgement is uncertain. Each version file is a transaction;
this does not make a whole multi-object graph one transaction.
