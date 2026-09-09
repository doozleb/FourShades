# Publishing Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up an owned, fast, SEO-correct content site with RSS and email capture, deployed live, ready to publish the first piece.

**Architecture:** A static Astro site in `site/` inside the public `FourShades` monorepo, alongside the emulator source and the published specs/plans that serve as the project's receipts. Content lives as Markdown in a typed content collection. The build output in `dist/` is asserted against by Vitest, so "the site is correct" is a test run, not an opinion. Hosting is Cloudflare Pages; the newsletter is Kit, which owns the subscriber list independently of the site and is free to 10,000 subscribers.

**Tech Stack:** Astro 7, Vitest, cheerio, `@astrojs/rss`, `@astrojs/sitemap`, Cloudflare Pages, Kit. npm (not pnpm). Node 24.15.0, npm 11.12.1, git 2.54.0.

## Global Constraints

- The repository is **public from commit one**. The commit history is the evidence; a private repo discards the entire proof.
- Specs and plans in `docs/superpowers/` are published, not hidden. They are content.
- The site is served from a **domain the operator owns**. Rented audiences (Medium, DEV) do not compound.
- Every post ships with its artefact: spec, diff, commit, or test result.
- Nothing is offered for sale during months 1-3.
- Node 24.15.0 / npm 11.12.1 are the toolchain. Do not introduce pnpm, yarn, or the `gh` CLI — none are installed.
- Astro must be installed as `astro@^7`. The APIs used in this plan (`glob()` loader, `src/content.config.ts`, `render()` from `astro:content`, `post.id` rather than `post.slug`) are the current Astro 6/7 APIs — every breaking change since v4 moved toward them, so the plan code is unchanged by this. Astro 5 is two majors behind and carries known esbuild/sharp advisories.
- All commands run from `C:\GameMode` unless a step says otherwise.

---

## Prerequisites (operator actions, not code)

These need a human, but no money — every account below is free, and the domain is already owned.

**Tasks 1-4 need none of them.** Kit is required at **Task 5**, and Cloudflare plus the GitHub repository at **Task 6**.

**Note:** The Kit form ID is `9900055`. Task 5's test asserts that exact ID appears in the form action, so a leftover placeholder fails the build rather than passing silently.

- [ ] Create a free **Cloudflare** account (hosting).
- [x] Kit account created; inline form ID is **9900055**.
- [x] GitHub account: **DoozleB**. Create a new **public** repository named `FourShades`.
- [x] Domain: **doozleb.com**, already owned. Attached in Task 7; Tasks 1-6 run on a `*.pages.dev` subdomain first, so nothing is blocked.

---

### Task 1: Repository and Astro scaffold that builds

**Files:**
- Create: `C:\GameMode\.gitignore`
- Create: `C:\GameMode\site\package.json`
- Create: `C:\GameMode\site\astro.config.mjs`
- Create: `C:\GameMode\site\vitest.config.ts`
- Create: `C:\GameMode\site\src\pages\index.astro`
- Test: `C:\GameMode\site\tests\build.test.ts`

**Interfaces:**
- Consumes: nothing.
- Produces: a built site at `site/dist/`, an `npm test` script in `site/package.json` that runs `astro build` then `vitest run`. Every later task's tests rely on both.

- [ ] **Step 1: Initialise the repository**

```bash
cd /c/GameMode
git init -b main
```

- [ ] **Step 2: Write `.gitignore`**

Create `C:\GameMode\.gitignore`:

```gitignore
node_modules/
dist/
.astro/
.env
.DS_Store
build/
```

- [ ] **Step 3: Write `site/package.json`**

Create `C:\GameMode\site\package.json`:

```json
{
  "name": "fourshades-site",
  "type": "module",
  "version": "0.1.0",
  "private": true,
  "scripts": {
    "dev": "astro dev",
    "build": "astro build",
    "preview": "astro preview",
    "test": "astro build && vitest run"
  }
}
```

- [ ] **Step 4: Install dependencies**

```bash
cd /c/GameMode/site
npm install astro@^7 @astrojs/rss @astrojs/sitemap
npm install -D vitest cheerio
```

Expected: `node_modules/` created, no `ERR!` lines.

- [ ] **Step 5: Write `site/astro.config.mjs`**

