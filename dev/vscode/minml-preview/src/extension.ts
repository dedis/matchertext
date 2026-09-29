import * as fs from "fs";
import * as path from "path";
import * as vscode from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
} from "vscode-languageclient/node";

let client: LanguageClient;

export function activate(context: vscode.ExtensionContext) {
  const config = vscode.workspace.getConfiguration("minml");

  const binaryName = process.platform === "win32" ? "minml-lsp.exe" : "minml-lsp";

  // Prefer a user-configured path, then the binary bundled alongside the
  // extension (installed by `make vscode-live-preview`), then fall back to PATH.
  const bundled = path.join(context.extensionPath, binaryName);
  const serverPath =
    config.get<string>("lspPath") || (fs.existsSync(bundled) ? bundled : binaryName);

  const debug = config.get<boolean>("debug") || false;

  const args: string[] = [];
  if (debug) {
    args.push("--debug");
  }

  const serverOptions: ServerOptions = {
    run: { command: serverPath, args, transport: TransportKind.stdio },
    debug: { command: serverPath, args: [...args, "--debug"], transport: TransportKind.stdio },
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "minml" }],
  };

  client = new LanguageClient("minml", "MinML Language Server", serverOptions, clientOptions);
  context.subscriptions.push(client);

  client.start().catch((err) => {
    vscode.window.showErrorMessage(`Failed to start MinML Language Server: ${err}`);
  });

  if (vscode.window.registerWebviewPanelSerializer) {
    // Make sure we register a serializer in activation
    vscode.window.registerWebviewPanelSerializer(LivePreviewPanel.viewType, {
      async deserializeWebviewPanel(webviewPanel: vscode.WebviewPanel, state: any) {
        console.log(`Reviving MinML preview panel from state: ${state}`);
        // We need an active editor to revive properly, or we can try to find the document
        const editor = vscode.window.activeTextEditor;
        if (editor && editor.document.languageId === "minml") {
          LivePreviewPanel.revive(webviewPanel, context.extensionUri, editor.document);
        } else {
          // If no active editor, we might not be able to revive fully, but we can at least set it up
          // For now, just dispose if we can't find a document to show
          webviewPanel.dispose();
        }
      },
    });
  }

  context.subscriptions.push(
    vscode.commands.registerCommand("minml-preview.showPreview", () => {
      LivePreviewPanel.createOrShow(context.extensionUri, vscode.ViewColumn.Active);
    }),
  );

  context.subscriptions.push(
    vscode.commands.registerCommand("minml-preview.showPreviewToSide", () => {
      LivePreviewPanel.createOrShow(context.extensionUri, vscode.ViewColumn.Beside);
    }),
  );

  context.subscriptions.push(
    vscode.commands.registerTextEditorCommand("minml.toggleComment", toggleComment),
  );
}

// A MinML comment ends at the "]" that balances its "-[". VS Code's comment commands end it at
// the first "]" instead, so they break lines with brackets; the keys for both run this command.
// It comments or uncomments each selection, or the line of each empty selection.
function toggleComment(editor: vscode.TextEditor, edit: vscode.TextEditorEdit) {
  const doc = editor.document;
  const lines = new Set<number>();
  for (const sel of editor.selections) {
    if (sel.isEmpty && lines.has(sel.active.line)) {
      continue;
    }
    lines.add(sel.active.line);
    const range = sel.isEmpty ? doc.lineAt(sel.active.line).range : sel;
    const text = doc.getText(range);
    const start = text.length - text.trimStart().length;
    const end = text.trimEnd().length;
    if (start >= end) {
      continue;
    }
    const body = text.slice(start, end);
    const offset = doc.offsetAt(range.start);
    edit.replace(
      new vscode.Range(doc.positionAt(offset + start), doc.positionAt(offset + end)),
      isComment(body) ? body.slice(2, -1).replace(/^ /, "").replace(/ $/, "") : `-[ ${body} ]`,
    );
  }
}

// isComment tells whether text is one comment: "-[", text that does not close it, and "]".
// The text between need not balance, so that commenting lines such as "p[" and uncommenting
// them restores them.
function isComment(text: string): boolean {
  if (!text.startsWith("-[") || !text.endsWith("]")) {
    return false;
  }
  let depth = 0;
  for (let i = 1; i < text.length - 1; i++) {
    if ("([{".includes(text[i])) {
      depth++;
    } else if (")]}".includes(text[i]) && --depth === 0) {
      return false;
    }
  }
  return true;
}

export function deactivate(): Thenable<void> | undefined {
  if (!client) {
    return undefined;
  }
  return client.stop();
}

function getWebviewOptions(extensionUri: vscode.Uri): vscode.WebviewOptions {
  const workspaceFolders = vscode.workspace.workspaceFolders?.map((folder) => folder.uri) || [];

  return {
    enableScripts: true,
    localResourceRoots: [vscode.Uri.joinPath(extensionUri, "media"), ...workspaceFolders],
  };
}

class LivePreviewPanel {
  public static currentPanel: LivePreviewPanel | undefined;
  public static readonly viewType = "MinMLPreview";
  private static readonly debounceMs = 100;

