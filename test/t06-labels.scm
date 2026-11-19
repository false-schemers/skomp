;;; Datum labels.  R7RS draws three lines here and the compiled runtime now
;;; draws all three: `write' must label circular structure and must leave
;;; plain sharing alone, `write-shared' must label both, and `write-simple'
;;; may label neither and may diverge on a cycle.
;;;
;;; The cases are built with vector-set! rather than set-cdr!, which is not
;;; in (scheme base); a tail reachable from two spines covers the pair path,
;;; where a label has to break the list open and print the tail as a cdr.
(import (scheme base) (scheme write))

(define (p l v) (display l) (display " => ") (write v) (newline))
(define (s->string proc x)
  (let ([o (open-output-string)]) (proc x o) (get-output-string o)))

;; sharing without a cycle: one list reachable by two paths
(define leaf (list 1 2))
(define shared (list leaf leaf))

;; a cycle
(define cyc (vector 1 2))
(vector-set! cyc 0 cyc)

;; a tail reachable from two spines
(define tail (list 3 4))
(define two (list (cons 1 tail) (cons 2 tail)))

(p "write leaves plain sharing alone" (s->string write shared))
(p "write-shared labels it"           (s->string write-shared shared))
(p "write-simple leaves it alone"     (s->string write-simple shared))
(p "write labels a cycle"             (s->string write cyc))
(p "write-shared labels a cycle"      (s->string write-shared cyc))
(p "write leaves a shared tail alone" (s->string write two))
(p "write-shared opens the spine"     (s->string write-shared two))

;; the scan must not disturb equal?, which shares its table
(p "equal? still works after a scan" (equal? shared (list (list 1 2) (list 1 2))))
(p "and interleaved with write-shared"
   (let ([a (s->string write-shared two)])
     (and (equal? two (list (cons 1 (list 3 4)) (cons 2 (list 3 4))))
          (string=? a (s->string write-shared two)))))
