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

(defun minml--syntax-propertize (start end)
  "Mark each comment between START and END as a generic comment.
A comment runs from the \"-\" of \"-[\" to the \"]\" that balances its \"[\".
The \"-\" must be a whole element name: at the start of a line, or after
whitespace or a matcher, with an optional space sucker \"<\" in between."
  (remove-text-properties start end '(minml-open nil))
  (goto-char start)
  (while (search-forward "-[" end t)
    (let* ((dash (- (point) 2))
           (before (if (eq (char-before dash) ?<) (1- dash) dash))
           (close (and (or (= before (point-min))
                           (memq (char-before before) '(?\s ?\t ?\n ?\r ?\[ ?\] ?\( ?\) ?\{ ?\})))
                       ;; Only the matchers count, not the comments marked so far.
                       (let ((parse-sexp-lookup-properties nil))
                         (condition-case nil (scan-lists (1+ dash) 1 0)
                           (scan-error 'open))))))
      (cond
       ((not close))
       ((eq close 'open)
        ;; Text after it can still close it.
        (put-text-property dash (1+ dash) 'minml-open t))
       (t
        (put-text-property dash (1+ dash) 'syntax-table (string-to-syntax "!"))
        (put-text-property (1- close) close 'syntax-table (string-to-syntax "!"))
        ;; A later chunk that starts inside the comment propertizes all of it again.
        (put-text-property dash close 'syntax-multiline t)
        (goto-char close))))))

(defvar-local minml--matcher-deleted nil
  "Whether the text that the current change deletes has a matcher.")

(defun minml--before-change (beg end)
  "Record whether the change deletes a matcher between BEG and END."
  (setq minml--matcher-deleted
        (save-excursion (goto-char beg) (re-search-forward "[][(){}]" end t))))

(defun minml--after-change (beg end _len)
  "Propertize again from the first comment whose end the change at BEG can move.
Only a change of matchers can: it can end the comment BEG is in, or an unclosed
\"-[\" before BEG.  END is the end of the inserted text."
  (when (or minml--matcher-deleted
            (save-excursion (goto-char beg) (re-search-forward "[][(){}]" end t)))
    (let ((open (text-property-any (point-min) beg 'minml-open t))
          (ppss (save-excursion (syntax-ppss beg))))
      (syntax-ppss-flush-cache (min (or open beg) (if (nth 4 ppss) (nth 8 ppss) beg))))))

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
  ;; A comment ends at the "]" that balances its "-[", so brackets inside need no quoting.
  (setq-local comment-quote-nested nil)
  (setq-local syntax-propertize-function #'minml--syntax-propertize)
  (add-hook 'syntax-propertize-extend-region-functions
            #'syntax-propertize-multiline 'append t)
  (add-hook 'before-change-functions #'minml--before-change nil t)
  (add-hook 'after-change-functions #'minml--after-change nil t)
  ;; Brackets in a comment do not count, because its closing "]" is a comment fence.
  (setq-local parse-sexp-ignore-comments t)
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