  private readonly _panel: vscode.WebviewPanel;
  private readonly _extensionUri: vscode.Uri;
  private _document: vscode.TextDocument;
  private _disposables: vscode.Disposable[] = [];
  private _pending: NodeJS.Timeout | undefined;

  public static createOrShow(extensionUri: vscode.Uri, viewColumn: vscode.ViewColumn) {
    const editor = vscode.window.activeTextEditor;
    if (!editor || !this._isEditorValid(editor)) {
      return;
    }

    if (LivePreviewPanel.currentPanel) {
      LivePreviewPanel.currentPanel._document = editor.document;
      LivePreviewPanel.currentPanel._panel.reveal(viewColumn);
      LivePreviewPanel.currentPanel._update();
      return;
    }

    const panel = vscode.window.createWebviewPanel(
      LivePreviewPanel.viewType,
      "MinML Live Preview",
      viewColumn,
      getWebviewOptions(extensionUri),
    );

    LivePreviewPanel.currentPanel = new LivePreviewPanel(panel, extensionUri, editor.document);
  }

  public static revive(
    panel: vscode.WebviewPanel,
    extensionUri: vscode.Uri,
    document: vscode.TextDocument,
  ) {
    LivePreviewPanel.currentPanel = new LivePreviewPanel(panel, extensionUri, document);
  }

  private constructor(
    panel: vscode.WebviewPanel,
    extensionUri: vscode.Uri,
    document: vscode.TextDocument,
  ) {
    this._panel = panel;
    this._extensionUri = extensionUri;
    this._document = document;

    this._update();

    this._panel.onDidDispose(() => this.dispose(), null, this._disposables);

    vscode.window.onDidChangeActiveTextEditor(
      (e) => {
        if (!e || !LivePreviewPanel._isEditorValid(e)) {
          return;
        }
        this._document = e.document;
        this._update();
      },
      null,
      this._disposables,
    );

    vscode.workspace.onDidChangeTextDocument(
      (e) => {
        if (e.document !== this._document) {
          return;
        }
        // Convert once typing pauses rather than on every keystroke
        clearTimeout(this._pending);
        this._pending = setTimeout(() => this._update(), LivePreviewPanel.debounceMs);
      },
      null,
      this._disposables,
    );

    this._panel.onDidChangeViewState(
      () => {
        if (this._panel.visible) {
          this._update();
        }
      },
      null,
      this._disposables,
    );

    this._panel.webview.onDidReceiveMessage(
      (message) => {
        switch (message.command) {
          case "alert":
            vscode.window.showErrorMessage(message.message);
            return;
          case "ready":
            const wasmUri = this._panel.webview.asWebviewUri(
              vscode.Uri.joinPath(this._extensionUri, "media", "main.wasm"),
            );
            this._panel.webview.postMessage({
              command: "init",
              wasmUri: wasmUri.toString(),
            });
            this._update();
            return;
        }
      },
      null,
      this._disposables,
    );
  }

  public dispose() {
    clearTimeout(this._pending);
    LivePreviewPanel.currentPanel = undefined;
    this._panel.dispose();
    while (this._disposables.length) {
      const x = this._disposables.pop();
      if (x) {
        x.dispose();
      }
    }
  }

  private static _isEditorValid(editor: vscode.TextEditor) {
    return editor?.document.languageId === "minml";
  }

  private _update() {
    const webview = this._panel.webview;
    const docFileName = this._document.fileName;
    const filename = docFileName.split("/").pop();
    this._panel.title = `Preview: ${filename}`;

    const baseDir = vscode.Uri.file(docFileName).with({
      path: docFileName.substring(0, docFileName.lastIndexOf("/")),
    });

    this._panel.webview.html = this._getHtmlForWebview(webview, webview.asWebviewUri(baseDir));

    this._panel.webview.postMessage({
      command: "update",
      content: this._document.getText(),
    });
  }

  private _getHtmlForWebview(webview: vscode.Webview, baseUri: vscode.Uri) {
    const scriptUri = this._getMediaUri("main.js", webview);
    const wasmExecUri = this._getMediaUri("wasm_exec.js", webview);
    const stylesMainUri = this._getMediaUri("vscode.css", webview);
    const csp = `default-src 'none'; img-src ${webview.cspSource} https: data:; script-src ${webview.cspSource} 'wasm-unsafe-eval'; style-src ${webview.cspSource} 'unsafe-inline'; connect-src ${webview.cspSource};`;

    return `<!DOCTYPE html>
			<html lang="en">
			<head>
				<meta charset="UTF-8">
				<meta name="viewport" content="width=device-width, initial-scale=1.0">
				<meta http-equiv="Content-Security-Policy" content="${csp}">
				<base href="${baseUri}/">
				<link href="${stylesMainUri}" rel="stylesheet">
				<title>MinML Live Preview</title>
				<script src="${wasmExecUri}"></script>
			</head>
			<body>
				<div id="content"></div>
				<script src="${scriptUri}"></script>
			</body>
			</html>`;
  }

  private _getMediaUri(filePath: string, webview: vscode.Webview) {
    const pathOnDisk = vscode.Uri.joinPath(this._extensionUri, "media", filePath);
    return webview.asWebviewUri(pathOnDisk);
  }
}
