# Engine access

The host loads game modules and owns native hooks. Plugins borrow capabilities
from `PluginContext::engine`. A borrowed interface, function pointer, or entity
view must not outlive its module or world.

## Modules and interfaces

`MappedModuleImage::ForModule` borrows an already loaded module's address and
bytes. `ResolveEngineInterface` calls that module's `CreateInterface` export
with the requested registration name. Neither operation loads, retains, or
unloads a module. Missing modules, exports, and registrations return errors.

CMake selects the Windows implementation or an unsupported-host implementation
that returns errors. Callers use the same API on every host. Windows ABI calls
and inline detours remain guarded where they need Windows types or SafetyHook.
Portable parsing, callback dispatch, and fixture tests run without game modules.

## Signatures and addresses

`ParseSignature` and `SignatureScan` operate on borrowed byte spans.
`ResolveScannedSymbol`, `DecodeRelativeCall`, and `DecodeRelativeLea` share
one unique-match check: missing and ambiguous signatures fail with the probe's
name. The relative decoders check that the complete instruction fits before
reading its signed displacement. Probe definitions keep each pattern, instruction
offset, and native ABI fact beside the capability that uses it.

An address is evidence of a pattern match, not proof that an arbitrary function
signature is safe to call. Callers must use the recorded calling convention and
layout. Schema offsets and virtual slots remain separate resolution paths.

## Hook lifetime

`VtableSlotHook` patches one entry in the module's original dispatch table.
It never copies the table or changes an object's table pointer, so unnamed slots
remain available to the engine. The caller supplies a valid slot and keeps the
table alive until restoration. Hooks stacked on the same slot must be removed
in reverse installation order.

`ThunkOwner` couples a slot hook to its callback cleanup. Destruction and move
assignment restore the old slot before clearing callback state; moving transfers
both duties. Callback holders declare their owner after borrowed handler storage
so dispatch is restored before that storage is destroyed.

Inline detours use SafetyHook directly because native code can call a function
without going through its virtual slot. Each hook keeps its own original-call
signature, callback ordering, and thread contract. Installation and teardown
follow the host's engine lifecycle; the RAII owners do not synchronize concurrent
hook installation or wait for callbacks on other threads.

## Checks

The SDK tests exercise unique signature matching, relative instruction bounds,
dispatch-table preservation, move ownership, and restore-before-clear ordering.
The independent hello plugin checks the installed SDK and plugin admission.
Windows builds check native code paths; a live game run is needed to validate
patterns and ABI layouts against a new game binary.
