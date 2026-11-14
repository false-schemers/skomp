;;; (scheme base) and (scheme write) only: the floor every other test
;;; stands on.  Nothing here reaches outside the program.
(import (scheme base) (scheme write))

(define (p x) (write x) (newline))

;; numbers, at the edges the compiled runtime cares about
(p (+ 1 2 3))
(p (* 536870911 1))                     ; the largest fixnum
(p (- -536870912 0))                    ; the smallest (-2^29)
(p (quotient 17 5))
(p (remainder 17 5))
(p (modulo -17 5))
(p (/ 1.0 4))
(p (sqrt 16.0))

;; pairs, lists and vectors
(p (let loop ([i 0] [acc '()])
     (if (= i 5) (reverse acc) (loop (+ i 1) (cons (* i i) acc)))))
(p (vector-ref (vector 'a 'b 'c) 1))
(p (list->vector (list 1 2 3)))
(p (apply + (list 1 2 3 4)))

;; strings and characters
(p (string-append "ab" "cd" "ef"))
(p (string-length "hello"))
(p (substring "hello" 1 3))
(p (string->symbol "sym"))
(p (symbol->string 'sym))
(p (char->integer #\A))
(p (string->list "hi"))
(p (list->string (list #\o #\k)))
(p (string->number "42"))
(p (number->string 255 16))

;; closures, tail calls and higher order
(define (adder n) (lambda (x) (+ x n)))
(p ((adder 10) 5))
(p (map (lambda (x) (* 2 x)) (list 1 2 3)))
(define (count-down n) (if (= n 0) 'done (count-down (- n 1))))
(p (count-down 200000))                 ; proper tail calls, no stack growth

;; call/cc and dynamic-wind
(p (call-with-current-continuation (lambda (k) (+ 1 (k 41)))))
(p (let ([order '()])
     (dynamic-wind (lambda () (set! order (cons 'in order)))
                   (lambda () (set! order (cons 'body order)))
                   (lambda () (set! order (cons 'out order))))
     (reverse order)))

;; values
(p (call-with-values (lambda () (values 1 2 3)) list))

;; records
(define-record-type point (make-point x y) point? (x point-x) (y point-y))
(p (point? (make-point 1 2)))
(p (point-y (make-point 1 2)))

;; equality
(p (equal? (list 1 (vector 2 "3")) (list 1 (vector 2 "3"))))
(p (eqv? 'a 'a))

;; exceptions
(p (call-with-current-continuation
    (lambda (k)
      (with-exception-handler
       (lambda (e) (k (list 'caught (error-object-message e)
                            (error-object-irritants e))))
       (lambda () (error "boom" 1 2))))))
