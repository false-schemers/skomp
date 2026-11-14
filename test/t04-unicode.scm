(import (scheme base) (scheme write) (scheme char) (scheme read))
(define (p l v) (display l) (display " => ") (write v) (newline))

(define lam  (integer->char #x3BB))      ; GREEK SMALL LETTER LAMBDA
(define LAM  (integer->char #x39B))      ; GREEK CAPITAL LETTER LAMBDA
(define snow (integer->char #x2603))     ; SNOWMAN, category So
(define emo  (integer->char #x1F600))    ; GRINNING FACE, category So
(define a0   (integer->char #x0660))     ; ARABIC-INDIC DIGIT ZERO, Nd
(define nbsp (integer->char #x00A0))     ; NO-BREAK SPACE, Zs
(define idsp (integer->char #x3000))     ; IDEOGRAPHIC SPACE, Zs
(define s3 (string #\a lam #\x))

;; --- representation
(p "string-length of a lambda string" (string-length s3))          ; 3
(p "char->integer lambda"             (char->integer lam))         ; 955
(p "char->integer emoji"              (char->integer emo))         ; 128512
(p "string-ref codepoint"             (char->integer (string-ref s3 1)))
(p "source literal is one char"       (string-length "aλx"))       ; 3

;; --- transport
(p "write char"                       lam)
(p "write string"                     s3)
(p "utf8 round trip"                  (string=? s3 (utf8->string (string->utf8 s3))))
(p "string->utf8"                     (string->utf8 s3))
(p "symbol round trip"                (symbol->string (string->symbol s3)))
(p "symbol length"                    (string-length (symbol->string (string->symbol s3))))

;; --- the reader
(p "read a non-ASCII symbol"          (read (open-input-string "(aλx)")))
(p "read a hex char escape"           (read (open-input-string "#\\x3bb")))
(p "read a non-ASCII string"          (read (open-input-string "\"aλ\"")))

;; --- classification; the R7RS answer is in the comment
(p "char-alphabetic? lambda"          (char-alphabetic? lam))      ; #t
(p "char-alphabetic? snowman"         (char-alphabetic? snow))     ; #f
(p "char-alphabetic? emoji"           (char-alphabetic? emo))      ; #f
(p "char-numeric? arabic zero"        (char-numeric? a0))          ; #t
(p "digit-value arabic zero"          (digit-value a0))            ; 0
(p "char-whitespace? nbsp"            (char-whitespace? nbsp))     ; #t
(p "char-whitespace? ideo space"      (char-whitespace? idsp))     ; #t
(p "char-upper-case? LAMBDA"          (char-upper-case? LAM))      ; #t
(p "char-lower-case? lambda"          (char-lower-case? lam))      ; #t

;; --- case mapping; the R7RS answer is in the comment
(p "char-upcase lambda"               (char-upcase lam))           ; LAMBDA
(p "char-downcase LAMBDA"             (char-downcase LAM))         ; lambda
(p "char-foldcase LAMBDA"             (char-foldcase LAM))         ; lambda
(p "string-upcase"                    (string-upcase s3))          ; A LAMBDA X
(p "string-downcase"                  (string-downcase (string #\A LAM #\X)))
(p "string-ci=? LAMBDA lambda"        (string-ci=? (string LAM) (string lam)))  ; #t
(p "char-ci=? LAMBDA lambda"          (char-ci=? LAM lam))         ; #t