The `site` value is required for RSS and sitemap absolute URLs. It is replaced with the real domain in Task 7.

```js
import { defineConfig } from 'astro/config';

export default defineConfig({
  site: 'https://fourshades.pages.dev',
});
```

- [ ] **Step 6: Write `site/vitest.config.ts`**

```ts
import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    environment: 'node',
    include: ['tests/**/*.test.ts'],
  },
});
```

- [ ] **Step 7: Write the failing test**

Create `C:\GameMode\site\tests\build.test.ts`:

```ts
import { readFileSync } from 'node:fs';
import { load } from 'cheerio';
import { describe, it, expect } from 'vitest';

describe('homepage', () => {
  it('builds and renders the site name in an h1', () => {
    const html = readFileSync('dist/index.html', 'utf8');
    const $ = load(html);
    expect($('h1').text()).toContain('FourShades');
  });
});
```

- [ ] **Step 8: Run the test to verify it fails**

```bash
cd /c/GameMode/site && npm test
```

Expected: FAIL. With no pages yet, the build produces no `dist/index.html`, so the
test fails with `ENOENT: no such file or directory, open 'dist/index.html'`. (If
Astro instead aborts the build for having no pages, that is also an acceptable
failure for this step — either way, the test is red.)

- [ ] **Step 9: Write the minimal homepage**

Create `C:\GameMode\site\src\pages\index.astro`:

```astro
---
const title = 'FourShades';
---

<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>{title}</title>
  </head>
  <body>
    <h1>{title}</h1>
    <p>Building a Game Boy emulator in C++ with AI agents, in public.</p>
  </body>
</html>
```

- [ ] **Step 10: Run the test to verify it passes**

```bash
cd /c/GameMode/site && npm test
```

Expected: PASS, 1 test.

- [ ] **Step 11: Commit**

```bash
cd /c/GameMode
git add .gitignore site/package.json site/package-lock.json site/astro.config.mjs site/vitest.config.ts site/src/pages/index.astro site/tests/build.test.ts
git commit -m "feat: scaffold Astro site with build assertion test"
```

---

### Task 2: Typed content collection and post rendering

**Files:**
- Create: `C:\GameMode\site\src\content.config.ts`
- Create: `C:\GameMode\site\src\content\posts\hello-scoreboard.md`
- Create: `C:\GameMode\site\src\pages\posts\[...slug].astro`
- Test: `C:\GameMode\site\tests\posts.test.ts`

**Interfaces:**
- Consumes: the `npm test` script from Task 1.
- Produces: a `posts` collection whose entries have `data.title` (string), `data.description` (string), `data.date` (Date), and `id` (the slug). Posts render at `/posts/<id>/`. Tasks 3, 4 and 5 all read this collection.

- [ ] **Step 1: Write the failing test**

Create `C:\GameMode\site\tests\posts.test.ts`:

```ts
import { readFileSync } from 'node:fs';
import { load } from 'cheerio';
import { describe, it, expect } from 'vitest';

describe('post page', () => {
  it('renders the post title and body', () => {
    const html = readFileSync('dist/posts/hello-scoreboard/index.html', 'utf8');
    const $ = load(html);
    expect($('h1').text()).toContain('Starting the scoreboard at zero');
    expect($('body').text()).toContain('test ROMs');
  });
});
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /c/GameMode/site && npm test
```

Expected: FAIL with `ENOENT: no such file or directory, open 'dist/posts/hello-scoreboard/index.html'`.

- [ ] **Step 3: Define the content collection**

Create `C:\GameMode\site\src\content.config.ts`:

```ts
import { defineCollection, z } from 'astro:content';
import { glob } from 'astro/loaders';

const posts = defineCollection({
  loader: glob({ pattern: '**/*.md', base: './src/content/posts' }),
  schema: z.object({
    title: z.string(),
    description: z.string(),
    date: z.coerce.date(),
  }),
});

export const collections = { posts };
```

- [ ] **Step 4: Write the first post**

Create `C:\GameMode\site\src\content\posts\hello-scoreboard.md`:

