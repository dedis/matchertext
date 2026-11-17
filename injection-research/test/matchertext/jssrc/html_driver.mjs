// Drive the matchertext parse5 fork through an HTML attack corpus. Each
// untrusted value goes into one host context by one arm, and the parsed tree
// is compared with the same host and arm holding a benign value. All
// judgement of the corpus lives in pipeline/html_experiment.py: this prints
// measurements.
//
// Usage: node html_driver.mjs <path to packages/parse5/dist/index.js>
//
// A parser without the matchertext option is stock parse5: with the option off
// the fork passes the whole stock test suite unchanged.
//
// Input: one JSON object per line, {"i": index, "v": value}.
// Output: one JSON object per line:
//   {"hosts": [...], "arms": [...]}                 the host and arm tables
//   {"selfcheck": {...}}                            verdicts of fixed cases
//   {"i", "h", "a", "verdict", "reader", "trip"}    one per legal host and arm
//   {"i", "done": n}                                after the n rows of value i
//   {"end": count}                                  after the last value
//
// verdict  INERT      the host's structure, and the hole holds the value exactly
//          ALTERED    the host's structure, but the hole holds another value
//          BREAKOUT   another structure
//          REJECTED   the arm refused the value (VERIFY or composeHtml)
//          REFUSED    a serializer of the fork refused the tree
//          MALFORMED  the parser refused the document that the arm wrote
// reader   for a matchertext arm, how a parser without the option reads the
//          same document, against the benign document: SAME, ATTRIBUTES (other
//          attribute names on the same elements) or ELEMENTS (other elements or
//          comments); "-" for other arms. Such a parser reads a hole as markup,
//          so its attribute names hold the value even for a benign value.
// trip     for a matchertext arm, whether the default serializer's output, read
//          by a parser without the option, gives the same tree: SAME,
//          DIFFERENT or REFUSED; "-" for other arms
import { createInterface } from 'node:readline';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const distPath = process.argv[2];
const {
    parse,
    serialize,
    composeHtml,
    htmlTemplate,
    verifyMatchertext,
    MatchertextError,
    defaultTreeAdapter: adapter,
} = await import(
    pathToFileURL(distPath).href
);
// The escaping that parse5 itself uses, from the fork's own dependencies.
const { escapeText, escapeAttribute } = createRequire(distPath)('entities/escape');

const BENIGN = 'alice';
const STOCK = { matchertext: false };
const MT = { matchertext: true };

function fail(message) {
    process.stderr.write(`html_driver.mjs: ${message}\n`);
    process.exit(1);
}

// A host is the markup around the hole. For a text host the hole is the text of the element `id=h`, between the
// two parts of `wrap`; for an attribute host it is attribute `attr` of that element; for the comment host it is the
// data of the first comment.
const HOSTS = [
    { name: 'text', kind: 'text', tag: 'div', cooked: true },
    { name: 'table_cell', kind: 'text', tag: 'td', cooked: true, pre: '<table><tr>', post: '</tr></table>' },
    { name: 'svg_text', kind: 'text', tag: 'text', cooked: true, pre: '<svg>', post: '</svg>' },
    { name: 'textarea', kind: 'text', tag: 'textarea' },
    { name: 'title', kind: 'text', tag: 'title' },
    { name: 'style', kind: 'text', tag: 'style' },
    { name: 'script_string', kind: 'text', tag: 'script', wrap: ['var x = "', '";'] },
    { name: 'attr_dq', kind: 'attr', tag: 'div', attr: 'title', quote: '"' },
    { name: 'attr_sq', kind: 'attr', tag: 'div', attr: 'title', quote: "'" },
    { name: 'attr_unquoted', kind: 'attr', tag: 'div', attr: 'title', quote: '' },
    { name: 'href', kind: 'attr', tag: 'a', attr: 'href', quote: '"', semantic: true },
    { name: 'onclick', kind: 'attr', tag: 'a', attr: 'onclick', quote: '"', semantic: true },
    { name: 'comment', kind: 'comment' },
].map((host) => ({ pre: '', post: '', wrap: ['', ''], ...host }));

