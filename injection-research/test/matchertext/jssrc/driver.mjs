// Test driver for the matchertext scanner and holes of the parse5 fork in
// injection-research/parse5. It is the JavaScript counterpart of
// csrc/driver.c: it decodes commands, calls the fork, and prints one result.
// All test logic lives in matchertext_test.go, which owns the Go oracle.
//
// Usage: node driver.mjs <path to packages/parse5/dist/index.js>
//
// Commands arrive one per line on stdin. Values are hex encoded UTF-8, so that
// zero bytes and newlines survive the trip.
//
//   V <hex>   print 1 if verifyMatchertext(value), else 0
//   H <hex>   print 1 if <div [value]> parses to a div whose text is the
//             value, else 0
//   A <hex>   print 1 if <div title=[value]> parses to a div whose title is
//             the value, else 0
//
// The parser reads NUL in a hole as U+FFFD, so a hole holds the value when it
// holds the value with that replacement. A malformed command is a harness bug,
// so the driver exits non-zero and says so.
import { createInterface } from 'node:readline';
import { pathToFileURL } from 'node:url';

const { parseFragment, verifyMatchertext, MatchertextError } = await import(pathToFileURL(process.argv[2]).href);
const decoder = new TextDecoder('utf-8', { fatal: true });
const OPTIONS = { matchertext: true };

function fail(message) {
    process.stderr.write(`driver.mjs: ${message}\n`);
    process.exit(1);
}

function decode(hex) {
    if (!/^(?:[0-9a-f]{2})*$/i.test(hex)) {
        fail('malformed hex');
    }
    return decoder.decode(Buffer.from(hex, 'hex'));
}

// Parse `html` and return its only node if it is a div, or null if the parser refuses the input.
function parseDiv(html) {
    let nodes;

    try {
        nodes = parseFragment(html, OPTIONS).childNodes;
    } catch (error) {
        if (error instanceof MatchertextError) {
            return null;
        }
        throw error;
    }

    return nodes.length === 1 && nodes[0].nodeName === 'div' ? nodes[0] : null;
}

function holdsElement(value) {
    const div = parseDiv(`<div [${value}]>`);
    const data = value.replaceAll('\0', '\uFFFD');

    if (div === null) {
        return false;
    }
    if (div.childNodes.length === 0) {
        return data === '';
    }
    return div.childNodes.length === 1 && div.childNodes[0].nodeName === '#text' && div.childNodes[0].value === data;
}

function holdsAttribute(value) {
    const div = parseDiv(`<div title=[${value}]>`);
    const data = value.replaceAll('\0', '\uFFFD');

    return div !== null && div.attrs.length === 1 && div.attrs[0].name === 'title' && div.attrs[0].value === data;
}

const out = [];

for await (const line of createInterface({ input: process.stdin, crlfDelay: Infinity })) {
    if (line[1] !== ' ') {
        fail('malformed command');
    }

    const value = decode(line.slice(2));

    switch (line[0]) {
        case 'V': {
            out.push(verifyMatchertext(value) ? '1' : '0');
            break;
        }
        case 'H': {
            out.push(holdsElement(value) ? '1' : '0');
            break;
        }
        case 'A': {
            out.push(holdsAttribute(value) ? '1' : '0');
            break;
        }
        default: {
            fail('malformed command');
        }
    }
}

process.stdout.write(out.length > 0 ? `${out.join('\n')}\n` : '');