```markdown
---
title: 'Starting the scoreboard at zero'
description: 'Why I am building a Game Boy emulator in C++ with AI agents, and publishing every number.'
date: 2026-09-09
---

Agents score 21% on Python tasks and 4% when C or C++ is involved. That gap is
the whole reason for this project.

I am building a Game Boy emulator in C++, driven by AI coding agents, and
publishing the result against the public test ROMs. The pass count is the
scoreboard, and it starts at zero.

Every claim here ships with its artefact: the spec, the diff, the commit, the
test output. Including the failures.
```

- [ ] **Step 5: Write the post route**

Create `C:\GameMode\site\src\pages\posts\[...slug].astro`:

```astro
---
import { getCollection, render } from 'astro:content';

export async function getStaticPaths() {
  const posts = await getCollection('posts');
  return posts.map((post) => ({
    params: { slug: post.id },
    props: { post },
  }));
}

const { post } = Astro.props;
const { Content } = await render(post);
---

<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>{post.data.title}</title>
  </head>
  <body>
    <h1>{post.data.title}</h1>
    <time datetime={post.data.date.toISOString()}>
      {post.data.date.toISOString().slice(0, 10)}
    </time>
    <Content />
  </body>
</html>
```

- [ ] **Step 6: Run the test to verify it passes**

```bash
cd /c/GameMode/site && npm test
```

Expected: PASS, 2 tests.

- [ ] **Step 7: Commit**

```bash
cd /c/GameMode
git add site/src/content.config.ts site/src/content/posts/hello-scoreboard.md "site/src/pages/posts/[...slug].astro" site/tests/posts.test.ts
git commit -m "feat: add typed posts collection and post rendering"
```

---

### Task 3: RSS feed

**Files:**
- Create: `C:\GameMode\site\src\pages\rss.xml.ts`
- Test: `C:\GameMode\site\tests\rss.test.ts`

**Interfaces:**
- Consumes: the `posts` collection from Task 2 (`data.title`, `data.description`, `data.date`, `id`).
- Produces: a feed at `/rss.xml`. Task 7 updates the absolute URLs when the domain changes.

- [ ] **Step 1: Write the failing test**

Create `C:\GameMode\site\tests\rss.test.ts`:

```ts
import { readFileSync } from 'node:fs';
import { describe, it, expect } from 'vitest';

describe('rss feed', () => {
  it('lists the first post with an absolute link', () => {
    const xml = readFileSync('dist/rss.xml', 'utf8');
    expect(xml).toContain('<title>Starting the scoreboard at zero</title>');
    expect(xml).toContain('/posts/hello-scoreboard/');
    expect(xml).toContain('<link>https://');
  });
});
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /c/GameMode/site && npm test
```

Expected: FAIL with `ENOENT: no such file or directory, open 'dist/rss.xml'`.

- [ ] **Step 3: Write the feed endpoint**

Create `C:\GameMode\site\src\pages\rss.xml.ts`:

```ts
import rss from '@astrojs/rss';
import { getCollection } from 'astro:content';
import type { APIContext } from 'astro';

export async function GET(context: APIContext) {
  const posts = await getCollection('posts');
  const sorted = posts.sort(
    (a, b) => b.data.date.valueOf() - a.data.date.valueOf(),
  );

  return rss({
    title: 'FourShades',
    description:
      'Building a Game Boy emulator in C++ with AI agents, in public.',
    site: context.site!,
    items: sorted.map((post) => ({
      title: post.data.title,
      description: post.data.description,
      pubDate: post.data.date,
      link: `/posts/${post.id}/`,
    })),
  });
}
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd /c/GameMode/site && npm test
```

Expected: PASS, 3 tests.

- [ ] **Step 5: Commit**

```bash
cd /c/GameMode
git add site/src/pages/rss.xml.ts site/tests/rss.test.ts
git commit -m "feat: add RSS feed"
```

---

### Task 4: SEO head and sitemap

**Files:**
- Create: `C:\GameMode\site\src\components\BaseHead.astro`
- Modify: `C:\GameMode\site\src\pages\index.astro` (replace `<head>` contents)
- Modify: `C:\GameMode\site\src\pages\posts\[...slug].astro` (replace `<head>` contents)
- Modify: `C:\GameMode\site\astro.config.mjs` (register the sitemap integration)
- Test: `C:\GameMode\site\tests\seo.test.ts`

