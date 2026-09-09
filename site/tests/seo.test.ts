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
