# import

Decision record, 2026-10-19. `script-dir | path-join workflows.cmdr | load` worked only for a script next to `workflows.cmdr`. A script elsewhere had to know where the example lived. `import` finds a module on a search path, as Python, Ruby, and Haskell do.

## Decision

`import NAME [as ALIAS]` follows Python's `import`, except where noted.

- **Search.** For each directory, in order, the first of `DIR/NAME/__init__.cmdr` and `DIR/NAME.cmdr` that exists is the module:
  1. The directory of the file in which `import` is written. In the REPL and `-c`, the current directory.
  2. Each entry of `CMDR_PATH`, separated by `:`.
  3. `~/.cmdr/lib`.
  4. `${prefix}/share/cmdr/lib`, fixed when cmdr is configured.
- **Run once.** The first import of a module, by its resolved path, runs it with no arguments. Later imports, from any file, bind its names again without running it. A failed import is not recorded, so the next one runs the module again.
- **Silent.** The module's values are not printed, as a Python module prints only what it prints explicitly.
- **Bind qualified names.** The importer gets `NAME.DEF` for each definition the module made, or `ALIAS.DEF` with `as`. When a module defines a name twice, the later definition is bound.
- **Hide the rest.** The module's own names, including its imports, are visible only inside it. Its definitions resolve names where they were written, so they keep working after the import.
- **Errors.** No module: `import NAME: not found in DIR:DIR:...`. A module that imports itself, directly or through others: `import NAME: circular import`. An error inside the module: `import NAME: PATH:LINE: ...`.
- `load` is unchanged: it runs a file every time, and its definitions enter the session unqualified.

## Departures from Python

| Python | cmdr | Because |
|-|-|-|
| `sys.path[0]` is the main script's directory | The importing file's directory | Each file finds the modules next to it. This matches `script-file`, which resolves to the file in which it is written |
| A circular import yields a partly initialized module | An error | cmdr has no module object to fill in |
| `import a.b` loads a submodule | A dot in NAME is an error | Nothing needs submodules yet; adding them later breaks no import that works now |
| `from M import f` | Absent | `import M as m` keeps names short; `from` would bring back unqualified collisions |

## Alternatives

| Choice | Over | Because |
|-|-|-|
| Qualified names, `aw.fix` | A prefix rule, `aw-fix`, checked at import | A prefix rule makes every module carry its prefix in every internal name. Moving to qualified names later would rewrite every module and every importer. Today there is one module and three importers |
| Qualified names | Flat names, as Ruby's `require` | `chain`, `target`, and `agent` in the example would shadow the importer's names without warning |
| Export every definition | Hide names starting with `_` | Python exports all names to `M.name`; `_` is a convention. One rule fewer |
| Package directories first | `NAME.cmdr` first | Python's order. A package's command-line script can share its name: `aw.cmdr` next to `aw/` imports the package |
| One alias layer per definition | One layer per import | Session layers hold one command each; lookup cost is the same as for that many `def`s |

## Mechanism

The session owns every layer it allocates in a list, and frees from that list at close. An import saves the head of the name chain, runs the module, and walks the layers the module pushed to collect its definitions. It then resets the head to the saved one, which unlinks the module's layers without freeing them, and pushes one alias layer per definition. An alias shares the module's compiled block.

The search path's last entry is compiled in. `cmake --install --prefix` with another prefix leaves cmdr searching the configured one.
