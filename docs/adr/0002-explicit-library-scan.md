# ADR 0002: Scan only after an explicit user action

## Status

Accepted

## Context

Automatically scanning the SD card would consume resources without user intent and could make startup unpredictable on large collections.

## Decision

Scan only the configured default folder or the user-selected music folders after an explicit user action. Show scan progress and allow cancellation. A successful empty scan presents the exact recovery instruction to run `Settings > Library > Full scan again`.

## Consequences

Startup is predictable and battery use is lower. The interface must provide a clear way to select a root and start a scan.

## Amendment

The user may select several music folders anywhere on the SD card (stored in `sdmc:/3dsmsc/folders.txt`). With none selected the default folder is scanned. The scan stays explicit and cancellable.