// The compose arm writes each host as a template literal, since composeHtml takes no markup built at run time. The
// startup check below holds each to the document of its host.
const COMPOSE = {
    text: (v) => htmlTemplate`<div id=h [${v}]>`,
    table_cell: (v) => htmlTemplate`<table><tr><td id=h [${v}]></tr></table>`,
    svg_text: (v) => htmlTemplate`<svg><text id=h [${v}]></svg>`,
    textarea: (v) => htmlTemplate`<textarea id=h [${v}]>`,
    title: (v) => htmlTemplate`<title id=h [${v}]>`,
    style: (v) => htmlTemplate`<style id=h [${v}]>`,
    script_string: (v) => htmlTemplate`<script id=h [var x = "${v}";]>`,
    attr_dq: (v) => htmlTemplate`<div id=h title=[${v}]></div>`,
    attr_sq: (v) => htmlTemplate`<div id=h title=[${v}]></div>`,
    attr_unquoted: (v) => htmlTemplate`<div id=h title=[${v}]></div>`,
    href: (v) => htmlTemplate`<a id=h href=[${v}]></a>`,
    onclick: (v) => htmlTemplate`<a id=h onclick=[${v}]></a>`,
};

const ARMS = [
    'concat',
    'escape',
    'dom_stock',
    'dom_guarded',
    'dom_mt',
    'mt_verify',
    'mt_section',
    'mt_encode',
    'compose',
];
const MT_ARMS = new Set(['dom_mt', 'mt_verify', 'mt_section', 'mt_encode', 'compose']);
// The serializer writes raw text that the guard refuses as a matchertext element when the text is matchertext.
const MT_OUTPUT = { matchertextOutput: true };

function legal(host, arm) {
    if (host.kind === 'comment') {
        return arm === 'concat' || arm.startsWith('dom_');
    }
    if (arm === 'mt_section' || arm === 'mt_encode') {
        // MDATA sections and character references are read only in normal element content.
        return host.cooked === true;
    }
    return true;
}

function matcherKind(c) {
    const i = '()[]{}'.indexOf(c);
    return i < 0 ? 0 : (i % 2 === 0 ? 1 : -1) * (1 + (i >> 1));
}

const REFERENCE = { '(': '&lpar;', ')': '&rpar;', '[': '&lsqb;', ']': '&rsqb;', '{': '&lcub;', '}': '&rcub;' };

/**
 * Encode `v` for normal element content without refusing it: matcher-balanced runs go into MDATA sections, and
 * the matchers that cannot be in a balanced run become character references. A closer that does not match the
 * open matcher makes every open matcher a reference too, so that no pair spans a reference and every run is
 * balanced. A value nested deeper than the limit has all its matchers as references.
 */
function encodeCooked(v) {
    const ref = new Uint8Array(v.length);
    const open = [];
    let tooDeep = false;

    for (let i = 0; i < v.length; i++) {
        const kind = matcherKind(v[i]);

        if (kind > 0) {
            open.push(i);
            tooDeep ||= open.length > 1000;
        } else if (kind < 0) {
            if (open.length > 0 && matcherKind(v[open[open.length - 1]]) === -kind) {
                open.pop();
            } else {
                ref[i] = 1;
                for (const j of open) ref[j] = 1;
                open.length = 0;
            }
        }
    }
    for (const j of open) ref[j] = 1;

    let out = '';
    let run = '';
    const flush = () => {
        if (run.length > 0) out += `<![MDATA[${run}]]>`;
        run = '';
    };

    for (let i = 0; i < v.length; i++) {
        if (ref[i] === 1 || (tooDeep && matcherKind(v[i]) !== 0)) {
            flush();
            out += REFERENCE[v[i]];
        } else {
            run += v[i];
        }
    }
    flush();

    return out;
}

