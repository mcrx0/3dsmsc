# ADR 0001: Use portable C++ modules around platform adapters

## Status

Accepted

## Context

The 3DS has constrained memory, cooperative threading, and a platform-specific audio output path. The application also needs host-side tests for filesystem-independent behavior.

## Decision

Use C++17 for portable modules and keep libctru, filesystem, input, APT, memory, and timing behind platform adapters. Keep decoder implementations behind a common decoder interface. Disable exceptions and RTTI for the 3DS build.

## Consequences

Core behavior can be tested on Linux without the 3DS SDK. Platform-specific failures stay localized. The interface must be kept small so UI and playback code do not depend on decoder internals.
