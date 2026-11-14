;;; The OS boundary with non-ASCII names.  Separate from t04 because the
;;; environment cannot be enumerated on the wasm target: WASI exposes getenv
;;; but wcpl's headers declare no environ, so get-environment-variables
;;; answers the empty list there.  These checks are for the native targets,
;;; and on Windows they are what exercises the wide entry points.
(import (scheme base) (scheme write) (scheme file) (scheme process-context))

(define (p l v) (display l) (display " => ") (write v) (newline))
(define lam (integer->char #x3BB))
(define nm (string-append "skomp-t05-" (string lam) ".tmp"))

(call-with-output-file nm (lambda (o) (write-string "ok" o)))
(p "a non-ASCII file name round trips"
   (and (file-exists? nm)
        (string=? "ok" (call-with-input-file nm
                         (lambda (i) (let loop ([a '()])
                           (let ([c (read-char i)])
                             (if (eof-object? c) (list->string (reverse a))
                                 (loop (cons c a))))))))))
(delete-file nm)
(p "and deletes" (file-exists? nm))

(p "the environment can be enumerated" (> (length (get-environment-variables)) 3))
(p "entries are pairs of strings"
   (let ([e (car (get-environment-variables))])
     (and (pair? e) (string? (car e)) (string? (cdr e)))))
(p "a name that is not set" (get-environment-variable "SKOMP_NO_SUCH_VAR_XYZ"))
