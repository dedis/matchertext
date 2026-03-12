(function () {
  const vscode = acquireVsCodeApi();
  const go = new Go();

  async function initWasm(uri) {
    try {
      const result = await WebAssembly.instantiateStreaming(fetch(uri), go.importObject);
      go.run(result.instance);
      const minmlTestString = `
                div{style=[flex: 1 1 500px; background-color: #b3d0ff]}[
                    h1[This is just a test file to make sure the wasm is functioning correctly]
                    h2{style=[color:red]}[If this is red and looking like normal HTML then it's working pretty well]
                ]
            `;
      try {
        // Call the Go function registered in WASM
        const html = minmlConvert(minmlTestString);
        document.getElementById("content").innerHTML = html;
      } catch (err) {
        console.error("Conversion error", err);
        vscode.postMessage({ command: "alert", message: err.message });
        document.getElementById("content").innerHTML =
          `<pre style="color: var(--vscode-errorForeground)">Error: ${err.message}</pre>`;
      }
      console.log("MinML WASM initialized");
    } catch (err) {
      vscode.postMessage({ command: "alert", message: err.message });
      console.error("Failed to initialize WASM", err);
    }
  }

  // Handle messages from the extension
  window.addEventListener("message", (event) => {
    const message = event.data;
    switch (message.command) {
      case "init":
        initWasm(message.wasmUri);
        break;
    }
  });

  vscode.postMessage({ command: "ready" });
})();
