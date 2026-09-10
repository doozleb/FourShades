/** Structural type so this stays unit-testable without the Astro runtime. */
type PostLike = { data: { date: Date; draft?: boolean } };

/**
 * Published posts, newest first. Drafts are excluded. Returns a new array;
 * the input is not mutated.
 */
export function publishedPosts<T extends PostLike>(posts: readonly T[]): T[] {
  return posts
    .filter((post) => !post.data.draft)
    .toSorted((a, b) => b.data.date.valueOf() - a.data.date.valueOf());
}
