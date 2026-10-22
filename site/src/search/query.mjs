// SPDX-License-Identifier: GPL-3.0-only
// Only complete, known task phrases are normalized. Symbols, quoted searches,
// unknown words and partial queries keep Pagefind's normal matching behavior.
const vocabulary = ['spawn', 'read', 'ship', 'inventory', 'container', 'contents'];
const articles = new Set(['a', 'an', 'the']);

function oneEditApart(left, right) {
  if (Math.abs(left.length - right.length) > 1) return false;
  let index = 0;
  while (index < left.length && left[index] === right[index]) index++;
  if (left.length === right.length) {
    return left.slice(index + 1) === right.slice(index + 1)
      || (left[index] === right[index + 1] && left[index + 1] === right[index]
        && left.slice(index + 2) === right.slice(index + 2));
  }
  const [shorter, longer] = left.length < right.length ? [left, right] : [right, left];
  return shorter.slice(index) === longer.slice(index + 1);
}

export function normalizeTaskQuery(query) {
  if (!/^[a-z ]+$/i.test(query) || query.length > 100) return query;
  const phrase = query.toLowerCase().trim().replace(/^(?:please |how (?:do i|can i|to) |can i |i want to |show me how to )/, '');
  const words = phrase.split(/ +/);
  const corrected = words.map(word => {
    if (articles.has(word) || vocabulary.includes(word) || word.length < 4) return word;
    const matches = vocabulary.filter(candidate => oneEditApart(word, candidate));
    return matches.length === 1 ? matches[0] : word;
  });
  const task = corrected.filter(word => !articles.has(word)).join(' ');
  if (!['spawn ship', 'read inventory', 'read container contents'].includes(task)) return query;
  const normalized = task === 'spawn ship' ? 'spawn a ship' : task;
  return normalized === query.toLowerCase().trim() ? query : normalized;
}
