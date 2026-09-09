import { readFileSync } from 'node:fs';
import { load } from 'cheerio';
import { describe, it, expect } from 'vitest';

describe('homepage', () => {
  it('builds and renders the site name in an h1', () => {
    const html = readFileSync('dist/index.html', 'utf8');
    const $ = load(html);
    expect($('h1').text()).toContain('GameMode');
  });
});
