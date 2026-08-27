# RE1 port notes

Working notes for the long-lived RE1 -> RE2-engine port. Everything in this
directory is branch-local: it lives on the `re1-port` branch and is stripped
whenever progress is merged back into the source branch (see
`scripts/re1-merge.sh`).

## Index

- `re1-port-handoff.md` - the original handoff: RE1 event VM opcode reference,
  Ghidra DB access, file layout, and what the port means for the engine.
- `re1-rdt-mapping.md` - RE1 RDT -> RE2 RDT chunk/field mapping (first task).

## Workflow reminder

Work happens on `re1-port`. To merge progress back into the source branch
(currently `ecs`, may become `master`) without bringing these notes along:

    scripts/re1-merge.sh [source-branch]