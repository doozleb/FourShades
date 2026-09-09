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
