# FourShades

A Game Boy emulator written in C++, built in public with AI coding agents.

Agents score 21% on Python tasks and 4% when C or C++ is involved. This project
tests what disciplined, spec-first agentic development can actually do in that
gap — measured against the public Game Boy test ROMs, published honestly,
failures included.

The test-ROM pass rate is the scoreboard. It starts at zero.

- Specs: `docs/superpowers/specs/`
- Plans: `docs/superpowers/plans/`
- Site: `site/` — the writing lives at https://fourshades.pages.dev

## Site

```bash
cd site
npm install
npm test    # astro build && vitest run
npm run dev
```
