import rss from '@astrojs/rss';
import { getCollection } from 'astro:content';
import type { APIContext } from 'astro';
import { publishedPosts } from '../lib/posts';

export async function GET(context: APIContext) {
  const sorted = publishedPosts(await getCollection('posts'));

  return rss({
    title: 'FourShades',
    description:
      'Building a Game Boy emulator in C++ with AI agents, in public.',
    site: context.site!,
    items: sorted.map((post) => ({
      title: post.data.title,
      description: post.data.description,
      pubDate: post.data.date,
      link: `/posts/${post.id}/`,
    })),
  });
}