**Interfaces:**
- Consumes: the `posts` collection from Task 2.
- Produces: `BaseHead.astro`, which accepts props `title` (string), `description` (string) and optional `canonicalPath` (string, defaults to `Astro.url.pathname`). Every page from here on uses it.

- [ ] **Step 1: Write the failing test**

Create `C:\GameMode\site\tests\seo.test.ts`:

```ts
import { readFileSync, existsSync } from 'node:fs';
import { load } from 'cheerio';
import { describe, it, expect } from 'vitest';

describe('seo', () => {
  it('gives the post a description, canonical and og:title', () => {
    const html = readFileSync('dist/posts/hello-scoreboard/index.html', 'utf8');
    const $ = load(html);
    expect($('meta[name="description"]').attr('content')).toContain(
      'Game Boy emulator',
    );
    expect($('link[rel="canonical"]').attr('href')).toContain(
      '/posts/hello-scoreboard/',
    );
    expect($('meta[property="og:title"]').attr('content')).toBe(
      'Starting the scoreboard at zero',
    );
  });

  it('emits a sitemap', () => {
    expect(existsSync('dist/sitemap-index.xml')).toBe(true);
  });
});
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /c/GameMode/site && npm test
```

Expected: FAIL — `meta[name="description"]` is undefined, and `dist/sitemap-index.xml` does not exist.

- [ ] **Step 3: Write `BaseHead.astro`**

Create `C:\GameMode\site\src\components\BaseHead.astro`:

```astro
---
interface Props {
  title: string;
  description: string;
  canonicalPath?: string;
}

const { title, description, canonicalPath } = Astro.props;
const canonical = new URL(canonicalPath ?? Astro.url.pathname, Astro.site);
---

<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>{title}</title>
<meta name="description" content={description} />
<link rel="canonical" href={canonical.href} />
<meta property="og:type" content="website" />
<meta property="og:title" content={title} />
<meta property="og:description" content={description} />
<meta property="og:url" content={canonical.href} />
<link rel="alternate" type="application/rss+xml" title="FourShades" href="/rss.xml" />
```

- [ ] **Step 4: Register the sitemap integration**

Replace the contents of `C:\GameMode\site\astro.config.mjs`:

```js
import { defineConfig } from 'astro/config';
import sitemap from '@astrojs/sitemap';

export default defineConfig({
  site: 'https://fourshades.pages.dev',
  integrations: [sitemap()],
});
```

- [ ] **Step 5: Use `BaseHead` on the post page**

Replace the contents of `C:\GameMode\site\src\pages\posts\[...slug].astro`:

```astro
---
import { getCollection, render } from 'astro:content';
import BaseHead from '../../components/BaseHead.astro';

export async function getStaticPaths() {
  const posts = await getCollection('posts');
  return posts.map((post) => ({
    params: { slug: post.id },
    props: { post },
  }));
}

const { post } = Astro.props;
const { Content } = await render(post);
---

<html lang="en">
  <head>
    <BaseHead title={post.data.title} description={post.data.description} />
  </head>
  <body>
    <h1>{post.data.title}</h1>
    <time datetime={post.data.date.toISOString()}>
      {post.data.date.toISOString().slice(0, 10)}
    </time>
    <Content />
  </body>
</html>
```

- [ ] **Step 6: Use `BaseHead` on the homepage**

Replace the contents of `C:\GameMode\site\src\pages\index.astro`:

```astro
---
import { getCollection } from 'astro:content';
import BaseHead from '../components/BaseHead.astro';

const posts = (await getCollection('posts')).sort(
  (a, b) => b.data.date.valueOf() - a.data.date.valueOf(),
);
---

<html lang="en">
  <head>
    <BaseHead
      title="FourShades"
      description="Building a Game Boy emulator in C++ with AI agents, in public."
    />
  </head>
  <body>
    <h1>FourShades</h1>
    <p>Building a Game Boy emulator in C++ with AI agents, in public.</p>
    <ul>
      {
        posts.map((post) => (
          <li>
            <a href={`/posts/${post.id}/`}>{post.data.title}</a>
          </li>
        ))
      }
    </ul>
  </body>
</html>
```

- [ ] **Step 7: Run the test to verify it passes**

```bash
cd /c/GameMode/site && npm test
```

