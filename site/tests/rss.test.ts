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
