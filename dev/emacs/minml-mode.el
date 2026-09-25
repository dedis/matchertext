;;; minml-mode.el --- Major mode for MinML  -*- lexical-binding: t; -*-

;; Package-Requires: ((emacs "29.1"))
;; URL: https://github.com/dedis/matchertext

;;; Commentary:

;; Editing support for MinML through the minml-lsp language server and eglot:
;; diagnostics, hover, completion, and, from Emacs 31, semantic highlighting.
;; The server sends no tokens for brackets, so this mode colors them itself.

;;; Code:

(require 'eglot)

(defgroup minml nil
  "MinML markup."
  :group 'languages)

(defcustom minml-lsp-path nil
  "Path to the minml-lsp binary.
When nil, use $MINML_LSP_PATH, then the binary `make emacs-plugin' puts
beside this file, then minml-lsp on the variable `exec-path'."
  :type '(choice (const :tag "Automatic" nil) file))

(defcustom minml-start-eglot t
  "Whether `minml-mode' starts the language server with `eglot-ensure'."
  :type 'boolean)

(defconst minml--directory (file-name-directory (or load-file-name buffer-file-name)))

(defun minml--server-command (&optional _interactive _project)
  "Return the command that starts the MinML language server."
  (let ((env (getenv "MINML_LSP_PATH"))
        (bundled (expand-file-name "bin/minml-lsp" minml--directory)))
    (list (cond (minml-lsp-path)
                ((and env (not (string-empty-p env))) env)
                ((file-executable-p bundled) bundled)
                (t "minml-lsp")))))

(defvar minml-mode-syntax-table
  (let ((table (make-syntax-table text-mode-syntax-table)))
    (modify-syntax-entry ?\[ "(]" table)
    (modify-syntax-entry ?\] ")[" table)
    (modify-syntax-entry ?\{ "(}" table)
    (modify-syntax-entry ?\} "){" table)
    (modify-syntax-entry ?\( "()" table)
    (modify-syntax-entry ?\) ")(" table)
    table)
  "Syntax table for `minml-mode'.")

(defvar minml-font-lock-keywords
  '(("[][{}()]" . 'font-lock-bracket-face))
  "Highlighting that the language server leaves to the editor.")

;;;###autoload
(define-derived-mode minml-mode text-mode "MinML"
  "Major mode for MinML markup."
  (setq-local comment-start "-[")
  (setq-local comment-end "]")
  (setq-local comment-start-skip "-\\[\\s-*")
  (setq-local comment-end-skip "\\s-*\\]")
  (setq-local font-lock-defaults '(minml-font-lock-keywords))
  (when minml-start-eglot
    (eglot-ensure)))

(add-to-list 'eglot-server-programs '(minml-mode . minml--server-command))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.minml\\'" . minml-mode))

;;;###autoload
(defun minml--m-file-p ()
  "Whether the current .m file is MinML.
Objective-C and MATLAB also use .m, so a .m file is MinML only if its first
non-blank line starts with an element, attributes, or a MinML construct."
  (and buffer-file-name
       (string-suffix-p ".m" buffer-file-name)
       (save-excursion
         (skip-chars-forward " \t\r\n")
         (looking-at "<?[[:alnum:]_:.-]+[[{]\\|[-+?\"'][[]"))))

;;;###autoload
(add-to-list 'magic-mode-alist '(minml--m-file-p . minml-mode))

(provide 'minml-mode)
;;; minml-mode.el ends here
