import os

import sublime
from LSP.plugin import AbstractPlugin, register_plugin, unregister_plugin


class Minml(AbstractPlugin):
    """Starts minml-lsp for MinML views, configured by LSP-minml.sublime-settings."""

    @classmethod
    def name(cls):
        return "minml"

    @classmethod
    def additional_variables(cls):
        # $MINML_LSP_PATH, then the binary `make sublime-plugin` puts in this package,
        # then minml-lsp on PATH.
        exe = "minml-lsp.exe" if sublime.platform() == "windows" else "minml-lsp"
        path = os.environ.get("MINML_LSP_PATH", "")
        if not path:
            bundled = os.path.join(os.path.dirname(__file__), "bin", exe)
            path = bundled if os.access(bundled, os.X_OK) else exe
        return {"minml_lsp": path}


def plugin_loaded():
    register_plugin(Minml)


def plugin_unloaded():
    unregister_plugin(Minml)
