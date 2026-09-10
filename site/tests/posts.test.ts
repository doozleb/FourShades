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

describe('homepage post list', () => {
  it('links to the post', () => {
    const html = readFileSync('dist/index.html', 'utf8');
    const $ = load(html);
    expect($('ul a[href="/posts/hello-scoreboard/"]').length).toBe(1);
  });
});
