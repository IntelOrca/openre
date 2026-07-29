---
name: decompile-batch
description: 'Decompile several RE2 functions (in parallel) into hand-written C++ code, replacing its interop::call wrapper'
argument-hint: 'Multiple address (e.g., 0x00431000, 0x004D2500)'
---

# Batch Decompile

## Step 1

Spawn multiple sub agents, one per function to decompile. Each sub agent shall use the decompile skill.
You expect each sub agent to return a fully complete C++ implementation of their designated function.
You the orchastrator does NOT decompile or analyze anything.

## Step 2

Integrate each sub agent's output into the codebase.

## Step 3

Spawn a sub agent to run an independant code review on the complete output. The code review must not decompile anything other than to double check any specific uncertainties. This sub agent must apply it's suggestions.

# Step 4

Ask the user to review the final output and approve it.
