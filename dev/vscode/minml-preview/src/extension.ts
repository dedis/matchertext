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
  // Prefer a user-configured path, then the binary bundled alongside the
  // extension (installed by `make vscode-live-preview`), then fall back to PATH.
  const serverPath =
    config.get<string>("lspPath") ||
    path.join(context.extensionPath, "minml-lsp");
  const debug = config.get<boolean>("debug") || false;

  const args: string[] = [];
  if (debug) {
    args.push("--debug");
  }

  const serverOptions: ServerOptions = {
    run: { command: serverPath, args, transport: TransportKind.stdio },
    debug: { command: serverPath, args, transport: TransportKind.stdio },
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "minml" }],
  };

  client = new LanguageClient("minml", "MinML Language Server", serverOptions, clientOptions);
  context.subscriptions.push(client);
  client.start();

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

  private readonly _panel: vscode.WebviewPanel;
  private readonly _extensionUri: vscode.Uri;
  private _document: vscode.TextDocument;
  private _disposables: vscode.Disposable[] = [];

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
      (_) => {
        this._update();
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
    const csp = `default-src 'none'; img-src ${webview.cspSource} https: data:; script-src ${webview.cspSource} 'wasm-unsafe-eval'; style-src ${webview.cspSource}; connect-src ${webview.cspSource};`;

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
