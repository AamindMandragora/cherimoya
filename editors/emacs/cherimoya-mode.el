;;; cherimoya-mode.el --- Major mode for cherimoya, run by cheri's language server  -*- lexical-binding: t -*-

;; Version: 0.1.0
;; Package-Requires: ((emacs "29.1"))
;; Keywords: languages
;; URL: https://github.com/AamindMandragora/cherimoya

;;; Commentary:

;; edits .moya files, and .toya files of tests, and starts cheri's language server for them, with eglot, built
;; into emacs, or lsp-mode, if it's installed, which gives emacs the server's diagnostics, hovers, definitions,
;; references, renames, outline, completion, signature help, quick fixes, and inlay hints, which lsp-mode shows
;; with lsp-inlay-hint-enable, and under lsp-mode, a Run test lens above every test. the server colors the code
;; too, through semantic tokens, which eglot reads from emacs 31 and lsp-mode with lsp-semantic-tokens-enable, so
;; the mode itself only knows comments, strings, brackets, and indentation

;;; Code:

(defgroup cherimoya nil
  "Cherimoya, run by cheri's language server."
  :group 'languages
  :prefix "cherimoya-")

(defcustom cherimoya-compiler-path "cheri"
  "The cheri executable, which is started as cheri lsp."
  :type 'string)

(defcustom cherimoya-indent-offset 4
  "How many columns each scope in braces indents."
  :type 'integer)

(defun cherimoya-server-command (&optional _interactive)
  "The command that starts cheri's language server."
  (list cherimoya-compiler-path "lsp"))

;; # starts a comment that runs to the end of the line, and ' quotes a char literal the way " quotes a string
(defvar cherimoya-mode-syntax-table
  (let ((table (make-syntax-table)))
    (modify-syntax-entry ?# "<" table)
    (modify-syntax-entry ?\n ">" table)
    (modify-syntax-entry ?' "\"" table)
    (modify-syntax-entry ?_ "_" table)
    table)
  "Syntax table for `cherimoya-mode'.")

(defun cherimoya-indent-line ()
  "Indent the current line by how many braces are open at its start.
A line that starts by closing a brace is one level out."
  (interactive)
  (let* ((open (nth 9 (save-excursion (syntax-ppss (line-beginning-position)))))
         (depth (seq-count (lambda (position) (eq (char-after position) ?{)) open))
         (closes (save-excursion (back-to-indentation) (eq (char-after) ?}))))
    (save-excursion (indent-line-to (* cherimoya-indent-offset (max 0 (if closes (1- depth) depth)))))
    (when (< (current-column) (current-indentation)) (back-to-indentation))))

;;;###autoload
(define-derived-mode cherimoya-mode prog-mode "Cherimoya"
  "Major mode for cherimoya, which cheri's language server checks and colors."
  (setq-local comment-start "# "
              comment-end ""
              comment-start-skip "#+\\s-*"
              indent-line-function #'cherimoya-indent-line
              indent-tabs-mode nil
              electric-indent-chars (cons ?} electric-indent-chars)))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.[mt]oya\\'" . cherimoya-mode))

(defvar eglot-server-programs)
(with-eval-after-load 'eglot
  (add-to-list 'eglot-server-programs '(cherimoya-mode . cherimoya-server-command)))

(declare-function lsp-register-client "lsp-mode")
(declare-function make-lsp-client "lsp-mode")
(declare-function lsp-stdio-connection "lsp-mode")
(declare-function lsp-activate-on "lsp-mode")
(declare-function lsp--uri-to-path "lsp-mode")
(defvar lsp-language-id-configuration)

(defun cherimoya-run-test (action)
  "Run the test a Run test lens names with `compile'.
ACTION carries the test's document and name."
  (let ((arguments (gethash "arguments" action)))
    (compile (mapconcat #'shell-quote-argument
                        (list cherimoya-compiler-path "test" (lsp--uri-to-path (aref arguments 0)) (aref arguments 1) "--exact")
                        " "))))

(with-eval-after-load 'lsp-mode
  (add-to-list 'lsp-language-id-configuration '(cherimoya-mode . "cherimoya"))
  ;; the server's Run test lenses name a command lsp-mode runs here, since running a test is the editor's job
  (let ((handlers (make-hash-table :test #'equal)))
    (puthash "cherimoya.runTest" #'cherimoya-run-test handlers)
    (lsp-register-client
     (make-lsp-client :new-connection (lsp-stdio-connection (lambda () (cherimoya-server-command)))
                      :activation-fn (lsp-activate-on "cherimoya")
                      :action-handlers handlers
                      :server-id 'cheri))))

(provide 'cherimoya-mode)
;;; cherimoya-mode.el ends here