// The document of `host` with `inner` as the markup of its hole element, or of its comment.
function documentOf(host, inner) {
    return `${host.pre}${inner}${host.post}`;
}

function element(host, content) {
    return `<${host.tag} id=h>${content}</${host.tag}>`;
}

function attribute(host, value) {
    return `<${host.tag} id=h ${host.attr}=${host.quote}${value}${host.quote}></${host.tag}>`;
}

/** The value that the hole of `host` must hold for value `v`. */
function expected(host, v) {
    return host.wrap[0] + v + host.wrap[1];
}

/**
 * Write `v` into `host` by `arm`. Return the parsed document, or a verdict string if the arm refuses the value or
 * the parser refuses the document.
 */
function write(host, arm, v) {
    const [w0, w1] = host.wrap;

    try {
        switch (arm) {
            case 'concat': {
                const inner =
                    host.kind === 'text'
                        ? element(host, w0 + v + w1)
                        : host.kind === 'attr'
                          ? attribute(host, v)
                          : `<!--${v}-->`;
                return parse(documentOf(host, inner), STOCK);
            }
            case 'escape': {
                const inner =
                    host.kind === 'text' ? element(host, w0 + escapeText(v) + w1) : attribute(host, escapeAttribute(v));
                return parse(documentOf(host, inner), STOCK);
            }
            case 'dom_stock':
            case 'dom_guarded': {
                return parse(serialize(build(host, v), arm === 'dom_stock' ? STOCK : {}), STOCK);
            }
            case 'dom_mt': {
                return parse(serialize(build(host, v), MT_OUTPUT), MT);
            }
            case 'mt_verify':
            case 'mt_section':
            case 'mt_encode': {
                const html = matchertextDocument(host, arm, v);
                return html === null ? 'REJECTED' : parse(html, MT);
            }
            case 'compose': {
                return composeHtml(COMPOSE[host.name](v));
            }
        }
    } catch (error) {
        if (!(error instanceof MatchertextError)) throw error;
        if (error.code === 'matchertext-invalid-argument') return 'REJECTED';
        if (error.code === 'matchertext-unserializable-text' || error.code === 'matchertext-unserializable-comment') {
            return 'REFUSED';
        }
        return 'MALFORMED';
    }

    throw new Error(`unknown arm ${arm}`);
}

/** The tree of `host` holding `v`, built through the tree adapter so that `v` is never parsed. */
function build(host, v) {
    const document = parse(documentOf(host, host.kind === 'comment' ? '' : element(host, '')), STOCK);

    if (host.kind === 'comment') {
        adapter.appendChild(document.childNodes[0].childNodes[1], adapter.createCommentNode(v));
        return document;
    }

    const hole = findHole(document);
    if (host.kind === 'text') {
        adapter.insertText(hole, expected(host, v));
    } else {
        hole.attrs.push({ name: host.attr, value: v });
    }
    return document;
}

function children(node) {
    // Only an HTML template has a content fragment; an SVG or MathML template keeps its children.
    return node.content ? node.content.childNodes : (node.childNodes ?? []);
}

function findHole(node) {
    for (const child of children(node)) {
        if (child.attrs?.some(({ name, value }) => name === 'id' && value === 'h')) return child;
        const found = findHole(child);
        if (found) return found;
    }
    return null;
}

/**
 * The structure of `document` for `host`, with the hole as one marker, and the value in the hole. The text of the
 * hole element is the hole only while it has no child that is not text; the first comment is the comment hole.
 */
