# Publishing

How to put a post on https://fourshades.pages.dev.

Everything lives in this folder. There is no CMS and no publish button — a post
is a Markdown file, and pushing it to GitHub puts it live in about two minutes.

## Write a post

Create a file under `site/src/content/posts/`. **The filename becomes the URL**,
so `first-cpu-instructions.md` is served at `/posts/first-cpu-instructions/`.
Use lowercase and hyphens, no spaces.

The file must start with this block, between the `---` lines:

```markdown
---
title: 'Getting the first CPU instructions running'
description: 'One sentence. Google shows this, and it goes in the RSS feed.'
date: 2026-09-15
---

Then write. Normal Markdown.
```

All three fields are required. If one is missing or misspelled, the build fails
loudly rather than publishing something broken — that is deliberate.

| Field | Notes |
|---|---|
| `title` | Wrap in single quotes. If the title itself contains an apostrophe, double it: `'Don''t'` |
| `description` | One sentence. This is the search result and the RSS summary — write it for a stranger |
| `date` | `YYYY-MM-DD`. Controls ordering; newest appears first |
| `draft` | Optional. See below |

## Not ready yet?

Add one line to the frontmatter:

```markdown
draft: true
```

The post is then invisible everywhere — not on the homepage, not in the RSS
feed, not at its own URL, not in the sitemap. Commit and push it as often as you
like; nothing is published until you delete that line.

Use this for anything half-written. Without it, every commit is a publication.

## Preview it privately

```bash
cd C:\GameMode\site
npm run dev
```

Open the URL it prints (usually http://localhost:4321). It reloads as you type.
This runs only on your machine — nobody else can reach it.

Press `Ctrl+C` to stop.

## Publish

```bash
cd C:\GameMode
git add .
git commit -m "post: first CPU instructions"
git push
```

Cloudflare rebuilds automatically. Give it ~2 minutes, then check the live site.

## Check you haven't broken anything

```bash
cd C:\GameMode\site
npm test
```

This type-checks, builds the site, and runs the test suite. **If it ends with
`Tests  N passed`, you are fine.** If it fails, do not push — read the error; it
names the file and line.

This also runs automatically on GitHub every time you push, so a broken build
emails you even if you forget to check.

## Send it to subscribers

Publishing does **not** email anyone. That is a separate, deliberate step.

1. Log in to Kit.
2. New broadcast, paste the post in, link back to the live URL.
3. Send.

Keeping it manual means you can publish something small without emailing
everyone about it.

## Then share it

Post the link where the readers already are — usually one or two of:

- **r/emudev** — people writing emulators. Small, on-topic, friendly to this
- **r/cpp** — C++ developers, openly sceptical of AI tooling, which makes an
  honest report interesting rather than annoying
- **Hacker News** — highest ceiling, least predictable
- **lobste.rs** — smaller, more technical

Link plus a genuine one-paragraph summary. Not marketing copy; these communities
detect it instantly and being seen as a self-promoter costs more than any single
post gains.

## Weekly rhythm

1. Build the emulator (~12 hrs)
2. Write up what happened, failures included (~2 hrs)
3. `npm test`, then `git push`
4. Send from Kit
5. Drop the link in one community

## Troubleshooting

**Build fails with a frontmatter error.** A required field is missing or
misspelled, or the date isn't `YYYY-MM-DD`. The error names the file.

**Post doesn't appear on the live site.** Check `draft: true` isn't still there.
Then check the Cloudflare build actually succeeded.

**Post appears but not in the RSS feed.** Almost always a `date` in the future.

**`npm run dev` won't start.** Run `npm install` in `site/` first.

## What is where

```
site/src/content/posts/     your posts — this is the only folder you need
site/src/pages/             the page templates
site/src/components/        <head> tags and the signup form
site/tests/                 the test suite
docs/superpowers/           the spec and plans behind the project
```
