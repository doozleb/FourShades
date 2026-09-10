import { readFileSync, existsSync } from 'node:fs';
import { load } from 'cheerio';
import { describe, it, expect } from 'vitest';

describe('404 page', () => {
  it('builds a 404.html with a heading and a link home', () => {
    const html = readFileSync('dist/404.html', 'utf8');
    const $ = load(html);
    expect($('h1').text()).toContain('Page not found');
    expect($('a[href="/"]').length).toBeGreaterThan(0);
  });
});

describe('robots.txt', () => {
  it('is served as a real file and points at the sitemap', () => {
    expect(existsSync('dist/robots.txt')).toBe(true);
    const txt = readFileSync('dist/robots.txt', 'utf8');
    expect(txt).toContain('User-agent: *');
    expect(txt).toContain('Sitemap: https://fourshades.pages.dev/sitemap-index.xml');
  });
});
