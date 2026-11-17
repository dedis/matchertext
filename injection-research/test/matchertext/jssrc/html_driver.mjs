// Drive the matchertext parse5 fork through an HTML attack corpus. Each
// untrusted value goes into one host context by one arm, and the parsed tree
// is compared with the same host and arm holding a benign value. All
// judgement of the corpus lives in pipeline/html_experiment.py: this prints
// measurements.
//
// Usage: node html_driver.mjs <packages/parse5/dist/index.js> <autoescape binary> [--bytes] [--browsers]
//
// With the matchertext options off in the parser and the serializer, the fork
// passes the whole stock test suite unchanged.
//
// The autoescape binary (test/matchertext/gosrc/autoescape) writes the
// documents of the auto-escaping arm with Go's html/template.
//
// --bytes adds two arms that write the document of the element or attribute
// hole as UTF-8 bytes and decode them as a reader does (A3 at the byte level):
// mt_utf8 with the WHATWG UTF-8 decoder, and mt_sjis with the Shift_JIS
// decoder, a charset that the writer did not declare.
//
// --browsers also parses, in Chromium, Firefox, and WebKit (browser.mjs), the
// document that a browser receives from each case: the document of a stock
// arm, the default serializer's output of a matchertext tree, or the fragment
// that the client path sets by innerHTML.
//
// Input: one JSON object per line, {"i": index, "v": value}.
// Output: one JSON object per line:
//   {"hosts": [...], "arms": [...], "browsers": {...}} the host and arm tables, the browser versions
//   {"selfcheck": {...}}                            verdicts of fixed cases
//   {"i", "h", "a", "verdict", "reader", "trip", "js", "browsers"}  one per legal host and arm
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
// js       for the script host, the verdict of the JavaScript string that holds
//          the value (jsVerdict); "-" for other hosts or when the HTML holds no value
// browsers the verdict in each browser of the document that it receives, in the
//          order of browser.mjs ENGINES; "-" without --browsers or when no
//          document is sent
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
import { Script } from 'node:vm';

const [distPath, autoescapePath, ...flags] = process.argv.slice(2);
const {
    parse,
    parseFragment,
    serialize,
    composeHtml,
    htmlTemplate,
    verifyMatchertext,
    MatchertextError,
    defaultTreeAdapter: adapter,
} = await import(pathToFileURL(distPath).href);
// The escaping that parse5 itself uses, from the fork's own dependencies.
const { escapeText, escapeAttribute } = createRequire(distPath)('entities/escape');

const BENIGN = 'alice';
const STOCK = { matchertext: false };
const MT = { matchertext: true };
const BATCH = 4000;

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
    { name: 'option', kind: 'text', tag: 'option', cooked: true, pre: '<select>', post: '</select>' },
    { name: 'template', kind: 'text', tag: 'template', cooked: true },
    { name: 'textarea', kind: 'text', tag: 'textarea' },
    { name: 'title', kind: 'text', tag: 'title' },
    { name: 'style', kind: 'text', tag: 'style' },
    { name: 'noscript', kind: 'text', tag: 'noscript', pre: '<body>' },
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
    option: (v) => htmlTemplate`<select><option id=h [${v}]></select>`,
    template: (v) => htmlTemplate`<template id=h [${v}]>`,
    textarea: (v) => htmlTemplate`<textarea id=h [${v}]>`,
    title: (v) => htmlTemplate`<title id=h [${v}]>`,
    style: (v) => htmlTemplate`<style id=h [${v}]>`,
    noscript: (v) => htmlTemplate`<body><noscript id=h [${v}]>`,
    script_string: (v) => htmlTemplate`<script id=h [var x = "${v}";]>`,
    attr_dq: (v) => htmlTemplate`<div id=h title=[${v}]></div>`,
    attr_sq: (v) => htmlTemplate`<div id=h title=[${v}]></div>`,
    attr_unquoted: (v) => htmlTemplate`<div id=h title=[${v}]></div>`,
    href: (v) => htmlTemplate`<a id=h href=[${v}]></a>`,
    onclick: (v) => htmlTemplate`<a id=h onclick=[${v}]></a>`,
};

