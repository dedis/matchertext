// The module 'vscode' contains the VS Code extensibility API
// Import the module and reference it with the alias vscode in your code below
import * as vscode from "vscode";

// This method is called when your extension is activated
// Your extension is activated the very first time the command is executed
export function activate(context: vscode.ExtensionContext) {
  // Use the console to output diagnostic information (console.log) and errors (console.error)
  // This line of code will only be executed once when your extension is activated
  console.log('Congratulations, your extension "minml-preview" is now active!');

  // The command has been defined in the package.json file
  // Now provide the implementation of the command with registerCommand
  // The commandId parameter must match the command field in package.json
  // const disposable = vscode.commands.registerCommand(
  //   "minml-preview.helloWorld",
  //   () => {
  //     // The code you place here will be executed every time your command is executed
  //     // Display a message box to the user
  //     vscode.window.showInformationMessage(
  //       "Hello World from minml-livepreview!",
  //     );
  //   },
  // );

  const openPanel = vscode.commands.registerCommand("minml-preview.showPreview", () => {
    LivePreviewPanel.createOrShow(context.extensionUri);
  });

  context.subscriptions.push(openPanel);
}

function getWebviewOptions(extensionUri: vscode.Uri): vscode.WebviewOptions {
  return {
    // Enable javascript in the webview
    enableScripts: true,

    // And restrict the webview to only loading content from our extension's `media` directory.
    localResourceRoots: [vscode.Uri.joinPath(extensionUri, "media")],
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
  private _disposables: vscode.Disposable[] = [];

  public static createOrShow(extensionUri: vscode.Uri) {
    // If we already have a panel, show it.
    if (LivePreviewPanel.currentPanel) {
      LivePreviewPanel.currentPanel._panel.reveal(vscode.ViewColumn.Beside);
      return;
    }

    // Otherwise, create a new panel.
    const panel = vscode.window.createWebviewPanel(
      LivePreviewPanel.viewType,
      "MinML Live Preview",
      vscode.ViewColumn.Beside,
      getWebviewOptions(extensionUri),
    );

    LivePreviewPanel.currentPanel = new LivePreviewPanel(panel, extensionUri);
  }

  public static revive(panel: vscode.WebviewPanel, extensionUri: vscode.Uri) {
    LivePreviewPanel.currentPanel = new LivePreviewPanel(panel, extensionUri);
  }

  private constructor(panel: vscode.WebviewPanel, extensionUri: vscode.Uri) {
    this._panel = panel;
    this._extensionUri = extensionUri;

    // Set the webview's initial html content
    this._update();

    // Listen for when the panel is disposed
    // This happens when the user closes the panel or when the panel is closed programmatically
    this._panel.onDidDispose(() => this.dispose(), null, this._disposables);

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

  private _update() {
    const webview = this._panel.webview;

    this._panel.title = "Test Panel";
    this._panel.webview.html = this._getHtmlForWebview(webview);
  }

  private _getHtmlForWebview(webview: vscode.Webview) {
    // And the uri we use to load this script in the webview
    const scriptUri = this._getMediaUri("main.js");
    const wasmExecUri = this._getMediaUri("wasm_exec.js");

    // Uri to load styles into webview
    const stylesResetUri = this._getMediaUri("reset.css");
    const stylesMainUri = this._getMediaUri("vscode.css");

    return `<!DOCTYPE html>
			<html lang="en">
			<head>
				<meta charset="UTF-8">
				<meta name="viewport" content="width=device-width, initial-scale=1.0">
				<link href="${stylesResetUri}" rel="stylesheet">
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

  private _getMediaUri(path: string) {
    const webview = this._panel.webview;
    const pathOnDisk = vscode.Uri.joinPath(this._extensionUri, "media", path);

    return webview.asWebviewUri(pathOnDisk);
  }
}

// This method is called when your extension is deactivated
export function deactivate() {}