Expected: PASS, 5 tests.

- [ ] **Step 8: Commit**

```bash
cd /c/GameMode
git add site/src/components/BaseHead.astro site/astro.config.mjs site/src/pages/index.astro "site/src/pages/posts/[...slug].astro" site/tests/seo.test.ts
git commit -m "feat: add SEO head component and sitemap"
```

---

### Task 5: Newsletter capture

**Files:**
- Create: `C:\GameMode\site\src\components\Subscribe.astro`
- Modify: `C:\GameMode\site\src\pages\index.astro` (add `<Subscribe />` below the intro paragraph)
- Modify: `C:\GameMode\site\src\pages\posts\[...slug].astro` (add `<Subscribe />` after `<Content />`)
- Test: `C:\GameMode\site\tests\subscribe.test.ts`

**Interfaces:**
- Consumes: `BaseHead` conventions from Task 4.
- Produces: `Subscribe.astro`, which takes no props and posts to Kit inline form `9900055`. Note that Kit's email field must be named `email_address`, not `email` — a form using `email` silently fails to subscribe anyone.

- [ ] **Step 1: Write the failing test**

Create `C:\GameMode\site\tests\subscribe.test.ts`:

```ts
import { readFileSync } from 'node:fs';
import { load } from 'cheerio';
import { describe, it, expect } from 'vitest';

describe('subscribe form', () => {
  it('posts an email_address field to Kit from the homepage', () => {
    const html = readFileSync('dist/index.html', 'utf8');
    const $ = load(html);
    const form = $('form[data-testid="subscribe"]');
    expect(form.attr('action')).toContain('kit.com');
    expect(form.attr('action')).toContain('9900055');
    expect(form.attr('method')?.toLowerCase()).toBe('post');
    expect(form.find('input[type="email"]').attr('name')).toBe('email_address');
  });

  it('appears on post pages too', () => {
    const html = readFileSync('dist/posts/hello-scoreboard/index.html', 'utf8');
    const $ = load(html);
    expect($('form[data-testid="subscribe"]').length).toBe(1);
  });
});
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /c/GameMode/site && npm test
```

Expected: FAIL — `form.attr('action')` is undefined.

- [ ] **Step 3: Write `Subscribe.astro`**

Create `C:\GameMode\site\src\components\Subscribe.astro`. Use the real Kit form ID `9900055` exactly as written below:

```astro
---
const action = 'https://app.kit.com/forms/9900055/subscriptions';
---

<form data-testid="subscribe" action={action} method="post" target="_blank" rel="noopener noreferrer">
  <label for="kit-email">Get each post by email</label>
  <input id="kit-email" type="email" name="email_address" required placeholder="you@example.com" />
  <button type="submit">Subscribe</button>
</form>
```

- [ ] **Step 4: Add it to the homepage**

In `C:\GameMode\site\src\pages\index.astro`, add the import below the existing `BaseHead` import:

```astro
import Subscribe from '../components/Subscribe.astro';
```

and add the component immediately after the intro `<p>` element:

```astro
<Subscribe />
```

- [ ] **Step 5: Add it to the post page**

In `C:\GameMode\site\src\pages\posts\[...slug].astro`, add the import below the existing `BaseHead` import:

```astro
import Subscribe from '../../components/Subscribe.astro';
```

and add the component immediately after `<Content />`:

```astro
<Subscribe />
```

- [ ] **Step 6: Run the test to verify it passes**

```bash
cd /c/GameMode/site && npm test
```

Expected: PASS, 7 tests.

- [ ] **Step 7: Commit**

```bash
cd /c/GameMode
git add site/src/components/Subscribe.astro site/src/pages/index.astro "site/src/pages/posts/[...slug].astro" site/tests/subscribe.test.ts
git commit -m "feat: add newsletter subscribe form"
```

---

### Task 6: Deploy to Cloudflare Pages

**Files:**
- Create: `C:\GameMode\README.md`
- Modify: none.

**Interfaces:**
- Consumes: the built `site/dist/` from Tasks 1-5.
- Produces: a live URL of the form `https://fourshades.pages.dev`, used by Task 7.

- [ ] **Step 1: Write the README**

Create `C:\GameMode\README.md`:

