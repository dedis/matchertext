// Time the parse5 fork against its upstream base on parse5's huge benchmark page.
//
// Usage: node perf.mjs <stock dist/index.js> <fork dist/index.js> <page.html>
//
// Cases, each the stock call against the fork call on the same page:
//   parse                 parse with default options (the matchertext option off)
//   parse_matchertext     the fork parses with the matchertext option on
//   serialize             serialize with default options (the fork's guard on)
//   serialize_unguarded   the fork serializes with the guard off
//
// Output: one JSON object per case, {"case", "stock_ms", "fork_ms"}: the time of one call, the median of RUNS
// samples after a warm-up. A sample repeats the call for at least SAMPLE_MS; stock and fork samples alternate, and
// which of them comes first alternates too.
import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

const [stockPath, forkPath, pagePath] = process.argv.slice(2);
const stock = await import(pathToFileURL(stockPath).href);
const fork = await import(pathToFileURL(forkPath).href);
const page = readFileSync(pagePath, 'utf8');
const RUNS = 21;
const SAMPLE_MS = 100;
const WARM_UP = 20;

const stockTree = stock.parse(page);
const forkTree = fork.parse(page);
const CASES = {
    parse: [() => stock.parse(page), () => fork.parse(page)],
    parse_matchertext: [() => stock.parse(page), () => fork.parse(page, { matchertext: true })],
    serialize: [() => stock.serialize(stockTree), () => fork.serialize(forkTree)],
    serialize_unguarded: [() => stock.serialize(stockTree), () => fork.serialize(forkTree, { matchertext: false })],
};

function time(fn, repeat) {
    const start = process.hrtime.bigint();
    for (let i = 0; i < repeat; i++) fn();
    return Number(process.hrtime.bigint() - start) / 1e6 / repeat;
}

const median = (xs) => xs.toSorted((a, b) => a - b)[xs.length >> 1];

for (const [name, [stockFn, forkFn]] of Object.entries(CASES)) {
    for (let i = 0; i < WARM_UP; i++) {
        stockFn();
        forkFn();
    }
    const repeat = Math.max(1, Math.ceil(SAMPLE_MS / time(stockFn, 1)));
    const stockMs = [];
    const forkMs = [];
    for (let i = 0; i < RUNS; i++) {
        if (i % 2 === 0) {
            stockMs.push(time(stockFn, repeat));
            forkMs.push(time(forkFn, repeat));
        } else {
            forkMs.push(time(forkFn, repeat));
            stockMs.push(time(stockFn, repeat));
        }
    }
    process.stdout.write(`${JSON.stringify({ case: name, stock_ms: median(stockMs), fork_ms: median(forkMs) })}\n`);
}
