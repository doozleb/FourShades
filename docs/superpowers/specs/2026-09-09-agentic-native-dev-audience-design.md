# Building an audience around agentic development of hard native C++

**Date:** 2026-09-09
**Status:** Approved design, ready for implementation planning
**Timeline decision (2026-09-09):** Confirmed. First revenue is expected in
6-9 months, not 1-3. The original 1-3 month target is explicitly withdrawn, and
the trade described under "Consequence, stated plainly" is accepted.

## Goal

Build a revenue-generating position for a solo, part-time developer (10-15 hrs/week)
with no existing audience, no insider industry domain, and web-stack willingness.

Revenue is the goal. Audience is the chosen means, because the research below
established that it is the only input in this market that cannot be cloned.

## Problem: why the obvious approaches are dead

The original plan was the standard playbook: a free tool that ranks in search,
with a paid tier behind it. Ten niches were researched adversarially. Every one
was saturated, and the saturation had a single cause.

| Niche | Finding |
|---|---|
| Bank statement to CSV | 6+ exact-match-domain clones; per-bank OCR is a permanent support treadmill |
| Accessibility statements | Free lead-magnets from funded overlay vendors |
| AI/GEO visibility tracking | 20+ tools at $95-699/mo, VC-funded |
| Construction calculators | 8 near-identical calculator farms |
| License/permit renewal tracking | 5 identical micro-SaaS |
| Property tax appeals | 5 indie packet-sellers at $49-79 |
| Factur-X / Peppol validators | 12+ clones, including in French |
| Compiler diagnostics for agents | Occupied by open source AND Microsoft, free |
| Game runtime loop for agents | godot-agent-loop, NodeMori BugHunter, Unreal MCP (305 tools) |
| Game dev tooling generally | Entire market only $0.56B, split across engines |

Two recommendations were made during research and both were killed by further
research. They are recorded here so they are not re-litigated:

1. **E-invoicing validation API.** Killed because the EN 16931 Schematron rules
   are published free and open source by CEN, Mustangproject and KoSIT already
   ship open-source validators with REST APIs, ecosio and peppolvalidator give
   the API away free, and the actual margin sits behind French DGFiP
   accreditation (~70 platforms already accredited). Correctness was already free.
2. **Compiler-diagnostics tooling for C++ agents.** Killed because Microsoft
   ships its own Binlog MCP Server, and mcp-cpp, clangd MCP servers and
   mcp-debugger already exist as free open source.

### The structural conclusion

> Any product specifiable from the outside - without insider knowledge,
> proprietary data, or a licence - has already been built by many people who
> also have AI coding tools.

Four constraints were in play: no domain, no audience, revenue in 1-3 months,
part-time product (not services). All four cannot hold simultaneously. The
constraint chosen for release is **"no audience"**, because audience is the only
one of the four whose scarcity cannot be cloned, and because it makes the other
three tractable afterwards.

**Consequence, stated plainly:** releasing the audience constraint necessarily
also releases the 1-3 month revenue target. Audience-building does not produce
revenue on that timeline; direct sponsorship needs roughly 1,000 subscribers,
which is a 6-12 month horizon at one piece per week. This design trades speed to
first dollar for a position that compounds and cannot be cloned. If revenue
inside 3 months is non-negotiable, this is the wrong design and the correct move
is to release the "product not services" constraint instead - selling the
engineering directly, which pays in weeks.

## The opportunity

The evidence for the chosen position is specific and measured:

- On the GSO benchmark, agents score **21% on Python tasks but 4% when C/C++ is
  involved** - a 5x capability collapse.
- Stated root cause: development environments emit human-oriented, not
  machine-consumable, feedback, so agents struggle to interpret build errors.
- Agents cannot close the runtime loop: they edit files but cannot run the
  program and read live errors.
- Enterprise reality: legacy codebases run at 35% agent capability, multi-file
  refactors at 42%, and **88% of agent pilots never reach production**.
- Audience size: 85% of developers used AI coding tools regularly by mid-2026.
- Spec-driven development went mainstream in 2026 (GitHub Spec Kit, AWS Kiro,
  Microsoft, DeepLearning.AI, Cursor, Antigravity) - and **every published
  worked example is web/Python/TypeScript.**

So: the largest audience in software, an acute and *quantified* pain, and no
independent practitioner voice. Searches for who covers AI agents for C++ and
systems work return only generic listicles and vendor content. The one credible
artefact is ClickHouse's account of a year of agentic coding on a large C++
codebase - written about a company, by a journalist, once.

## The position

> The only sustained, independent, public account of driving hard native C++
> with AI agents - spec-first, test-first, with receipts.