```markdown
# FourShades

A Game Boy emulator written in C++, built in public with AI coding agents.

Agents score 21% on Python tasks and 4% when C or C++ is involved. This project
tests what disciplined, spec-first agentic development can actually do in that
gap — measured against the public Game Boy test ROMs, published honestly,
failures included.

- Specs: `docs/superpowers/specs/`
- Plans: `docs/superpowers/plans/`
- Site: `site/`
```

- [ ] **Step 2: Push to GitHub**

The GitHub username is `DoozleB`:

```bash
cd /c/GameMode
git add README.md
git commit -m "docs: add README"
git remote add origin https://github.com/DoozleB/FourShades.git
git push -u origin main
```

- [ ] **Step 3: Connect Cloudflare Pages**

In the Cloudflare dashboard: **Workers & Pages** → **Create** → **Pages** → **Connect to Git** → select the `FourShades` repository, then set:

- Framework preset: **Astro**
- Build command: `npm run build`
- Build output directory: `dist`
- Root directory: `site`

Save and deploy.

- [ ] **Step 4: Verify the deployment is live**

Substitute the assigned subdomain:

```bash
curl -s https://fourshades.pages.dev/ | grep -c "FourShades"
curl -s -o /dev/null -w "%{http_code}\n" https://fourshades.pages.dev/rss.xml
```

Expected: a non-zero count on the first command, and `200` on the second.

- [ ] **Step 5: Commit**

No code changed in this step. If Cloudflare's assigned subdomain differs from `fourshades.pages.dev`, update `site` in `site/astro.config.mjs` to match, then:

```bash
cd /c/GameMode
git add site/astro.config.mjs
git commit -m "chore: point site config at the deployed URL"
git push
```

---

### Task 7: Custom domain

**Files:**
- Modify: `C:\GameMode\site\astro.config.mjs` (the `site` value)
- Test: `C:\GameMode\site\tests\seo.test.ts` (assert the canonical host)

**Interfaces:**
- Consumes: the live Pages deployment from Task 6.
- Produces: the final canonical origin for all absolute URLs in RSS, sitemap, and canonical tags.

- [ ] **Step 1: Register the domain and attach it**

Register the domain, then in Cloudflare Pages: **Custom domains** → **Set up a custom domain** → enter the domain → follow the DNS instructions. Wait for the status to read **Active**.

- [ ] **Step 2: Write the failing test**

In `C:\GameMode\site\tests\seo.test.ts`, add this test inside the existing `describe('seo', ...)` block. The domain is `doozleb.com`, already owned:

```ts
  it('uses the custom domain as the canonical host', () => {
    const html = readFileSync('dist/posts/hello-scoreboard/index.html', 'utf8');
    const $ = load(html);
    expect($('link[rel="canonical"]').attr('href')).toContain(
      'https://doozleb.com/',
    );
  });
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
cd /c/GameMode/site && npm test
```

Expected: FAIL — the canonical href still points at the `pages.dev` subdomain.

- [ ] **Step 4: Update the site origin**

In `C:\GameMode\site\astro.config.mjs`, change the `site` value to the registered domain:

```js
  site: 'https://doozleb.com',
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
cd /c/GameMode/site && npm test
```

Expected: PASS, 8 tests.

- [ ] **Step 6: Commit and deploy**

```bash
cd /c/GameMode
git add site/astro.config.mjs site/tests/seo.test.ts
git commit -m "feat: switch canonical origin to the custom domain"
git push
```

- [ ] **Step 7: Verify live**

```bash
curl -s https://doozleb.com/rss.xml | grep -c "https://doozleb.com/posts/"
```

Expected: a non-zero count.

---

## Done when

- `npm test` passes 8 tests from `C:\GameMode\site`.
- The site is live on an owned domain over HTTPS.
- `/rss.xml` and `/sitemap-index.xml` both return 200 with absolute URLs on that domain.
- A visitor can subscribe by email.
- The repository is public and its history starts at commit one.

## Not in this plan

- Emulator code. That is the next plan.
- Styling beyond browser defaults. Deliberate: content and correctness first, and an unstyled page ships faster than a designed one. Revisit once the second post exists.
- Analytics. Add when there is traffic to measure.
- Any product, pricing, or paid tier — excluded by the spec for months 1-3.
