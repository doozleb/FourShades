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