function shape(host, document) {
    let value = null;
    let commentSeen = false;

    function walk(node) {
        if (node.nodeName === '#text') return JSON.stringify(node.value);
        if (node.nodeName === '#comment') {
            if (host.kind === 'comment' && !commentSeen) {
                commentSeen = true;
                value = node.data;
                return '<!HOLE>';
            }
            return `<!${JSON.stringify(node.data)}>`;
        }
        if (node.nodeName === '#documentType') return '<!DOCTYPE>';

        const isHole = value === null && node.attrs?.some(({ name, value: v }) => name === 'id' && v === 'h');
        const attrs = (node.attrs ?? [])
            .map(({ name, value: v }) => (isHole && host.kind === 'attr' && name === host.attr ? `${name}=HOLE` : `${name}=${JSON.stringify(v)}`))
            .join(' ');
        const kids = children(node);

        if (isHole && host.kind === 'attr') {
            value = node.attrs.find(({ name }) => name === host.attr)?.value ?? null;
        }
        if (isHole && host.kind === 'text' && kids.every((kid) => kid.nodeName === '#text')) {
            value = kids.map((kid) => kid.value).join('');
            return `<${node.namespaceURI ?? ''}:${node.nodeName} ${attrs}>HOLE</>`;
        }
        return `<${node.namespaceURI ?? ''}:${node.nodeName ?? '#document'} ${attrs}>${kids.map(walk).join('')}</>`;
    }

    return { structure: walk(document), value };
}

function judge(host, result, base, v) {
    if (typeof result === 'string') return result;

    const { structure, value } = shape(host, result);
    if (structure !== base.structure) return 'BREAKOUT';
    return value === expected(host, v) ? 'INERT' : 'ALTERED';
}

/** The document that a matchertext arm writes for `v`, or null if the arm refuses it. */
function matchertextDocument(host, arm, v) {
    const [w0, w1] = host.wrap;

    switch (arm) {
        case 'mt_verify': {
            if (!verifyMatchertext(v)) return null;
            return documentOf(
                host,
                host.kind === 'text'
                    ? `<${host.tag} id=h [${w0}${v}${w1}]>`
                    : `<${host.tag} id=h ${host.attr}=[${v}]></${host.tag}>`,
            );
        }
        case 'mt_section': {
            return verifyMatchertext(v) ? documentOf(host, element(host, `<![MDATA[${v}]]>`)) : null;
        }
        case 'mt_encode': {
            return documentOf(host, element(host, encodeCooked(v)));
        }
        case 'dom_mt': {
            try {
                return serialize(build(host, v), MT_OUTPUT);
            } catch (error) {
                if (error instanceof MatchertextError) return null;
                throw error;
            }
        }
        case 'compose': {
            // composeHtml writes no document; the equivalent document is the one mt_verify writes.
            return matchertextDocument(host, 'mt_verify', v);
        }
    }
    return null;
}

/** The elements and comments of `node`, and separately the attribute names of its elements, in document order. */
function outline(node) {
    const elements = [];
    const attributes = [];

    function walk(n) {
        if (n.nodeName === '#comment') {
            elements.push('#comment');
        } else if (n.tagName !== undefined) {
            elements.push(`${n.namespaceURI}:${n.tagName}`);
            attributes.push(n.attrs.map(({ name }) => name).join(' '));
        }
        for (const child of children(n)) walk(child);
    }
    walk(node);

    return { elements: elements.join(','), attributes: attributes.join(',') };
}

/** How a parser without the option reads the document of a matchertext arm, against the benign document. */
function reader(host, arm, v, readerBase) {
    const html = matchertextDocument(host, arm, v);
    if (html === null) return '-';
    const { elements, attributes } = outline(parse(html, STOCK));
    if (elements !== readerBase.elements) return 'ELEMENTS';
    return attributes === readerBase.attributes ? 'SAME' : 'ATTRIBUTES';
}

/** Whether the default serializer writes tree `document` so that a parser without the option reads it back. */
function trip(host, document) {
    let html;

    try {
        html = serialize(document);
    } catch (error) {
        if (error instanceof MatchertextError) return 'REFUSED';
        throw error;
    }

    const a = shape(host, document);
    const b = shape(host, parse(html, STOCK));
    return a.structure === b.structure && a.value === b.value ? 'SAME' : 'DIFFERENT';
}

