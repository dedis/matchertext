// Parse HTML in Chromium, Firefox, and WebKit through Playwright, and return each document as a plain tree with
// the fields of a parse5 default tree (nodeName, tagName, namespaceURI, attrs, childNodes, value, data, content),
// so that the drivers judge browser and parse5 trees with one function.
//
// Every parse reads the document as a browser that renders the page: a document that contains `noscript` is loaded
// in a frame whose Content-Security-Policy blocks every script and fetch, since only `noscript` depends on the
// scripting flag, and any other document is written into a document without a browsing context, where the full
// parser runs and nothing is fetched or run. DOMParser is not used: in WebKit it takes a fast path that drops a
// valueless event-handler attribute before `>` and reads a document without a doctype in no-quirks mode.
//
// Jobs, one per input, each with the HTML string `html`:
//   {kind: 'doc', html}        the parsed document
//   {kind: 'frag', html}       a new document whose html element gets `html` by innerHTML (fragment parsing)
// A job with `load: true` is read by a page load in a frame, however it would be read otherwise; the drivers use
// it to check the inert write against a page load.
import { mkdirSync } from 'node:fs';
import { createServer } from 'node:http';
import { fileURLToPath } from 'node:url';
import * as playwright from 'playwright';

export const ENGINES = ['chromium', 'firefox', 'webkit'];

/** Whether `job` is read by a page load in a frame rather than by the inert write. */
export const byPageLoad = (job) => job.load === true || /noscript/i.test(job.html);
const FRAMES = 64;
// Upper bounds on a frame load and on one batch in one engine; past them the run fails.
const FRAME_MS = 20_000;
const BATCH_MS = 120_000;
// Jobs that a page parses before it yields to its event loop, so that the engine never finds it unresponsive.
const SLICE = 100;

// On macOS 27, Firefox does not start while it cannot read the app-data folder of an installed Firefox, which the
// system protects (microsoft/playwright#42768), so it gets a home folder of its own; the drivers run one Firefox
// at a time. The GPU process of Chromium exits at start (exit code 5) and takes the browser with it, so the GPU
// thread runs in the browser process.
const FIREFOX_HOME = fileURLToPath(new URL('../../../data/build/firefox-home', import.meta.url));

/** Launch engine `name`. */
export async function launch(name) {
    const options = {};

    if (process.platform === 'darwin' && name === 'firefox') {
        mkdirSync(FIREFOX_HOME, { recursive: true });
        options.env = { ...process.env, CFFIXED_USER_HOME: FIREFOX_HOME };
    }
    if (process.platform === 'darwin' && name === 'chromium') {
        options.args = ['--in-process-gpu'];
    }

    const browser = await playwright[name].launch(options);
    return { browser, close: () => browser.close() };
}

// The documents of the frames of one batch, which the driver registers before the batch: `GET /doc/<id>` serves
// one under a policy that blocks every script and fetch. It admits inline styles, which fetch and run nothing:
// Firefox empties the value of a `style` attribute that the policy blocks.
function serve(docs) {
    const server = createServer((req, res) => {
        const doc = req.url.startsWith('/doc/') ? docs.get(req.url.slice(5)) : undefined;
        if (doc !== undefined) {
            res.writeHead(200, {
                'Content-Type': 'text/html; charset=utf-8',
                'Content-Security-Policy': "default-src 'none'; style-src 'unsafe-inline'",
            }).end(doc);
            return;
        }
        res.writeHead(req.url === '/' ? 200 : 404, { 'Content-Type': 'text/html; charset=utf-8' }).end(
            '<!DOCTYPE html><title>harness</title>',
        );
    });

    return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(server)));
}

