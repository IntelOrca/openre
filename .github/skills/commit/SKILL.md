---
name: commit
description: 'Commits the current changes. Run this whenever the user asks to commit the current changes.'
---

Before committing, ensure format.bat has been run.

## Commit message example format
```
Implement draw_line_flat/gourad

This includes:
- init_system 0x004C3F10
- init_input 0x0043B950
- init_global 0x004C4D70
- memclr 0x00502D70
- line_work_init 0x004C3C60

And added ... to the GameTable.
```

- DO NOT ADD A co-author trailer!
- DO NOT mention "code reviewer" or the incremental changes, just mention the final changes as if they were done in one shot.
