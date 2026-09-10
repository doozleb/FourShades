import { describe, it, expect } from 'vitest';
import { publishedPosts } from '../src/lib/posts';

const post = (id: string, date: string, draft?: boolean) => ({
  id,
  data: { date: new Date(date), draft },
});

describe('publishedPosts', () => {
  it('returns newest first', () => {
    const input = [post('old', '2026-01-01'), post('new', '2026-06-01'), post('mid', '2026-03-01')];
    expect(publishedPosts(input).map((p) => p.id)).toEqual(['new', 'mid', 'old']);
  });

  it('excludes drafts', () => {
    const input = [post('live', '2026-01-01'), post('wip', '2026-06-01', true)];
    expect(publishedPosts(input).map((p) => p.id)).toEqual(['live']);
  });

  it('treats a missing draft flag as published', () => {
    const input = [post('a', '2026-01-01')];
    expect(publishedPosts(input)).toHaveLength(1);
  });

  it('does not mutate its input', () => {
    const input = [post('old', '2026-01-01'), post('new', '2026-06-01')];
    publishedPosts(input);
    expect(input.map((p) => p.id)).toEqual(['old', 'new']);
  });
});