const pairs = [];
for (const [h, host] of HOSTS.entries()) {
    for (const [a, arm] of ARMS.entries()) {
        if (legal(host, arm)) pairs.push([h, a]);
    }
}

// A2: composeHtml must refuse markup that is not a template literal.
for (const forged of [
    () => htmlTemplate(['<p [', ']>'], 'x'),
    () => composeHtml({ strings: ['<p [', ']>'], values: ['x'] }),
]) {
    try {
        forged();
    } catch (error) {
        if (error instanceof TypeError) continue;
        throw error;
    }
    fail('composeHtml accepted markup that is not a template literal');
}

// Each compose template must write the document that mt_verify writes for its host.
for (const host of HOSTS) {
    if (!legal(host, 'compose')) continue;
    const want = serialize(parse(matchertextDocument(host, 'mt_verify', BENIGN), MT));
    if (serialize(composeHtml(COMPOSE[host.name](BENIGN))) !== want) fail(`compose template of ${host.name} differs`);
}

// The benign value gives the structure every arm must keep. A baseline that is not inert means a broken host.
const base = new Map();
const readerBase = new Map();
for (const [h, a] of pairs) {
    const host = HOSTS[h];
    const arm = ARMS[a];
    const result = write(host, arm, BENIGN);
    if (typeof result === 'string') fail(`baseline ${host.name}/${arm} is ${result}`);
    const benign = shape(host, result);
    if (benign.value !== expected(host, BENIGN)) fail(`baseline ${host.name}/${arm} does not hold the value`);
    base.set(`${h} ${a}`, benign);
    if (MT_ARMS.has(arm)) readerBase.set(`${h} ${a}`, outline(parse(matchertextDocument(host, arm, BENIGN), STOCK)));
}

function run(h, a, v) {
    const host = HOSTS[h];
    const arm = ARMS[a];
    const result = write(host, arm, v);
    const isMt = MT_ARMS.has(arm);

    return {
        verdict: judge(host, result, base.get(`${h} ${a}`), v),
        reader: isMt ? reader(host, arm, v, readerBase.get(`${h} ${a}`)) : '-',
        trip: isMt && typeof result !== 'string' ? trip(host, result) : '-',
    };
}

const out = (object) => process.stdout.write(`${JSON.stringify(object)}\n`);
const id = (list, name) => list.findIndex((item) => (item.name ?? item) === name);
const check = (hostName, armName, v) => run(id(HOSTS, hostName), id(ARMS, armName), v).verdict;

out({
    hosts: HOSTS.map(({ name, kind, semantic = false }) => ({ name, kind, semantic })),
    arms: ARMS.map((name) => ({ name, matchertext: MT_ARMS.has(name) })),
});
out({
    selfcheck: {
        concat_breakout: check('attr_dq', 'concat', '"><b>x</b>'),
        mt_rejects: check('text', 'mt_verify', 'x]>'),
        compose_inert: check('text', 'compose', '<b>x</b>'),
        dom_stock_breakout: check('script_string', 'dom_stock', '</script><b>x</b>'),
        dom_guarded_refuses: check('script_string', 'dom_guarded', '</script><b>x</b>'),
        dom_mt_inert: check('script_string', 'dom_mt', '</script><b>x</b>'),
        dom_mt_refuses: check('script_string', 'dom_mt', '</script>]'),
        encode_inert: check('text', 'mt_encode', 'x]> (y'),
    },
});

let count = 0;
for await (const line of createInterface({ input: process.stdin, crlfDelay: Infinity })) {
    let message;

    try {
        message = JSON.parse(line);
    } catch {
        fail('malformed input line');
    }
    if (!Number.isInteger(message.i) || typeof message.v !== 'string') fail('malformed input object');

    for (const [h, a] of pairs) {
        out({ i: message.i, h, a, ...run(h, a, message.v) });
    }
    out({ i: message.i, done: pairs.length });
    count++;
}
out({ end: count });