const BYTE_ARMS = { mt_utf8: 'utf-8', mt_sjis: 'shift_jis' };
const ARMS = [
    'concat',
    'escape',
    'escape_context',
    'autoescape',
    'dom_stock',
    'dom_guarded',
    'dom_mt',
    'mt_verify',
    'mt_section',
    'mt_encode',
    'compose',
    'compose_js',
    'compose_dom',
    ...(flags.includes('--bytes') ? Object.keys(BYTE_ARMS) : []),
];
const MT_ARMS = new Set([
    'dom_mt',
    'mt_verify',
    'mt_section',
    'mt_encode',
    'compose',
    'compose_js',
    'compose_dom',
    'mt_utf8',
]);
// The matchertext arms whose document a parser without the option reads (reader) and whose tree the default
// serializer writes for a browser (trip).
const TRIP_ARMS = new Set(['dom_mt', 'mt_verify', 'mt_section', 'mt_encode', 'compose', 'compose_js', 'mt_utf8']);
// The arms that parse the value as markup in its hole and judge the round trip of that tree through a serializer.
// The serializer writes raw text that the guard refuses as a matchertext element when the text is matchertext.
const MT_OUTPUT = { matchertextOutput: true };

function legal(host, arm) {
    if (host.kind === 'comment') {
        return arm === 'concat' || arm.startsWith('dom_');
    }
    switch (arm) {
        case 'mt_section':
        case 'mt_encode': {
            // MDATA sections and character references are read only in normal element content.
            return host.cooked === true;
        }
        case 'compose_js': {
            return host.name === 'script_string';
        }
        case 'autoescape': {
            // html/template writes a value in an event handler as a JavaScript string literal, not as the value.
            return host.name !== 'onclick';
        }
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

// Every character below U+0100 that is not an ASCII letter or digit.
const LOW = /[^\dA-Za-zĀ-￿]/g;
const hex = (c, width) => c.charCodeAt(0).toString(16).padStart(width, '0');

/**
 * `v` escaped for its host by the rule of the OWASP XSS Prevention Cheat Sheet for that context: entity escaping
 * in element content (rule 1), `&#xHH;` for every non-alphanumeric character below 256 in an attribute value (rule
 * 2), `\xHH` in a JavaScript string (rule 3), and `\HHHHHH` in CSS (rule 4).
 */
function escapeContext(host, v) {
    if (host.kind === 'attr') return v.replace(LOW, (c) => `&#x${hex(c, 2)};`);
    if (host.name === 'script_string') return v.replace(LOW, (c) => `\\x${hex(c, 2)}`);
    if (host.name === 'style') return v.replace(LOW, (c) => `\\${hex(c, 6)}`);
    return escapeText(v);
}

/** The body of a JavaScript string literal whose value is `v`: JSON is a subset of JavaScript (ES2019). */
function jsBody(v) {
    return JSON.stringify(v).slice(1, -1);
}

/**
 * The double-quoted JavaScript string literal whose body starts at `text[start]` (ECMAScript 2025, 12.9.4, sloppy
 * mode, as in a classic script): its value and the index after its closing quote, or null if it does not end or
 * holds an invalid escape.
 */
function jsString(text, start) {
    const SINGLE = { b: '\b', f: '\f', n: '\n', r: '\r', t: '\t', v: '\v' };
    const isOctal = (c) => c >= '0' && c <= '7';
    let value = '';
    let i = start;

    while (i < text.length) {
        const c = text[i++];

        if (c === '"') return { value, end: i };
        if (c === '\n' || c === '\r') return null;
        if (c !== '\\') {
            value += c;
            continue;
        }
        if (i === text.length) return null;

        const e = text[i++];
        if (e === '\r') {
            // A line continuation: CR LF counts as one line terminator.
            if (text[i] === '\n') i++;
        } else if (e === '\n' || e === ' ' || e === ' ') {
            // A line continuation adds nothing.
        } else if (e in SINGLE) {
            value += SINGLE[e];
        } else if (e === 'x') {
            const digits = text.slice(i, i + 2);
            if (!/^[\dA-Fa-f]{2}$/.test(digits)) return null;
            value += String.fromCharCode(Number.parseInt(digits, 16));
            i += 2;
        } else if (e === 'u' && text[i] === '{') {
            const close = text.indexOf('}', i);
            const digits = close < 0 ? '' : text.slice(i + 1, close);
            if (!/^[\dA-Fa-f]+$/.test(digits) || Number.parseInt(digits, 16) > 0x10_ff_ff) return null;
            value += String.fromCodePoint(Number.parseInt(digits, 16));
            i = close + 1;
        } else if (e === 'u') {
            const digits = text.slice(i, i + 4);
            if (!/^[\dA-Fa-f]{4}$/.test(digits)) return null;
            value += String.fromCharCode(Number.parseInt(digits, 16));
            i += 4;
        } else if (e === '0' && !(text[i] >= '0' && text[i] <= '9')) {
            value += '\0';
        } else if (isOctal(e)) {
            // A legacy octal escape: up to three digits from 0-3, up to two from 4-7.
            let digits = e;
            while (digits.length < (e <= '3' ? 3 : 2) && isOctal(text[i])) digits += text[i++];
            value += String.fromCharCode(Number.parseInt(digits, 8));
        } else {
            // 8, 9, quotes, the backslash, and every other character escape themselves.
            value += e;
        }
    }

    return null;
}

/**
 * The JavaScript verdict of the script host, whose text the host writes as `var x = "…";`. INERT if the string
 * literal ends at the quote that the host wrote and holds `v`; ALTERED if it ends there and holds another value;
 * MALFORMED if the script does not compile, so that none of it runs; BREAKOUT if it compiles to other code. The
 * check compiles the script and never runs it.
 */
function jsVerdict(text, v) {
    const prefix = 'var x = "';

    if (text.startsWith(prefix)) {
        const literal = jsString(text, prefix.length);
        if (literal !== null && text.slice(literal.end) === ';') return literal.value === v ? 'INERT' : 'ALTERED';
    }
    try {
        new Script(text);
    } catch {
        return 'MALFORMED';
    }
    return 'BREAKOUT';
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

/** A matchertext error as its verdict; any other error is a defect of the driver. */
function verdictOf(error) {
    if (!(error instanceof MatchertextError)) throw error;
    if (error.code === 'matchertext-invalid-argument') return 'REJECTED';
    if (error.code.startsWith('matchertext-unserializable-')) return 'REFUSED';
    return 'MALFORMED';
}

// The documents of the auto-escaping arm for the current value, from the autoescape binary.
let autoDocs = null;

/** The document that a stock arm writes for `v`, as a string. */
function stockDocument(host, arm, v) {
    const [w0, w1] = host.wrap;

    switch (arm) {
        case 'concat': {
            return documentOf(
                host,
                host.kind === 'text'
                    ? element(host, w0 + v + w1)
                    : host.kind === 'attr'
                      ? attribute(host, v)
                      : `<!--${v}-->`,
            );
        }
        case 'escape': {
            return documentOf(
                host,
                host.kind === 'text' ? element(host, w0 + escapeText(v) + w1) : attribute(host, escapeAttribute(v)),
            );
        }
        case 'escape_context': {
            const escaped = escapeContext(host, v);
            return documentOf(host, host.kind === 'text' ? element(host, w0 + escaped + w1) : attribute(host, escaped));
        }
        case 'autoescape': {
            return autoDocs[host.name];
        }
    }
    return null;
}

/**
 * Write `v` into `host` by `arm`. Return the parsed document, or a verdict string if the arm refuses the value or
 * the parser refuses the document; and the browser job of the document that a browser receives, if any.
 */
function write(host, arm, v) {
    try {
        switch (arm) {
            case 'concat':
            case 'escape':
            case 'escape_context':
            case 'autoescape': {
                const html = stockDocument(host, arm, v);
                return { result: parse(html, STOCK), job: { kind: 'doc', html } };
            }
            case 'dom_stock':
            case 'dom_guarded': {
                const html = serialize(build(host, v), arm === 'dom_stock' ? STOCK : {});
                return { result: parse(html, STOCK), job: { kind: 'doc', html } };
            }
            case 'dom_mt': {
                return { result: parse(serialize(build(host, v), MT_OUTPUT), MT) };
            }
            case 'mt_verify':
            case 'mt_section':
            case 'mt_encode': {
                const html = matchertextDocument(host, arm, v);
                return { result: html === null ? 'REJECTED' : parse(html, MT) };
            }
            case 'compose': {
                return { result: composeHtml(COMPOSE[host.name](v)) };
            }
            case 'compose_js': {
                return { result: composeHtml(htmlTemplate`<script id=h [var x = "${jsBody(v)}";]>`) };
            }
            case 'compose_dom': {
                // The client path: the page composes the value and sets the default serializer's output of the html
                // element by innerHTML, which parses it as a fragment in that context.
                const root = composeHtml(COMPOSE[host.name](v)).childNodes.find((n) => n.nodeName === 'html');
                const html = serialize(root);
                return { result: parseFragment(root, html, STOCK), job: { kind: 'frag', html } };
            }
            case 'mt_utf8':
            case 'mt_sjis': {
                const html = matchertextDocument(host, 'mt_verify', v);
                if (html === null) return { result: 'REJECTED' };
                return { result: parse(new TextDecoder(BYTE_ARMS[arm]).decode(new TextEncoder().encode(html)), MT) };
            }
        }
    } catch (error) {
        return { result: verdictOf(error) };
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
        adapter.insertText(contentOf(hole), expected(host, v));
    } else {
        hole.attrs.push({ name: host.attr, value: v });
    }
    return document;
}

/** The node that holds the children of `element`: its content fragment for an HTML template. */
function contentOf(element) {
    return element.content ?? element;
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
            .map(({ name, value: v }) =>
                isHole && host.kind === 'attr' && name === host.attr ? `${name}=HOLE` : `${name}=${JSON.stringify(v)}`,
            )
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

const same = (a, b) => a.structure === b.structure && a.value === b.value;

/** The verdict of parsed `result` against the benign shape `base`, and the value in its hole. */
function judge(host, result, base, v) {
    if (typeof result === 'string') return { verdict: result, value: null };

    const { structure, value } = shape(host, result);
    if (structure !== base.structure) return { verdict: 'BREAKOUT', value };
    return { verdict: value === expected(host, v) ? 'INERT' : 'ALTERED', value };
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
        case 'compose_js': {
            // composeHtml writes no document; this is the document with the same hole.
            return matchertextDocument(host, 'mt_verify', jsBody(v));
        }
        case 'compose':
        case 'mt_utf8': {
            // composeHtml writes no document; the equivalent document is the one mt_verify writes. mt_utf8 sends
            // that document as bytes.
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

/**
 * Whether the default serializer writes tree `document` so that a parser without the option reads it back, and
 * the browser job of that output.
 */
function trip(host, document) {
    let html;

    try {
        html = serialize(document);
    } catch (error) {
        if (error instanceof MatchertextError) return { trip: 'REFUSED', job: null };
        throw error;
    }

    return {
        trip: same(shape(host, document), shape(host, parse(html, STOCK))) ? 'SAME' : 'DIFFERENT',
        job: { kind: 'doc', html },
    };
}

const pairs = [];
for (const [h, host] of HOSTS.entries()) {
    for (const [a, arm] of ARMS.entries()) {
        if (legal(host, arm)) pairs.push([h, a]);
    }
}

// The auto-escaping arm: one document per host for each value, from Go's html/template.
const autoescaper = spawn(autoescapePath, [], { stdio: ['pipe', 'pipe', 'inherit'] });
const autoLines = createInterface({ input: autoescaper.stdout, crlfDelay: Infinity })[Symbol.asyncIterator]();
async function autoescape(v) {
    autoescaper.stdin.write(`${JSON.stringify(v)}\n`);
    const { value, done } = await autoLines.next();
    if (done) fail('the autoescape binary stopped');
    return JSON.parse(value);
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

// Each html/template template must write the document of its host.
autoDocs = await autoescape(BENIGN);
for (const host of HOSTS) {
    if (!legal(host, 'autoescape')) continue;
    const want = shape(host, parse(stockDocument(host, 'concat', BENIGN), STOCK));
    if (!same(shape(host, parse(autoDocs[host.name], STOCK)), want)) {
        fail(`autoescape template of ${host.name} differs`);
    }
}

/** The verdict, the reader and trip columns, the JavaScript verdict, and the browser job of `v` in pair (h, a). */
function run(h, a, v) {
    const host = HOSTS[h];
    const arm = ARMS[a];
    const { result, job } = write(host, arm, v);
    const { verdict, value } = judge(host, result, base.get(`${h} ${a}`), v);
    const tripped = TRIP_ARMS.has(arm) && typeof result !== 'string' ? trip(host, result) : { trip: '-', job };
    const js = host.name === 'script_string' && value !== null && verdict !== 'BREAKOUT' ? jsVerdict(value, v) : '-';

    return {
        verdict,
        reader: TRIP_ARMS.has(arm) ? reader(host, arm, v, readerBase.get(`${h} ${a}`)) : '-',
        trip: tripped.trip,
        js,
        job: TRIP_ARMS.has(arm) ? tripped.job : job,
    };
}

// The benign value gives the structure every arm must keep. A baseline that is not inert means a broken host.
const base = new Map();
const readerBase = new Map();
for (const [h, a] of pairs) {
    const host = HOSTS[h];
    const arm = ARMS[a];
    const { result } = write(host, arm, BENIGN);
    if (typeof result === 'string') fail(`baseline ${host.name}/${arm} is ${result}`);
    const benign = shape(host, result);
    if (benign.value !== expected(host, BENIGN)) fail(`baseline ${host.name}/${arm} does not hold the value`);
    base.set(`${h} ${a}`, benign);
    if (TRIP_ARMS.has(arm)) readerBase.set(`${h} ${a}`, outline(parse(matchertextDocument(host, arm, BENIGN), STOCK)));
}

// The browsers judge each job against their own reading of the benign job of the same pair.
let engines = null;
let browserBase = null;
let browser = null;
if (flags.includes('--browsers')) {
    browser = await import('./browser.mjs');
    engines = await browser.openEngines();
    const benign = pairs.map(([h, a]) => [h, a, run(h, a, BENIGN)]).filter(([, , row]) => row.job);
    const trees = await engines.parse(benign.map(([, , row]) => row.job));
    browserBase = trees.map((results, e) => {
        const m = new Map();
        for (const [k, [h, a]] of benign.entries()) {
            const b = shape(HOSTS[h], results[k]);
            if (b.value !== expected(HOSTS[h], BENIGN)) fail(`browser ${e} baseline ${HOSTS[h].name}/${ARMS[a]} fails`);
            m.set(`${h} ${a}`, b);
        }
        return m;
    });
}

/** The verdict of a case in one browser, from that browser's reading of its job. */
function browserVerdict(h, a, v, row, tree, e) {
    return judge(HOSTS[h], tree, browserBase[e].get(`${h} ${a}`), v).verdict;
}

const out = (object) => process.stdout.write(`${JSON.stringify(object)}\n`);
const id = (list, name) => list.findIndex((item) => (item.name ?? item) === name);
const check = (hostName, armName, v) => run(id(HOSTS, hostName), id(ARMS, armName), v);

out({
    hosts: HOSTS.map(({ name, kind, semantic = false }) => ({ name, kind, semantic })),
    arms: ARMS.map((name) => ({ name, matchertext: MT_ARMS.has(name) })),
    browsers: engines ? engines.versions() : {},
});
out({
    selfcheck: {
        concat_breakout: check('attr_dq', 'concat', '"><b>x</b>').verdict,
        mt_rejects: check('text', 'mt_verify', 'x]>').verdict,
        compose_inert: check('text', 'compose', '<b>x</b>').verdict,
        dom_stock_breakout: check('script_string', 'dom_stock', '</script><b>x</b>').verdict,
        dom_guarded_refuses: check('script_string', 'dom_guarded', '</script><b>x</b>').verdict,
        dom_mt_inert: check('script_string', 'dom_mt', '</script><b>x</b>').verdict,
        dom_mt_refuses: check('script_string', 'dom_mt', '</script>]').verdict,
        encode_inert: check('text', 'mt_encode', 'x]> (y').verdict,
        js_breakout: check('script_string', 'mt_verify', '";alert(1)//').js,
        js_contained: check('script_string', 'compose_js', '";alert(1)//').js,
        js_escaped: check('script_string', 'escape_context', '";alert(1)//</script>').js,
    },
});

// Every PAGE_EVERY-th job that the browsers read by the inert write is read again by a page load, which must build
// the same tree; the run reports how many it checked and every difference.
const PAGE_EVERY = 50;
const pageCheck = { n: 0, differ: {}, examples: [] };
let inertJobs = 0;

// Rows wait for the browsers in batches of about BATCH distinct jobs; each value's rows stay in order.
let pending = [];
const jobIndex = new Map();
async function flush() {
    const jobs = [...jobIndex.keys()].map((key) => JSON.parse(key));
    const checked = engines
        ? [...jobs.keys()].filter((k) => !browser.byPageLoad(jobs[k]) && inertJobs++ % PAGE_EVERY === 0)
        : [];
    const loads = checked.map((k) => ({ ...jobs[k], load: true }));
    const trees = engines && jobs.length > 0 ? await engines.parse([...jobs, ...loads]) : null;

    for (const [c, k] of checked.entries()) {
        pageCheck.n++;
        trees.forEach((results, e) => {
            if (JSON.stringify(results[k]) === JSON.stringify(results[jobs.length + c])) return;
            const name = browser.ENGINES[e];
            pageCheck.differ[name] = (pageCheck.differ[name] ?? 0) + 1;
            if (pageCheck.examples.length < 5) pageCheck.examples.push({ engine: name, ...jobs[k] });
        });
    }

    for (const item of pending) {
        if (item.done !== undefined) {
            out(item);
            continue;
        }
        const { i, h, a, v, row } = item;
        const browsers =
            trees && row.job
                ? trees.map((results, e) => browserVerdict(h, a, v, row, results[jobIndex.get(row.key)], e))
                : '-';
        out({ i, h, a, verdict: row.verdict, reader: row.reader, trip: row.trip, js: row.js, browsers });
    }
    pending = [];
    jobIndex.clear();
}

let count = 0;
for await (const line of createInterface({ input: process.stdin, crlfDelay: Infinity })) {
    let message;

    try {
        message = JSON.parse(line);
    } catch {
        fail('malformed input line');
    }
    if (!Number.isInteger(message.i) || typeof message.v !== 'string') fail('malformed input object');

    const v = message.v;
    autoDocs = await autoescape(v);
    for (const [h, a] of pairs) {
        const row = run(h, a, v);
        if (engines && row.job) {
            row.key = JSON.stringify(row.job);
            if (!jobIndex.has(row.key)) jobIndex.set(row.key, jobIndex.size);
        }
        pending.push({ i: message.i, h, a, v, row });
    }
    pending.push({ i: message.i, done: pairs.length });
    count++;
    if (jobIndex.size >= BATCH || !engines) await flush();
}
await flush();
autoescaper.stdin.end();
if (engines) {
    await engines.close();
    out({ page_check: pageCheck });
}
out({ end: count });