Not opinions about AI coding. A real, difficult, non-web codebase built in the
open, including the failures, with the specs, diffs and commits published
alongside every claim.

### Why this is defensible when nothing else was

Every rejected idea died because it could be cloned in a weekend. This cannot,
because it requires having actually done the work over time. Competitors are
also structurally aimed elsewhere: they write about TypeScript CRUD apps because
that is where the volume is. The 4%-vs-21% gap is the territory they abandoned.

Critically, the differentiator is **not** a private methodology. The workflow
used is a publicly available plugin, which makes the result *reproducible* for
readers: they can install the same tooling. The variable under test is the hard
part - native C++, CMake, MSVC, cycle-accurate timing - not secret sauce.

## The vehicle: a Game Boy emulator

A new C++ codebase, built in public from the first commit, chosen against four
criteria:

1. **Objective public test suite.** blargg and mooneye test ROMs turn "did the
   agent get it right?" into a number rather than an opinion.
2. **Visible output.** Screenshots and video of games booting travel; database
   logs do not.
3. **Genuinely hard for agents.** Cycle-accurate timing, bit-level operations
   and memory behaviour - precisely where the research shows agents are weakest.
4. **Finishable part-time.** Achievable at 10-15 hrs/week over a long horizon.

The prior project, Litharia (18,418 lines of C++20/SFML, 290 commits, 35 specs,
30 plans, 80 review diffs, built 2026-07-14 to 2026-07-27), is **not** the
vehicle. It is dormant, and a retrofitted record is weaker than one designed for
the purpose. It remains available as supporting evidence and back-catalogue
material.

### The content engine

The test-ROM pass rate is the recurring narrative device: a public, honest,
rising number ("agents took this from 0 to 412 of 1,300 tests passing"), with
every regression and failure published rather than hidden.

## Content and SEO plan

Hub-and-spoke, targeting queries with real intent and no credible answer:

| Cluster | Example targets |
|---|---|
| Native + agents | `Claude Code C++ codebase`, `AI agent CMake build errors`, `MSVC diagnostics AI context` |
| Spec-driven, but real | `spec driven development C++`, `spec-driven development large codebase` |
| Verification | `how to test AI generated C++`, closing the runtime loop, regression discipline |
| Honest reporting | Documented failures with commits - what vendor blogs structurally cannot publish |

A full SERP-overlap cluster analysis is to be run against these seeds during
implementation planning, replacing these assumed groupings with measured ones.

## Monetization ladder

Deliberately sequenced. The product is chosen last, from knowledge, rather than
guessed at first - which is what killed the ten rejected niches.

1. **Months 1-3: audience only, nothing for sale.** Direct sponsorship becomes
   viable at roughly 1,000 subscribers.
2. **Then, whichever the audience reveals it needs:** sponsorships ($500-2,000
   per piece at 5k audience), paid deep-dives, templates, a course ($97-2,000),
   or ambassador retainers ($500-3,000/mo). Open-source maintainers in adjacent
   spaces earn $500-15,000/mo via GitHub Sponsors.
3. **Later: a tool**, built with distribution and domain knowledge already in
   hand.

## Scope and cadence

- Emulator work proceeds as the primary activity; writing is a byproduct of work
  already done, not a second project.
- One substantial published piece per week.
- Every claim ships with its artefact: the spec, the diff, the commit, the test
  result.

## Non-goals

- Not shipping a commercial game.
- Not building a tool or SaaS first.
- Not writing generic "AI coding" content - that market is saturated and the
  position would be lost.
- Not claiming a proprietary methodology.

## Risks and falsifiability

| Risk | How we would know | Response |
|---|---|---|
| Position does not land | At 90 days: under ~300 engaged subscribers, or no piece above ~1,000 views | Re-aim; the constraint choice returns to the table |
| Model risk: agents get good at C++ | Benchmark movement on native-code tasks, reviewed quarterly | Migrate position toward verification and architecture |
| Cadence failure | Two consecutive weeks without a published piece | Reduce piece size before reducing frequency |
| Emulator stalls like Litharia did | Test-ROM pass rate flat for 3+ weeks | The scoreboard makes this visible early by design |

**Leading indicator:** inbound questions from native/C++ developers. If people
begin asking how to do this, the position is working before traffic confirms it.

## Open decisions

- Publishing platform and domain.
- Whether the emulator repository is public from commit one (recommended) and
  under which licence.
- Whether this venture spec lives in the emulator repository long-term or moves
  to its own.

## Next step

Invoke the writing-plans skill to produce an implementation plan covering the
publishing foundation and the emulator project setup. These are two distinct
workstreams and the plan should sequence them explicitly.
