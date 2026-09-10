import { readFileSync } from 'node:fs';
import { load } from 'cheerio';
import { describe, it, expect } from 'vitest';

const pages = {
  homepage: 'dist/index.html',
  post: 'dist/posts/hello-scoreboard/index.html',
  notFound: 'dist/404.html',
};

describe('colophon', () => {
  for (const [name, file] of Object.entries(pages)) {
    it(`discloses AI drafting on the ${name}`, () => {
      const $ = load(readFileSync(file, 'utf8'));
      const colophon = $('footer[data-testid="colophon"]');
      expect(colophon.length).toBe(1);
      expect(colophon.text()).toContain('drafted by the same AI agents');
      expect(colophon.find('a[href="https://github.com/doozleb/FourShades"]').length).toBe(1);
    });
  }
});
