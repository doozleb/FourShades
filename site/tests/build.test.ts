import { readFileSync } from 'node:fs';
import { load } from 'cheerio';
import { describe, it, expect } from 'vitest';

describe('homepage', () => {
  it('builds and renders the site name in an h1', () => {
    const html = readFileSync('dist/index.html', 'utf8');
    const $ = load(html);
    expect($('h1').text()).toContain('GameMode');
  });

  // Astro injects the doctype at build time; the source templates omit it.
  // This pins that behaviour rather than driving it — it passed on first run.
  it('declares a doctype so the page is not in quirks mode', () => {
    const html = readFileSync('dist/index.html', 'utf8');
    expect(html.trimStart().toLowerCase().startsWith('<!doctype html>')).toBe(true);
  });
});
