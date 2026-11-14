;;; Libraries beyond (scheme base).  Each of these is served from the
;;; compiler's own clib/ tree and from nowhere else; (srfi 2), (srfi 69)
;;; and (srfi 132) were borrowed from the host's library tree until they
;;; were packaged there, so this test is what says the packaging holds.
(import (scheme base) (scheme write)
        (only (srfi 1) fold iota delete-duplicates last)
        (srfi 2)                                ; and-let*
        (only (srfi 26) cut <>)
        (only (srfi 69) make-hash-table hash-table-set! hash-table-ref/default
                        hash-table-size)
        (only (srfi 132) list-sort vector-sort!)
        (only (srfi 111) box unbox)
        (only (srfi 158) make-range-generator generator->list))

(define (p x) (write x) (newline))

;; (srfi 1): had a clib file all along
(p (fold + 0 (iota 10)))
(p (delete-duplicates (list 1 2 1 3 2)))
(p (last (list 1 2 3)))

;; (srfi 2): was borrowed
(p (and-let* ([x 5] [(> x 1)] [y (* x 2)]) y))
(p (and-let* ([x 0] [(> x 1)]) 'unreachable))

;; (srfi 26)
(p (map (cut * 3 <>) (list 1 2 3)))

;; (srfi 69): a hash table over equal?
(p (let ([h (make-hash-table equal?)])
     (hash-table-set! h "k" 1)
     (hash-table-set! h (list 1 2) 2)
     (list (hash-table-ref/default h "k" 'none)
           (hash-table-ref/default h (list 1 2) 'none)
           (hash-table-ref/default h 'absent 'none)
           (hash-table-size h))))

;; (srfi 132): sorting
(p (list-sort < (list 5 1 4 2 3)))
(p (let ([v (vector 5 1 4 2 3)]) (vector-sort! < v) v))

;; (srfi 111): boxes
(p (unbox (box 'in-a-box)))

;; (srfi 158): generators
(p (generator->list (make-range-generator 0 5)))
