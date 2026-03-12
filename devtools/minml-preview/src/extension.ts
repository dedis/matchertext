// The module 'vscode' contains the VS Code extensibility API
// Import the module and reference it with the alias vscode in your code below
import * as vscode from "vscode";

// This method is called when your extension is activated
// Your extension is activated the very first time the command is executed
export function activate(context: vscode.ExtensionContext) {
  const openPanel = vscode.commands.registerCommand("minml-preview.showPreview", () => {
    LivePreviewPanel.createOrShow(context.extensionUri);
  });

  context.subscriptions.push(openPanel);
}

function getWebviewOptions(extensionUri: vscode.Uri): vscode.WebviewOptions {
  const workspaceFolders = vscode.workspace.workspaceFolders?.map((folder) => folder.uri) || [];

  return {
    // Enable javascript in the webview
    enableScripts: true,

    // And restrict the webview to only loading content from our extension's `media` directory.
    localResourceRoots: [vscode.Uri.joinPath(extensionUri, "media"), ...workspaceFolders],
  };
}

/**
 * Manages live preview webview panel
 * This class was inspired from https://github.com/microsoft/vscode-extension-samples/blob/main/webview-sample/src/extension.ts
 */
class LivePreviewPanel {
  /**
   * Track the currently panel. Only allow a single panel to exist at a time.
   */
  public static currentPanel: LivePreviewPanel | undefined;

  public static readonly viewType = "MinMLPreview";

  private readonly _panel: vscode.WebviewPanel;
  private readonly _extensionUri: vscode.Uri;
  private _document: vscode.TextDocument;
  private _disposables: vscode.Disposable[] = [];

  public static createOrShow(extensionUri: vscode.Uri) {
    const editor = vscode.window.activeTextEditor;
    if (!editor || !this._isEditorValid(editor)) {
      return;
    }

    // If we already have a panel, show it.
    if (LivePreviewPanel.currentPanel) {
      LivePreviewPanel.currentPanel._document = editor.document;
      LivePreviewPanel.currentPanel._panel.reveal(vscode.ViewColumn.Beside);
      LivePreviewPanel.currentPanel._update();
      return;
    }

    // Otherwise, create a new panel.
    const panel = vscode.window.createWebviewPanel(
      LivePreviewPanel.viewType,
      "MinML Live Preview",
      vscode.ViewColumn.Beside,
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

    // Set the webview's initial html content
    this._update();

    // Listen for when the panel is disposed
    // This happens when the user closes the panel or when the panel is closed programmatically
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

    // Update the content based on view changes
    this._panel.onDidChangeViewState(
      () => {
        if (this._panel.visible) {
          this._update();
        }
      },
      null,
      this._disposables,
    );

    // Handle messages from the webview
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

    // Clean up our resources
    this._panel.dispose();

    while (this._disposables.length) {
      const x = this._disposables.pop();
      if (x) {
        x.dispose();
      }
    }
  }

  private static _isEditorValid(editor: vscode.TextEditor) {
    const currentFileExtension = editor?.document.fileName.split(".").pop();
    return currentFileExtension === "minml" || currentFileExtension === "m";
  }

  private _update() {
    const webview = this._panel.webview;
    const docFileName = this._document.fileName
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
    // And the uri we use to load this script in the webview
    const scriptUri = this._getMediaUri("main.js", webview);
    const wasmExecUri = this._getMediaUri("wasm_exec.js", webview);

    // Uri to load styles into webview
    const stylesMainUri = this._getMediaUri("vscode.css", webview);

    return `<!DOCTYPE html>
			<html lang="en">
			<head>
				<meta charset="UTF-8">
				<meta name="viewport" content="width=device-width, initial-scale=1.0">
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

  private _getMediaUri(path: string, webview: vscode.Webview) {
    const pathOnDisk = vscode.Uri.joinPath(this._extensionUri, "media", path);

    return webview.asWebviewUri(pathOnDisk);
  }
}

// This method is called when your extension is deactivated
export function deactivate() {}