// Runs in the page. Installs `window.mtParse(jobs)`.
function install({ frames, frameMs, slice }) {
    const HTML = 'http://www.w3.org/1999/xhtml';

    function tree(n) {
        switch (n.nodeType) {
            case Node.TEXT_NODE:
            case Node.CDATA_SECTION_NODE: {
                return { nodeName: '#text', value: n.data };
            }
            case Node.COMMENT_NODE: {
                return { nodeName: '#comment', data: n.data };
            }
            case Node.DOCUMENT_TYPE_NODE: {
                return { nodeName: '#documentType', name: n.name };
            }
            case Node.DOCUMENT_NODE:
            case Node.DOCUMENT_FRAGMENT_NODE: {
                return {
                    nodeName: n.nodeType === Node.DOCUMENT_NODE ? '#document' : '#document-fragment',
                    childNodes: [...n.childNodes].map(tree),
                };
            }
            case Node.ELEMENT_NODE: {
                const e = {
                    nodeName: n.localName,
                    tagName: n.localName,
                    namespaceURI: n.namespaceURI,
                    attrs: [...n.attributes].map((a) => ({ name: a.localName, value: a.value })),
                    childNodes: [...n.childNodes].map(tree),
                };
                if (n.namespaceURI === HTML && n.localName === 'template') e.content = tree(n.content);
                return e;
            }
        }
        return { nodeName: `#node${n.nodeType}`, childNodes: [] };
    }

    // At most `frames` frames load at once. Each frame loads one document and is removed, so that no navigation
    // that a document starts after its load event reaches another document. A frame is not rendered, so the tree
    // is the parser's: Firefox's editor adds a `br` to an empty editable element of a rendered page.
    let free = frames;
    const waiting = [];
    async function slot() {
        if (free > 0) free--;
        else await new Promise((resolve) => waiting.push(resolve));
    }
    function release() {
        const resolve = waiting.shift();
        if (resolve) resolve();
        else free++;
    }

    /** Load registered document `id` in a frame with scripting on and nothing executed; give `use(document)`. */
    async function inFrame(id, use) {
        await slot();
        try {
            return await new Promise((resolve, reject) => {
                const frame = document.createElement('iframe');
                frame.style.display = 'none';
                const timer = setTimeout(() => {
                    frame.remove();
                    reject(new Error(`frame of document ${id} did not load`));
                }, frameMs);
                frame.onload = () => {
                    clearTimeout(timer);
                    try {
                        resolve(use(frame.contentDocument));
                    } catch (error) {
                        reject(error);
                    } finally {
                        frame.remove();
                    }
                };
                frame.src = `/doc/${id}`;
                document.body.append(frame);
            });
        } finally {
            release();
        }
    }

    /** Give `use(document)` for `html` as a page reads it: in the frame of the job if it has one. */
    function parse(job, html, use) {
        if (job.frame !== undefined) return inFrame(job.frame, use);
        const d = document.implementation.createHTMLDocument('');
        d.open();
        d.write(html);
        d.close();
        return use(d);
    }

    async function run(job) {
        switch (job.kind) {
            case 'doc': {
                return parse(job, job.html, tree);
            }
            case 'frag': {
                return parse(job, '', (d) => {
                    d.documentElement.innerHTML = job.html;
                    return tree(d);
                });
            }
        }
        throw new Error(`unknown job ${job.kind}`);
    }

    // The jobs run in slices, with a yield to the event loop between slices. One JSON string crosses the protocol
    // much faster than the trees as objects.
    window.mtParse = async (jobs) => {
        const results = [];
        for (let k = 0; k < jobs.length; k += slice) {
            results.push(...(await Promise.all(jobs.slice(k, k + slice).map(run))));
            await new Promise((resolve) => setTimeout(resolve, 0));
        }
        return JSON.stringify(results);
    };
}

/** One page per engine, each with the frame server. */
export async function openEngines() {
    const docs = new Map();
    const server = await serve(docs);
    const origin = `http://127.0.0.1:${server.address().port}/`;
    const engines = await Promise.all(ENGINES.map(async (name) => ({ name, handle: await launch(name) })));

    /**
     * Run `jobs` in engine `name` on a page of a new browser context, which the batch closes. An engine keeps the
     * memory of the documents that a page parses for the life of its context (WebKit's content process reached
     * 12 GB over one page or over many pages of one context, and was killed without a crash event).
     */
    async function batch({ name, handle }, jobs) {
        const context = await handle.browser.newContext();
        let timer;

        try {
            const page = await context.newPage();
            const failed = new Promise((_, reject) => {
                timer = setTimeout(() => reject(new Error(`${name} did not answer a batch`)), BATCH_MS);
                page.once('crash', () => reject(new Error(`${name} crashed`)));
            });
            const work = (async () => {
                await page.goto(origin);
                await page.evaluate(install, { frames: FRAMES, frameMs: FRAME_MS, slice: SLICE });
                return page.evaluate((j) => window.mtParse(j), jobs);
            })();
            return JSON.parse(await Promise.race([work, failed]));
        } finally {
            clearTimeout(timer);
            await context.close();
        }
    }

    return {
        /** The results of `jobs` in each engine, in the order of ENGINES. */
        async parse(jobs) {
            // A document that contains `noscript` loads in a frame; the frag job loads an empty one.
            const framed = jobs.map((job, k) => {
                if (!byPageLoad(job)) return job;
                docs.set(String(k), job.kind === 'doc' ? job.html : '');
                return { ...job, frame: String(k) };
            });
            try {
                return await Promise.all(engines.map((engine) => batch(engine, framed)));
            } finally {
                docs.clear();
            }
        },
        versions() {
            return Object.fromEntries(engines.map(({ name, handle }) => [name, handle.browser.version()]));
        },
        async close() {
            await Promise.all(engines.map(({ handle }) => handle.close()));
            server.close();
        },
    };
}
