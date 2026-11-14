;;; Files, ports and the reader: (scheme file), (scheme read) and the
;;; string ports.  This is also where the runtime's buffered input and
;;; block output are exercised, since a file long enough to refill the
;;; buffer goes through both.
(import (scheme base) (scheme write) (scheme read) (scheme file))

(define (p x) (write x) (newline))
(define tmp "skomp-t02.tmp")

;; write a file longer than the runtime's input buffer (8 KiB), so that
;; reading it back crosses a refill
(define line "the quick brown fox jumps over it")   ; 32 chars + newline
(call-with-output-file tmp
  (lambda (out)
    (do ([i 0 (+ i 1)]) ((= i 400))
      (write-string line out)
      (newline out))))

;; read-char, counting, with a checksum that depends on every byte
(p (call-with-input-file tmp
     (lambda (in)
       (let loop ([n 0] [sum 0])
         (let ([c (read-char in)])
           (if (eof-object? c)
               (list n sum)
               (loop (+ n 1) (+ sum (char->integer c)))))))))

;; read-line, and peek-char agreeing with read-char across a refill
(p (call-with-input-file tmp
     (lambda (in)
       (let loop ([n 0])
         (if (eof-object? (read-line in)) n (loop (+ n 1)))))))
(p (call-with-input-file tmp
     (lambda (in)
       (let loop ([i 0] [ok #t])
         (if (= i 9000)
             ok
             (let ([a (peek-char in)] [b (read-char in)])
               (loop (+ i 1) (and ok (eqv? a b)))))))))

;; eof stays eof once reached
(p (call-with-input-file tmp
     (lambda (in)
       (let loop () (if (eof-object? (read-char in)) 'at-eof (loop))))))

;; the reader, over a string port and over a file
(p (read (open-input-string "(1 #\\a \"s\" #(2 3) (a . b) -0.5)")))
(call-with-output-file tmp
  (lambda (out) (write '(alpha (beta 2) #(3 "four")) out)))
(p (call-with-input-file tmp read))

;; string ports both ways
(p (let ([out (open-output-string)])
     (write-string "ab" out)
     (write-char #\c out)
     (write 12 out)
     (get-output-string out)))

;; block output: display of a long string is one write, and must come
;; back byte for byte
(define big
  (let loop ([i 0] [acc ""]) (if (= i 300) acc (loop (+ i 1) (string-append acc "0123456789")))))
(call-with-output-file tmp (lambda (out) (display big out)))
(p (string=? big (call-with-input-file tmp
                   (lambda (in)
                     (let loop ([acc '()])
                       (let ([c (read-char in)])
                         (if (eof-object? c)
                             (list->string (reverse acc))
                             (loop (cons c acc)))))))))

(delete-file tmp)
(p (file-exists? tmp))
